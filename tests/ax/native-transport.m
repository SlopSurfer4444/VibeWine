/* Offline execution of the actual native transport; no NSApplication or Wine launch.
 * Stub windows model object lifetime only. No fake UI enters the shipping bridge. */
#import <Cocoa/Cocoa.h>
#import <dispatch/dispatch.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
@interface WineWindow : NSObject
- (id)contentView;
- (BOOL)isOrderedIn;
@end
@implementation WineWindow
- (id)contentView { return nil; }
- (BOOL)isOrderedIn { return YES; }
@end
@interface WineContentView : NSView
@end
@implementation WineContentView
@end
@interface WineApplicationController : NSObject
+ (id)sharedController;
- (void)flipRect:(NSRect *)rect;
@end
@implementation WineApplicationController
+ (id)sharedController { return nil; }
- (void)flipRect:(NSRect *)rect { (void)rect; }
@end
static CGRect cgrect_mac_from_win(CGRect rect) { return rect; }
static unsigned int notifications;
static void ax_test_notification(id element, NSString *name)
{ (void)element; (void)name; notifications++; }
#define NSAccessibilityPostNotification ax_test_notification
#include "../../source/dlls/winemac.drv/ax_native.inc"
#undef NSAccessibilityPostNotification

static unsigned int checks;
#define CHECK(test) do { checks++; if (!(test)) { fprintf(stderr,"check %u failed at line %d: %s\n",checks,__LINE__,#test); exit(1); } } while (0)

static void drain_main_queue(void)
{
    __block BOOL drained = NO;
    dispatch_async(dispatch_get_main_queue(), ^{ drained = YES; });
    while (!drained) CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, YES);
}

static struct macdrv_ax_request poll_request(void)
{
    struct macdrv_ax_request request = {0};
    request.version = MACDRV_AX_VERSION;
    request.size = sizeof(request);
    CHECK(macdrv_ax_poll(&request) == 0);
    return request;
}

static struct macdrv_ax_snapshot snapshot_for(struct macdrv_ax_request request)
{
    struct macdrv_ax_snapshot snapshot = {0};
    snapshot.version = MACDRV_AX_VERSION;
    snapshot.size = sizeof(snapshot);
    snapshot.operation = request.operation;
    snapshot.hwnd = request.hwnd;
    snapshot.generation = request.generation;
    snapshot.revision = request.revision;
    snapshot.serial = request.serial;
    snapshot.count = 1;
    snapshot.nodes[0].id = 1;
    snapshot.nodes[0].token = 101;
    snapshot.nodes[0].role = MACDRV_AX_ROLE_BUTTON;
    snapshot.nodes[0].flags = MACDRV_AX_ENABLED | MACDRV_AX_PRESSABLE;
    snapshot.nodes[0].name[0] = 'G';
    return snapshot;
}

