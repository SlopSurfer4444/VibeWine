/* Cached native half of the MSAA bridge. Included after WineWindow's private interface.
 * Provider tokens preserve retained objects across snapshots; mutable cache reads
 * and action identity capture share ax_mutex. No accessibility entry point calls
 * the PE worker or a COM provider. */
#include "ax_validation.h"
#include <time.h>
#include <stdlib.h>
#include <string.h>

#define AX_ACTION_QUEUE 32
#define AX_ELIGIBILITY_MS (2 * MACDRV_AX_SNAPSHOT_MS)

/* WineWindow implements this predicate later in cocoa_window.m. */
@interface WineWindow (WineAXOrdering)
- (BOOL)isOrderedIn;
@end

@interface WineAXElement : NSAccessibilityElement
{
@public
    struct macdrv_ax_node ax_node;
    uint32_t ax_hwnd;
    uint64_t ax_generation, ax_revision, ax_token;
}
- (id)initWithNode:(const struct macdrv_ax_node *)node request:(const struct macdrv_ax_snapshot *)snapshot;
- (BOOL)accessibilityIsAttributeSettable:(NSAccessibilityAttributeName)attribute;
@end

struct ax_window_cache
{
    WineWindow *window; /* Unretained; registered/unregistered on AppKit's main thread. */
    uint32_t hwnd;
    uint64_t generation, revision, next_revision, received_ms, next_snapshot_ms;
    uint64_t pending_serial, pending_deadline;
    uint64_t eligibility_ms, reader_until_ms;
    BOOL ordered_in;
    NSArray *elements, *roots; /* Immutable arrays, replaced only on AppKit's main thread. */
};
static pthread_mutex_t ax_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct ax_window_cache ax_windows[MACDRV_AX_MAX_WINDOWS];
static struct macdrv_ax_request ax_actions[AX_ACTION_QUEUE], ax_outstanding;
static unsigned int ax_action_count, ax_round_robin;
static uint64_t ax_generation_counter, ax_serial_counter, ax_deadline;
static BOOL ax_quarantined, ax_eligibility_pending;
static uint64_t ax_next_eligibility_ms;

static uint64_t ax_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* All functions suffixed locked require ax_mutex; none sends AppKit messages. */
static struct ax_window_cache *ax_find_locked(uint32_t hwnd, uint64_t generation)
{
    unsigned int i;
    for (i = 0; i < MACDRV_AX_MAX_WINDOWS; i++)
        if (ax_windows[i].window && ax_windows[i].hwnd == hwnd && ax_windows[i].generation == generation)
            return &ax_windows[i];
    return NULL;
}

/* AppKit ownership stays on its main thread. One coalesced asynchronous sweep
 * samples Wine's ordering predicate, including minimized windows and visible
 * state saved when the application is hidden. Worker polling never waits for it.
 * An unserviced main queue expires eligibility instead of polling stale state. */
static void ax_refresh_eligibility_locked(uint64_t now)
{
    if (ax_eligibility_pending || now < ax_next_eligibility_ms) return;
    ax_eligibility_pending = YES;
    ax_next_eligibility_ms = now + MACDRV_AX_SNAPSHOT_MS;
    dispatch_async(dispatch_get_main_queue(), ^{
        @autoreleasepool
        {
            for (unsigned int i = 0; i < MACDRV_AX_MAX_WINDOWS; i++)
            {
                WineWindow *window;
                uint32_t hwnd;
                uint64_t generation, observed_ms;
                BOOL ordered_in;
                pthread_mutex_lock(&ax_mutex);
                window = [ax_windows[i].window retain];
                hwnd = ax_windows[i].hwnd;
                generation = ax_windows[i].generation;
                pthread_mutex_unlock(&ax_mutex);
                if (!window) continue;
                ordered_in = [window isOrderedIn];
                observed_ms = ax_now();
                pthread_mutex_lock(&ax_mutex);
                struct ax_window_cache *cache = ax_find_locked(hwnd, generation);
                if (cache && cache->window == window)
                {
                    cache->ordered_in = ordered_in;
                    cache->eligibility_ms = observed_ms;
                }
                pthread_mutex_unlock(&ax_mutex);
                [window release];
            }
            pthread_mutex_lock(&ax_mutex);
            ax_eligibility_pending = NO;
            pthread_mutex_unlock(&ax_mutex);
        }
    });
}