int main(void)
{
@autoreleasepool
{
    setenv("WINE_MACDRV_AX_BRIDGE", "1", 1);
    WineWindow *first = [WineWindow new], *second = [WineWindow new];
    struct macdrv_ax_request request, validate, pending;
    struct macdrv_ax_snapshot snapshot;
    struct ax_window_cache *cache;
    uint64_t old_generation, held_since;
    WineAXElement *element, *retained_child;

    CHECK(macdrv_ax_poll(NULL) == -1);
    CHECK(macdrv_ax_publish(NULL) == -1);
    request = poll_request();
    CHECK(request.operation == MACDRV_AX_NONE);
    request.version++;
    CHECK(macdrv_ax_poll(&request) == -1);
    request.version = MACDRV_AX_VERSION;
    request.size--;
    CHECK(macdrv_ax_poll(&request) == -1);
    request.size = sizeof(request);
    request.operation = MACDRV_AX_PRESS;
    CHECK(macdrv_ax_poll(&request) == -1);

    ax_register_window(first, (void *)(uintptr_t)0x1234);
    request = poll_request();
    CHECK(request.operation == MACDRV_AX_SNAPSHOT && request.hwnd == 0x1234);
    CHECK(request.generation && request.revision && request.serial);
    old_generation = request.generation;
    pending = poll_request();
    CHECK(pending.operation == MACDRV_AX_NONE); /* Never issue concurrent worker work. */
    validate = request;
    validate.operation = MACDRV_AX_VALIDATE;
    CHECK(macdrv_ax_poll(&validate) == 0 && validate.result == MACDRV_AX_OK);
    validate.serial++;
    CHECK(macdrv_ax_poll(&validate) == 0 && validate.result == MACDRV_AX_STALE);
    validate = request;
    validate.operation = MACDRV_AX_VALIDATE;
    validate.value[0] = 'x';
    CHECK(macdrv_ax_poll(&validate) == 0 && validate.result == MACDRV_AX_STALE);

    snapshot = snapshot_for(request);
    CHECK(macdrv_ax_publish(&snapshot) == 0);
    cache = ax_find_locked(request.hwnd, request.generation);
    CHECK(cache && cache->pending_serial == request.serial && !cache->revision);
    CHECK(!ax_outstanding.operation); /* Publish is accepted before AppKit queue drain. */
    snapshot.nodes[0].name[0] = 'Z'; /* Caller storage may immediately be reused. */
    drain_main_queue();
    CHECK(cache->revision == request.revision && [cache->roots count] == 1);
    retained_child = [[cache->roots objectAtIndex:0] retain];
    CHECK([[retained_child accessibilityLabel] isEqualToString:@"G"] && notifications == 1);
    held_since = ax_now();
    for (unsigned int cycle = 0; cycle < 13; cycle++)
    {
        usleep(250000);
        cache->next_snapshot_ms = 0;
    cache->eligibility_ms = ax_now();
        request = poll_request();
        CHECK(request.operation == MACDRV_AX_SNAPSHOT);
        snapshot = snapshot_for(request);
        snapshot.nodes[0].id = 100 + cycle; /* Traversal indices may legitimately change. */
        snapshot.nodes[0].name[0] = 'A' + cycle;
        CHECK(macdrv_ax_publish(&snapshot) == 0);
        drain_main_queue();
        CHECK([cache->roots objectAtIndex:0] == retained_child);
        CHECK([retained_child isAccessibilityElement] && [retained_child accessibilityPerformPress]);
        validate = poll_request();
        CHECK(validate.operation == MACDRV_AX_PRESS && validate.token == 101 &&
              validate.node_id == snapshot.nodes[0].id && validate.revision == snapshot.revision);
        pending = validate;
        pending.operation = MACDRV_AX_VALIDATE;
        CHECK(macdrv_ax_poll(&pending) == 0 && pending.result == MACDRV_AX_OK);
        struct macdrv_ax_snapshot action_result = snapshot_for(validate);
        CHECK(macdrv_ax_publish(&action_result) == 0);
    }
    CHECK(ax_now() - held_since >= 3000); /* One actual retained object, multiple real refresh cycles. */
    cache->next_snapshot_ms = 0;
    cache->eligibility_ms = ax_now();
    request = poll_request();
    snapshot = snapshot_for(request);
    snapshot.nodes[0].role = MACDRV_AX_ROLE_EDIT;
    snapshot.nodes[0].flags = MACDRV_AX_ENABLED | MACDRV_AX_SETTABLE;
    CHECK(macdrv_ax_publish(&snapshot) == 0);
    drain_main_queue();
    CHECK([cache->roots objectAtIndex:0] == retained_child &&
          [retained_child accessibilityIsAttributeSettable:NSAccessibilityValueAttribute]);
    cache->next_snapshot_ms = 0;
    cache->eligibility_ms = ax_now();
    request = poll_request();
    snapshot.nodes[0].flags |= MACDRV_AX_READONLY;
    snapshot.revision = request.revision;
    snapshot.serial = request.serial;
    CHECK(macdrv_ax_publish(&snapshot) == 0);
    drain_main_queue();
    CHECK([cache->roots objectAtIndex:0] == retained_child &&
          ![retained_child accessibilityIsAttributeSettable:NSAccessibilityValueAttribute]);
    [retained_child setAccessibilityValue:@"denied"];
    CHECK(ax_action_count == 0);

    cache->next_snapshot_ms = 0;
    cache->eligibility_ms = ax_now();
    request = poll_request();
    snapshot = snapshot_for(request);
    snapshot.nodes[0] = retained_child->ax_node;
    snapshot.nodes[0].token = 202; /* Same node id/name/shape, recreated provider lifetime. */
    CHECK(macdrv_ax_publish(&snapshot) == 0);
    drain_main_queue();
    CHECK([cache->roots objectAtIndex:0] != retained_child);
    CHECK(![retained_child isAccessibilityElement] && ![retained_child accessibilityPerformPress]);
    CHECK(![retained_child accessibilityIsAttributeSettable:NSAccessibilityValueAttribute]);
    cache->next_snapshot_ms = 0;
    cache->eligibility_ms = ax_now();
    request = poll_request();
    snapshot = snapshot_for(request); /* Removed token 101 returns; old object must not resurrect. */
    CHECK(macdrv_ax_publish(&snapshot) == 0);
    drain_main_queue();
    CHECK([cache->roots objectAtIndex:0] != retained_child && ![retained_child isAccessibilityElement]);
    [retained_child release];
    retained_child = [[cache->roots objectAtIndex:0] retain];

    CHECK(macdrv_ax_publish(&snapshot) == -1); /* Duplicate serial never re-admitted. */
    ax_unregister_window(first); /* Invalidates the pending immutable tree. */
    CHECK(!ax_find_locked(request.hwnd, request.generation));
    CHECK(![retained_child isAccessibilityElement] && ![retained_child accessibilityPerformPress]);
    [retained_child release];

    ax_register_window(second, (void *)(uintptr_t)0x1234);
    request = poll_request();
    CHECK(request.generation != old_generation);
    snapshot = snapshot_for(request);
    ax_unregister_window(second);
    CHECK(macdrv_ax_publish(&snapshot) == -1); /* Destroyed generation cannot publish. */
    CHECK(!ax_outstanding.operation); /* Completed revoked lease does not block others. */

    ax_register_window(first, (void *)(uintptr_t)0x5678);
    request = poll_request();
    snapshot = snapshot_for(request);
    snapshot.size--;
    CHECK(macdrv_ax_publish(&snapshot) == -1);
    snapshot.size = sizeof(snapshot);
    snapshot.result = MACDRV_AX_TIMEOUT + 1;
    CHECK(macdrv_ax_publish(&snapshot) == -1);
    snapshot.result = MACDRV_AX_OK;
    snapshot.nodes[0].name[0] = 0xd800; /* Reject malformed UTF16 before NSString creation. */
    CHECK(macdrv_ax_publish(&snapshot) == 0);
    CHECK(!ax_outstanding.operation);
    cache = ax_find_locked(request.hwnd, request.generation);
    CHECK(cache && cache->pending_serial == request.serial);
    /* Establish a cached node directly so queue/TTL guards can be tested without AppKit drain. */
    cache->pending_serial = 0;
    cache->revision = request.revision;
    cache->received_ms = ax_now();
    snapshot = snapshot_for(request);
    element = [[WineAXElement alloc] initWithNode:&snapshot.nodes[0] request:&snapshot];
    cache->elements = [[NSArray alloc] initWithObjects:element, nil];
    cache->roots = [cache->elements retain];
    element->ax_node.role = MACDRV_AX_ROLE_EDIT;
    element->ax_node.flags = MACDRV_AX_ENABLED | MACDRV_AX_SETTABLE;
    CHECK([element accessibilityIsAttributeSettable:NSAccessibilityValueAttribute]);
    unsetenv("WINE_MACDRV_AX_BRIDGE");
    CHECK(![element accessibilityIsAttributeSettable:NSAccessibilityValueAttribute]);
    CHECK(![element isAccessibilityElement] && ![element accessibilityPerformPress]);
    pending = poll_request();
    CHECK(pending.operation == MACDRV_AX_NONE);
    ax_register_window(second, (void *)(uintptr_t)0x9876);
    CHECK(!ax_find_locked(0x9876, ax_generation_counter));
    setenv("WINE_MACDRV_AX_BRIDGE", "1", 1);
    element->ax_node = snapshot.nodes[0];
    CHECK([element accessibilityPerformPress]);
    ax_action_count = 0;
    element->ax_node.flags = MACDRV_AX_PRESSABLE;
    CHECK(![element accessibilityPerformPress]);
    element->ax_node.flags = MACDRV_AX_ENABLED | MACDRV_AX_PRESSABLE | MACDRV_AX_READONLY;
    CHECK(![element accessibilityPerformPress]);
    element->ax_node.flags = MACDRV_AX_ENABLED | MACDRV_AX_PRESSABLE | MACDRV_AX_PASSWORD;
    CHECK(![element accessibilityPerformPress] && [element accessibilityValue] == nil);
    element->ax_node.role = MACDRV_AX_ROLE_EDIT;
    element->ax_node.flags = MACDRV_AX_ENABLED | MACDRV_AX_SETTABLE;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CHECK([element accessibilityIsAttributeSettable:NSAccessibilityValueAttribute]);
    CHECK(![element accessibilityIsAttributeSettable:NSAccessibilityRoleAttribute]);
    element->ax_node.flags |= MACDRV_AX_PASSWORD;
    CHECK(![element accessibilityIsAttributeSettable:NSAccessibilityValueAttribute]);
    element->ax_node.flags = MACDRV_AX_SETTABLE;
    CHECK(![element accessibilityIsAttributeSettable:NSAccessibilityValueAttribute]);
    element->ax_node.flags = MACDRV_AX_ENABLED | MACDRV_AX_SETTABLE;
    element->ax_node.role = MACDRV_AX_ROLE_BUTTON;
    CHECK(![element accessibilityIsAttributeSettable:NSAccessibilityValueAttribute]);
    CHECK(!ax_queue_action(element, MACDRV_AX_SET_VALUE, @"button must not write"));
    element->ax_node.role = MACDRV_AX_ROLE_EDIT;
    element->ax_node.flags |= MACDRV_AX_READONLY;
    CHECK(![element accessibilityIsAttributeSettable:NSAccessibilityValueAttribute]);
    element->ax_node.flags &= ~MACDRV_AX_READONLY;
#pragma clang diagnostic pop
    CHECK(ax_queue_action(element, MACDRV_AX_SET_VALUE, @"abc"));
    ax_action_count = 0;
    element->ax_node.flags |= MACDRV_AX_READONLY;
    CHECK(!ax_queue_action(element, MACDRV_AX_SET_VALUE, @"abc"));
    element->ax_node.role = MACDRV_AX_ROLE_BUTTON;
    element->ax_node.flags = MACDRV_AX_ENABLED | MACDRV_AX_PRESSABLE;
    for (unsigned int i = 0; i < AX_ACTION_QUEUE; i++) CHECK([element accessibilityPerformPress]);
    CHECK(![element accessibilityPerformPress] && ax_action_count == AX_ACTION_QUEUE);
    request = poll_request();
    CHECK(request.operation == MACDRV_AX_PRESS && request.node_id == 1);
    validate = request;
    validate.operation = MACDRV_AX_VALIDATE;
    CHECK(macdrv_ax_poll(&validate) == 0 && validate.result == MACDRV_AX_OK);
    cache->revision++;
    CHECK(macdrv_ax_poll(&validate) == 0 && validate.result == MACDRV_AX_STALE);
    snapshot = snapshot_for(request);
    CHECK(macdrv_ax_publish(&snapshot) == 0);
    CHECK(![element accessibilityPerformPress]); /* Provider/native revisions differ. */
    cache->next_revision = cache->revision; /* Model a fully published refresh. */
    CHECK([element accessibilityPerformPress]); /* Same current membership survives publication. */
    CHECK(ax_actions[ax_action_count - 1].revision == cache->revision &&
          ax_actions[ax_action_count - 1].token == element->ax_token);
    cache->revision = cache->next_revision = element->ax_revision;
    cache->received_ms = ax_now() - MACDRV_AX_CACHE_MS;
    CHECK(![element isAccessibilityElement] && ![element accessibilityPerformPress]);
    CHECK(![element accessibilityIsAttributeSettable:NSAccessibilityValueAttribute]);
    cache->received_ms = ax_now();
    cache->next_snapshot_ms = 0;
    cache->eligibility_ms = ax_now();
    ax_action_count = 0;
    request = poll_request();
    CHECK(request.operation == MACDRV_AX_SNAPSHOT);
    validate = request;
    validate.operation = MACDRV_AX_VALIDATE;
    ax_actions[0] = request;
    ax_action_count = 1;
    cache->pending_serial = 42;
    element->ax_node.role = MACDRV_AX_ROLE_EDIT;
    element->ax_node.flags = MACDRV_AX_ENABLED | MACDRV_AX_SETTABLE;
    CHECK(![element accessibilityIsAttributeSettable:NSAccessibilityValueAttribute]); /* Refresh in flight. */
    ax_deadline = ax_now(); /* Exact deadline is expired, not inclusive. */
    CHECK(macdrv_ax_poll(&validate) == 0 && validate.result == MACDRV_AX_STALE);
    CHECK(ax_quarantined && !ax_action_count && !cache->pending_serial);
    CHECK(![element isAccessibilityElement] && ![element accessibilityIsAttributeSettable:NSAccessibilityValueAttribute]);
    CHECK(![element isAccessibilitySelectorAllowed:@selector(setAccessibilityValue:)]);
    [element setAccessibilityValue:@"quarantine denied"];
    CHECK(!ax_action_count);
    snapshot = snapshot_for(request);
    CHECK(macdrv_ax_publish(&snapshot) == -1); /* Completed expired read is never applied. */
    CHECK(!ax_quarantined && !ax_snapshot_expired && !ax_outstanding.operation);
    cache->next_snapshot_ms = 0; /* Test due work, not the unchanged 250ms cadence. */
    pending = poll_request();
    CHECK(pending.operation == MACDRV_AX_SNAPSHOT && ![element accessibilityPerformPress]);
    ax_unregister_window(first);
    [element release];
    [first release]; [second release];
    /* Drain revoked queued snapshots; no native windows or accessibility notifications occur. */
    drain_main_queue();
    printf("native transport: %u checks passed\n", checks);
}
    return 0;
}