static BOOL ax_snapshot_eligible_locked(struct ax_window_cache *cache, uint64_t now)
{
    /* A reader that addresses the actual content view may request a hidden
     * window explicitly. Its demand is bounded by the existing cache lifetime. */
    return now < cache->reader_until_ms ||
           (cache->ordered_in && cache->eligibility_ms &&
            now < cache->eligibility_ms + AX_ELIGIBILITY_MS);
}

static BOOL ax_fresh_locked(struct ax_window_cache *cache, uint64_t revision, uint64_t now)
{
    return cache && cache->revision && cache->revision == revision && cache->received_ms &&
           now < cache->received_ms + MACDRV_AX_CACHE_MS;
}

static void ax_expire_locked(uint64_t now)
{
    unsigned int i;
    if (ax_outstanding.operation && now >= ax_deadline)
    {
        /* A hung COM worker cannot be safely replaced. The channel stays fail-closed
         * until this Wine process exits; late results and queued actions are rejected. */
        ax_quarantined = YES;
        memset(&ax_outstanding, 0, sizeof(ax_outstanding));
        ax_action_count = 0;
        for (i = 0; i < MACDRV_AX_MAX_WINDOWS; i++) ax_windows[i].pending_serial = 0;
    }
    for (i = 0; i < MACDRV_AX_MAX_WINDOWS; i++)
        if (ax_windows[i].pending_serial && now >= ax_windows[i].pending_deadline)
            ax_windows[i].pending_serial = 0;
}

static BOOL ax_identity_locked(const struct macdrv_ax_request *request, uint64_t now)
{
    return macdrv_ax_bridge_enabled() && !ax_quarantined &&
           macdrv_ax_request_matches(request, &ax_outstanding, now, ax_deadline) &&
           ax_find_locked(request->hwnd, request->generation);
}

static void ax_register_window(WineWindow *window, void *hwnd)
{
    unsigned int i;
    if (!macdrv_ax_bridge_enabled() || !window || !hwnd) return;
    /* Registration is already AppKit-main-thread owned. */
    BOOL ordered_in = [window isOrderedIn];
    uint64_t now = ax_now();
    pthread_mutex_lock(&ax_mutex);
    for (i = 0; i < MACDRV_AX_MAX_WINDOWS; i++)
        if (!ax_windows[i].window)
        {
            if (ax_generation_counter == UINT64_MAX) { ax_quarantined = YES; break; }
            memset(&ax_windows[i], 0, sizeof(ax_windows[i]));
            ax_windows[i].window = window;
            ax_windows[i].hwnd = (uint32_t)(uintptr_t)hwnd;
            ax_windows[i].generation = ++ax_generation_counter;
            ax_windows[i].ordered_in = ordered_in;
            ax_windows[i].eligibility_ms = now;
            break;
        }
    pthread_mutex_unlock(&ax_mutex);
}

static void ax_unregister_window(WineWindow *window)
{
    unsigned int i, j, n = 0;
    NSArray *elements = nil, *roots = nil;
    pthread_mutex_lock(&ax_mutex);
    for (i = 0; i < MACDRV_AX_MAX_WINDOWS; i++)
        if (ax_windows[i].window == window)
        {
            for (j = 0; j < ax_action_count; j++)
                if (ax_actions[j].hwnd != ax_windows[i].hwnd ||
                    ax_actions[j].generation != ax_windows[i].generation)
                    ax_actions[n++] = ax_actions[j];
            ax_action_count = n;
            /* Keep an in-flight destroyed-window lease until publish or timeout;
             * issuing another COM call while it may still be hung is unsafe. */
            elements = ax_windows[i].elements;
            roots = ax_windows[i].roots;
            memset(&ax_windows[i], 0, sizeof(ax_windows[i]));
            break;
        }
    pthread_mutex_unlock(&ax_mutex);
    [elements release];
    [roots release];
}

static BOOL ax_member_locked(struct ax_window_cache *cache, WineAXElement *element, uint64_t now)
{
    if (!macdrv_ax_bridge_enabled() || !ax_fresh_locked(cache, cache ? cache->revision : 0, now)) return NO;
    for (WineAXElement *candidate in cache->elements)
        if (candidate == element && candidate->ax_token == element->ax_token) return YES;
    return NO;
}

static BOOL ax_element_copy(WineAXElement *element, struct macdrv_ax_node *node)
{
    BOOL live;
    uint64_t now = ax_now();
    pthread_mutex_lock(&ax_mutex);
    ax_expire_locked(now);
    live = macdrv_ax_bridge_enabled() &&
           ax_member_locked(ax_find_locked(element->ax_hwnd, element->ax_generation), element, now);
    if (live && node) *node = element->ax_node;
    pthread_mutex_unlock(&ax_mutex);
    return live;
}

static BOOL ax_node_allow(const struct macdrv_ax_node *node, uint32_t operation)
{
    uint32_t flags = node->flags;
    if (!(flags & MACDRV_AX_ENABLED) || (flags & (MACDRV_AX_PASSWORD | MACDRV_AX_READONLY))) return NO;
    if (operation == MACDRV_AX_PRESS) return node->role == MACDRV_AX_ROLE_BUTTON && (flags & MACDRV_AX_PRESSABLE);
    if (operation == MACDRV_AX_SET_VALUE) return node->role == MACDRV_AX_ROLE_EDIT && (flags & MACDRV_AX_SETTABLE);
    return NO;
}

static BOOL ax_request_target_locked(const struct macdrv_ax_request *request, uint32_t operation, uint64_t now)
{
    struct ax_window_cache *cache = ax_find_locked(request->hwnd, request->generation);
    if (!ax_fresh_locked(cache, request->revision, now) || !request->token) return NO;
    for (WineAXElement *candidate in cache->elements)
        if (candidate->ax_token == request->token && candidate->ax_node.id == request->node_id)
            return ax_node_allow(&candidate->ax_node, operation);
    return NO;
}

/* Metadata and selector checks share atomic liveness/role/permission checks.
 * Queue acceptance repeats these independently before handing work to PE. */
static BOOL ax_action_allowed(WineAXElement *element, uint32_t operation)
{
    BOOL allowed;
    uint64_t now = ax_now();
    pthread_mutex_lock(&ax_mutex);
    ax_expire_locked(now);
    allowed = !ax_quarantined &&
              ax_member_locked(ax_find_locked(element->ax_hwnd, element->ax_generation), element, now) &&
              ax_node_allow(&element->ax_node, operation);
    pthread_mutex_unlock(&ax_mutex);
    return allowed;
}

static NSString *ax_string(const uint16_t *value)
{
    unsigned int length = 0;
    while (length < MACDRV_AX_TEXT && value[length]) length++;
    return [NSString stringWithCharacters:(const unichar *)value length:length];
}

static BOOL ax_queue_action(WineAXElement *element, uint32_t operation, NSString *value)
{
    struct macdrv_ax_request request = {0};
    struct ax_window_cache *cache;
    BOOL allowed = NO;
    uint64_t now;
    if (!macdrv_ax_bridge_enabled()) return NO;
    if (operation == MACDRV_AX_SET_VALUE)
    {
        if (![value isKindOfClass:[NSString class]] || [value length] >= MACDRV_AX_TEXT) return NO;
        [value getCharacters:(unichar *)request.value range:NSMakeRange(0, [value length])];
        if (!macdrv_ax_text_valid(request.value)) return NO;
    }
    request.version = MACDRV_AX_VERSION;
    request.size = sizeof(request);
    request.operation = operation;
    request.hwnd = element->ax_hwnd;
    request.generation = element->ax_generation;
    now = ax_now();
    pthread_mutex_lock(&ax_mutex);
    ax_expire_locked(now);
    cache = ax_find_locked(request.hwnd, request.generation);
    if (!ax_quarantined && ax_member_locked(cache, element, now) &&
        ax_node_allow(&element->ax_node, operation) && ax_action_count < AX_ACTION_QUEUE)
    {
        /* A retained object resolves the current provider node atomically. */
        request.revision = cache->revision;
        request.node_id = element->ax_node.id;
        request.token = element->ax_token;
        ax_actions[ax_action_count++] = request;
        allowed = YES; /* Accepted into a bounded queue, not provider completion. */
    }
    pthread_mutex_unlock(&ax_mutex);
    return allowed;
}

static NSArray *ax_children_for_view(WineContentView *view)
{
    unsigned int i;
    NSArray *children = nil;
    WineWindow *window = (WineWindow *)[view window];
    if (!macdrv_ax_bridge_enabled() || [window contentView] != view) return nil;
    uint64_t now = ax_now();
    pthread_mutex_lock(&ax_mutex);
    ax_expire_locked(now);
    for (i = 0; i < MACDRV_AX_MAX_WINDOWS; i++)
        if (ax_windows[i].window == window)
        {
            ax_windows[i].reader_until_ms = now + MACDRV_AX_CACHE_MS;
            if (ax_fresh_locked(&ax_windows[i], ax_windows[i].revision, now))
                children = [ax_windows[i].roots retain];
            break;
        }
    pthread_mutex_unlock(&ax_mutex);
    return [children autorelease];
}

@implementation WineAXElement
- (id)initWithNode:(const struct macdrv_ax_node *)node request:(const struct macdrv_ax_snapshot *)snapshot
{
    if ((self = [super init]))
    {
        ax_node = *node;
        ax_hwnd = snapshot->hwnd;
        ax_generation = snapshot->generation;
        ax_revision = snapshot->revision;
        ax_token = node->token;
    }
    return self;
}
- (BOOL)isAccessibilityElement { return ax_element_copy(self, NULL); }
- (NSString *)accessibilityRole
{
    struct macdrv_ax_node node;
    if (!ax_element_copy(self, &node)) return NSAccessibilityUnknownRole;
    if (node.role == MACDRV_AX_ROLE_BUTTON) return NSAccessibilityButtonRole;
    if (node.role == MACDRV_AX_ROLE_EDIT) return NSAccessibilityTextFieldRole;
    return NSAccessibilityGroupRole;
}
- (NSString *)accessibilityLabel
{
    struct macdrv_ax_node node;
    return ax_element_copy(self, &node) ? ax_string(node.name) : nil;
}
- (NSString *)accessibilityTitle { return [self accessibilityLabel]; }
- (id)accessibilityValue
{
    struct macdrv_ax_node node;
    if (!ax_element_copy(self, &node) || (node.flags & MACDRV_AX_PASSWORD)) return nil;
    return ax_string(node.value);
}
- (BOOL)isAccessibilityEnabled
{
    struct macdrv_ax_node node;
    return ax_element_copy(self, &node) && (node.flags & MACDRV_AX_ENABLED);
}
- (BOOL)isAccessibilityFocused
{
    struct macdrv_ax_node node;
    return ax_element_copy(self, &node) && (node.flags & MACDRV_AX_FOCUSED);
}
- (NSRect)accessibilityFrame
{
    struct macdrv_ax_node node;
    if (!ax_element_copy(self, &node)) return NSZeroRect;
    NSRect rect = NSRectFromCGRect(cgrect_mac_from_win(CGRectMake(node.x, node.y, node.width, node.height)));
    [[WineApplicationController sharedController] flipRect:&rect];
    return rect;
}
- (id)accessibilityParent
{
    id parent = nil;
    WineWindow *window = nil;
    uint64_t now = ax_now();
    pthread_mutex_lock(&ax_mutex);
    ax_expire_locked(now);
    struct ax_window_cache *cache = ax_find_locked(ax_hwnd, ax_generation);
    if (ax_member_locked(cache, self, now))
    {
        if (!ax_node.parent) window = [cache->window retain];
        else for (WineAXElement *candidate in cache->elements)
            if (candidate->ax_node.id == ax_node.parent) { parent = [candidate retain]; break; }
    }
    pthread_mutex_unlock(&ax_mutex);
    if (window) { parent = [[window contentView] retain]; [window release]; }
    return [parent autorelease];
}
- (NSArray *)accessibilityChildren
{
    NSMutableArray *children = [NSMutableArray array];
    uint64_t now = ax_now();
    pthread_mutex_lock(&ax_mutex);
    ax_expire_locked(now);
    struct ax_window_cache *cache = ax_find_locked(ax_hwnd, ax_generation);
    if (ax_member_locked(cache, self, now))
        for (WineAXElement *candidate in cache->elements)
            if (candidate->ax_node.parent == ax_node.id) [children addObject:candidate];
    pthread_mutex_unlock(&ax_mutex);
    return [NSArray arrayWithArray:children];
}
- (BOOL)accessibilityPerformPress { return ax_queue_action(self, MACDRV_AX_PRESS, nil); }
- (void)setAccessibilityValue:(id)value { ax_queue_action(self, MACDRV_AX_SET_VALUE, value); }
- (BOOL)isAccessibilitySelectorAllowed:(SEL)selector
{
    if (selector == @selector(accessibilityPerformPress)) return ax_action_allowed(self, MACDRV_AX_PRESS);
    if (selector == @selector(setAccessibilityValue:)) return ax_action_allowed(self, MACDRV_AX_SET_VALUE);
    if (!ax_element_copy(self, NULL)) return NO;
    return [super isAccessibilitySelectorAllowed:selector];
}
- (BOOL)accessibilityIsAttributeSettable:(NSAccessibilityAttributeName)attribute
{
    /* AppKit's modern selector-invocation guard does not govern AXValue's
     * writable metadata on the tested runtime. The supported legacy metadata
     * hook is explicit; provider action admission stays independently guarded. */
    if ([attribute isEqualToString:NSAccessibilityValueAttribute])
        return ax_action_allowed(self, MACDRV_AX_SET_VALUE);
    if (!ax_element_copy(self, NULL)) return NO;
    /* NSAccessibilityElement has no legacy implementation on modern AppKit.
     * Preserve it where present, and keep our cached informational data readonly. */
    if ([NSAccessibilityElement instancesRespondToSelector:@selector(accessibilityIsAttributeSettable:)])
    {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        return [super accessibilityIsAttributeSettable:attribute];
#pragma clang diagnostic pop
    }
    return NO;
}
@end

/* Native-owned request lease. This runs on the PE worker's unix-call thread. */
int32_t macdrv_ax_poll(void *args)
{
    struct macdrv_ax_request *request = args, input;
    struct ax_window_cache *cache;
    unsigned int i;
    uint64_t now = ax_now();
    if (!request) return -1;
    input = *request;
    if (input.version != MACDRV_AX_VERSION || input.size != sizeof(input)) return -1;
    if (input.operation != MACDRV_AX_NONE && input.operation != MACDRV_AX_VALIDATE) return -1;
    pthread_mutex_lock(&ax_mutex);
    ax_expire_locked(now);
    if (input.operation == MACDRV_AX_VALIDATE)
    {
        request->result = ax_identity_locked(&input, now) ? MACDRV_AX_OK : MACDRV_AX_STALE;
        /* Action authorization is independent of the snapshot/request identity. */
        if (request->result == MACDRV_AX_OK && ax_outstanding.operation != MACDRV_AX_SNAPSHOT)
        {
            if (!ax_request_target_locked(&input, ax_outstanding.operation, now)) request->result = MACDRV_AX_STALE;
        }
        pthread_mutex_unlock(&ax_mutex);
        return 0;
    }
    memset(request, 0, sizeof(*request));
    request->version = MACDRV_AX_VERSION;
    request->size = sizeof(*request);
    if (!macdrv_ax_bridge_enabled() || ax_quarantined || ax_outstanding.operation) { pthread_mutex_unlock(&ax_mutex); return 0; }
    ax_refresh_eligibility_locked(now);
    while (ax_action_count)
    {
        *request = ax_actions[0];
        memmove(ax_actions, ax_actions + 1, --ax_action_count * sizeof(ax_actions[0]));
        if (ax_request_target_locked(request, request->operation, now)) break;
        request->operation = MACDRV_AX_NONE;
    }
    if (!request->operation)
    {
        memset(request, 0, sizeof(*request));
        request->version = MACDRV_AX_VERSION;
        request->size = sizeof(*request);
        for (i = 0; i < MACDRV_AX_MAX_WINDOWS; i++)
        {
            unsigned int index = (ax_round_robin + i) % MACDRV_AX_MAX_WINDOWS;
            cache = &ax_windows[index];
            if (!cache->window || !ax_snapshot_eligible_locked(cache, now) || now < cache->next_snapshot_ms) continue;
            if (cache->next_revision == UINT64_MAX) { ax_quarantined = YES; break; }
            request->operation = MACDRV_AX_SNAPSHOT;
            request->hwnd = cache->hwnd;
            request->generation = cache->generation;
            request->revision = ++cache->next_revision;
            request->node_id = 0;
            cache->next_snapshot_ms = now + MACDRV_AX_SNAPSHOT_MS;
            ax_round_robin = (index + 1) % MACDRV_AX_MAX_WINDOWS;
            break;
        }
    }
    if (request->operation)
    {
        if (ax_serial_counter == UINT64_MAX) { ax_quarantined = YES; request->operation = MACDRV_AX_NONE; }
        else
        {
            request->serial = ++ax_serial_counter;
            ax_outstanding = *request;
            ax_deadline = now + MACDRV_AX_DEADLINE_MS;
        }
    }
    pthread_mutex_unlock(&ax_mutex);
    return 0;
}

/* Synchronous copy, admission by identity/deadline, then immutable main-thread tree replacement. */
int32_t macdrv_ax_publish(void *args)
{
    struct macdrv_ax_snapshot *copy;
    struct macdrv_ax_request identity = {0};
    struct ax_window_cache *cache;
    uint64_t deadline, now = ax_now();
    BOOL accepted;
    if (!macdrv_ax_bridge_enabled() || !args) return -1;
    copy = malloc(sizeof(*copy));
    if (!copy) return -1;
    memcpy(copy, args, sizeof(*copy));
    if (copy->version != MACDRV_AX_VERSION || copy->size != sizeof(*copy) || copy->result > MACDRV_AX_TIMEOUT)
    { free(copy); return -1; }
    if (copy->operation == MACDRV_AX_SNAPSHOT && copy->result == MACDRV_AX_OK && !macdrv_ax_snapshot_valid(copy))
        copy->result = MACDRV_AX_DENIED;
    identity.hwnd = copy->hwnd;
    identity.generation = copy->generation;
    identity.revision = copy->revision;
    identity.serial = copy->serial;
    pthread_mutex_lock(&ax_mutex);
    ax_expire_locked(now);
    identity.node_id = ax_outstanding.node_id;
    identity.token = ax_outstanding.token;
    memcpy(identity.value, ax_outstanding.value, sizeof(identity.value));
    accepted = ax_identity_locked(&identity, now) && copy->operation == ax_outstanding.operation;
    deadline = ax_deadline;
    if (accepted)
    {
        cache = ax_find_locked(copy->hwnd, copy->generation);
        memset(&ax_outstanding, 0, sizeof(ax_outstanding));
        if (copy->operation == MACDRV_AX_SNAPSHOT)
        {
            cache->pending_serial = copy->serial;
            cache->pending_deadline = deadline;
        }
    }
    else if (!ax_quarantined && copy->operation == ax_outstanding.operation &&
             macdrv_ax_request_matches(&identity, &ax_outstanding, now, ax_deadline))
    {
        /* A revoked/destroyed window completed its prior call. Release that
         * lease without admitting its result or issuing concurrent COM work. */
        memset(&ax_outstanding, 0, sizeof(ax_outstanding));
    }
    pthread_mutex_unlock(&ax_mutex);
    if (!accepted || copy->operation != MACDRV_AX_SNAPSHOT) { free(copy); return accepted ? 0 : -1; }
    /* Sanitize before handing off the immutable copy to AppKit. */
    for (unsigned int i = 0; i < copy->count && i < MACDRV_AX_MAX_NODES; i++)
        if (copy->nodes[i].flags & MACDRV_AX_PASSWORD)
            memset(copy->nodes[i].value, 0, sizeof(copy->nodes[i].value));
    dispatch_async(dispatch_get_main_queue(), ^{
        @autoreleasepool
        {
            NSMutableArray *elements = [NSMutableArray array], *roots = [NSMutableArray array];
            NSArray *old_elements = nil, *old_roots = nil, *previous_elements = nil;
            WineWindow *window = nil;
            struct ax_window_cache *apply_cache;
            uint64_t apply_now;
            unsigned int i;
            /* Capture previous membership only. Allocation and array construction
             * happen outside the lock; retained identity tokens never mutate. */
            pthread_mutex_lock(&ax_mutex);
            apply_cache = ax_find_locked(copy->hwnd, copy->generation);
            if (apply_cache && apply_cache->pending_serial == copy->serial)
                previous_elements = [apply_cache->elements retain];
            pthread_mutex_unlock(&ax_mutex);
            if (copy->result == MACDRV_AX_OK)
                for (i = 0; i < copy->count; i++)
                {
                    WineAXElement *element = nil;
                    for (WineAXElement *previous in previous_elements)
                        if (previous->ax_token == copy->nodes[i].token) { element = previous; break; }
                    if (!element)
                        element = [[[WineAXElement alloc] initWithNode:&copy->nodes[i] request:copy] autorelease];
                    [elements addObject:element];
                    if (!copy->nodes[i].parent) [roots addObject:element];
                }
            NSArray *new_elements = [elements copy], *new_roots = [roots copy];
            apply_now = ax_now();
            pthread_mutex_lock(&ax_mutex);
            ax_expire_locked(apply_now);
            apply_cache = ax_find_locked(copy->hwnd, copy->generation);
            if (!ax_quarantined && apply_cache && apply_cache->pending_serial == copy->serial &&
                apply_now < deadline && apply_now < apply_cache->pending_deadline)
            {
                /* All mutable fields update with the same lock used by getters
                 * and action capture. Objects absent from prior membership are
                 * never resurrected, even if a removed token later reappears. */
                for (i = 0; i < [new_elements count]; i++)
                {
                    WineAXElement *element = [new_elements objectAtIndex:i];
                    element->ax_node = copy->nodes[i];
                    element->ax_revision = copy->revision;
                }
                old_elements = apply_cache->elements;
                old_roots = apply_cache->roots;
                apply_cache->elements = new_elements;
                apply_cache->roots = new_roots;
                new_elements = new_roots = nil;
                apply_cache->revision = copy->revision;
                apply_cache->received_ms = apply_now;
                apply_cache->pending_serial = 0;
                window = [apply_cache->window retain];
            }
            pthread_mutex_unlock(&ax_mutex);
            [previous_elements release];
            [old_elements release]; [old_roots release];
            [new_elements release]; [new_roots release];
            if (window)
            {
                NSAccessibilityPostNotification([window contentView], NSAccessibilityLayoutChangedNotification);
                [window release];
            }
            free(copy);
        }
    });
    return 0;
}
