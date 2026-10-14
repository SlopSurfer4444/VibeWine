/* Executes the shipping adapter, not a copied scheduler. Window doubles supply
 * main-thread lifecycle state; provider snapshots model the real fixture's five
 * nodes. No Wine process, COM provider, NSApplication, or AX client is launched. */
#import <Cocoa/Cocoa.h>
#import <dispatch/dispatch.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static uint64_t test_ms = 10000;
static unsigned int state_reads;
static int test_clock(clockid_t clock, struct timespec *ts)
{ (void)clock; ts->tv_sec = test_ms / 1000; ts->tv_nsec = test_ms % 1000 * 1000000; return 0; }
@interface WineWindow : NSObject
{
@public
    BOOL visible, minimized, applicationHidden, savedVisible;
    NSView *view;
}
- (id)contentView;
- (BOOL)isOrderedIn;
@end
@implementation WineWindow
- (id)contentView { return view; }
- (BOOL)isOrderedIn
{
    if (![NSThread isMainThread]) abort();
    state_reads++;
    return (applicationHidden ? savedVisible : visible) || minimized;
}
@end
@interface WineContentView : NSView
{
@public WineWindow *testWindow;
}
@end
@implementation WineContentView
- (NSWindow *)window { return (NSWindow *)testWindow; }
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
static void test_notification(id element, NSString *name) { (void)element; (void)name; }
#define NSAccessibilityPostNotification test_notification
#define clock_gettime test_clock
static int delayed_lock(pthread_mutex_t *mutex);
#define pthread_mutex_lock delayed_lock
#ifndef AX_SOURCE
#define AX_SOURCE "../../source/dlls/winemac.drv/ax_native.inc"
#endif
#include AX_SOURCE
#undef pthread_mutex_lock
#undef clock_gettime
#undef NSAccessibilityPostNotification
static unsigned int checks;
#define CHECK(test) do { checks++; if (!(test)) { fprintf(stderr,"check %u line %d: %s\n",checks,__LINE__,#test); exit(1); } } while (0)
static void drain(void)
{
    __block BOOL done = NO;
    dispatch_async(dispatch_get_main_queue(), ^{ done = YES; });
    while (!done) CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, YES);
}
static struct macdrv_ax_request poll(void)
{
    struct macdrv_ax_request request = {0};
    request.version = MACDRV_AX_VERSION; request.size = sizeof(request);
    CHECK(!macdrv_ax_poll(&request));
    return request;
}
static void complete(struct macdrv_ax_request request, BOOL ui)
{
    struct macdrv_ax_snapshot snapshot = {0};
    snapshot.version = MACDRV_AX_VERSION; snapshot.size = sizeof(snapshot);
    snapshot.hwnd = request.hwnd; snapshot.generation = request.generation;
    snapshot.revision = request.revision; snapshot.serial = request.serial;
    snapshot.operation = request.operation;
    if (ui)
    {
        snapshot.count = 5;
        for (unsigned int i = 0; i < snapshot.count; i++)
        {
            snapshot.nodes[i].id = i + 1;
            snapshot.nodes[i].token = 100 + i;
            snapshot.nodes[i].role = MACDRV_AX_ROLE_BUTTON;
            snapshot.nodes[i].flags = MACDRV_AX_ENABLED | MACDRV_AX_PRESSABLE;
            snapshot.nodes[i].name[0] = 'A' + i;
        }
    }
    CHECK(!macdrv_ax_publish(&snapshot)); drain();
}

static struct macdrv_ax_snapshot response(struct macdrv_ax_request request)
{
    struct macdrv_ax_snapshot snapshot={0};
    snapshot.version=MACDRV_AX_VERSION;snapshot.size=sizeof(snapshot);
    snapshot.operation=request.operation;snapshot.hwnd=request.hwnd;
    snapshot.generation=request.generation;snapshot.revision=request.revision;snapshot.serial=request.serial;
    snapshot.count=1;snapshot.nodes[0].id=1;snapshot.nodes[0].token=100;
    snapshot.nodes[0].role=MACDRV_AX_ROLE_BUTTON;snapshot.nodes[0].flags=MACDRV_AX_ENABLED|MACDRV_AX_PRESSABLE;
    return snapshot;
}

/* A deterministic lock-contention seam advances the clock after acquisition.
 * This exercises the shipping decision paths with elapsed mutex wait time. */
static uint64_t lock_wait_ms;
static unsigned int lock_delay_after = 1;
static int delayed_lock(pthread_mutex_t *mutex)
{
    int result = pthread_mutex_lock(mutex);
    if (lock_wait_ms && !--lock_delay_after)
    { test_ms += lock_wait_ms; lock_wait_ms = 0; lock_delay_after = 1; }
    return result;
}
int main(int argc, char **argv)
{
@autoreleasepool
{
    setenv("WINE_MACDRV_AX_BRIDGE", "1", 1);
    WineWindow *window = [WineWindow new]; window->visible = YES;
    ax_register_window(window, (void *)0x1234);
    struct macdrv_ax_request request = poll();
    CHECK(request.operation == MACDRV_AX_SNAPSHOT);
    complete(request, YES);
    WineAXElement *element = [ax_windows[0].elements objectAtIndex:0];
    if (argc > 1 && strcmp(argv[1], "refresh"))
    {
        if (!strcmp(argv[1], "queue-time"))
        {
            lock_wait_ms = MACDRV_AX_CACHE_MS + 1;
            CHECK(!ax_queue_action(element, MACDRV_AX_PRESS, nil));
        }
        else if (!strcmp(argv[1], "validate-time"))
        {
            CHECK(ax_queue_action(element, MACDRV_AX_PRESS, nil));
            request = poll(); CHECK(request.operation == MACDRV_AX_PRESS);
            request.operation = MACDRV_AX_VALIDATE;
            lock_wait_ms = MACDRV_AX_DEADLINE_MS + 1;
            CHECK(!macdrv_ax_poll(&request)); CHECK(request.result == MACDRV_AX_STALE);
        }
        else if (!strcmp(argv[1], "publish-time"))
        {
            test_ms += MACDRV_AX_SNAPSHOT_MS; request = poll();
            struct macdrv_ax_snapshot snapshot = response(request);
            lock_wait_ms = MACDRV_AX_DEADLINE_MS + 1;
            CHECK(macdrv_ax_publish(&snapshot) == -1);
        }
        else if (!strcmp(argv[1], "apply-time"))
        {
            test_ms += MACDRV_AX_SNAPSHOT_MS; request = poll();
            struct macdrv_ax_snapshot snapshot = response(request);
            drain(); /* Complete eligibility sweep before scheduling tree application. */
            CHECK(!macdrv_ax_publish(&snapshot));
            lock_delay_after = 2; /* Capture previous tree, then final admission lock. */
            /* Delay a main-queue lock: expiry must be evaluated after it. */
            lock_wait_ms = MACDRV_AX_DEADLINE_MS + 1;
            drain(); CHECK(ax_windows[0].revision != request.revision);
        }
    }
    else
    {
        test_ms += MACDRV_AX_SNAPSHOT_MS;
        request = poll(); CHECK(request.operation == MACDRV_AX_SNAPSHOT);
        CHECK(!ax_action_allowed(element, MACDRV_AX_PRESS));
        CHECK(!ax_queue_action(element, MACDRV_AX_PRESS, nil));
        struct macdrv_ax_snapshot snapshot = response(request);
        CHECK(!macdrv_ax_publish(&snapshot)); /* AppKit publication deliberately withheld. */
        CHECK(!ax_queue_action(element, MACDRV_AX_PRESS, nil));
        CHECK(!poll().operation); /* No successor read before pending publication. */
        drain();
        CHECK(ax_queue_action(element, MACDRV_AX_PRESS, nil));
        test_ms += MACDRV_AX_SNAPSHOT_MS;
        request = poll(); CHECK(request.operation == MACDRV_AX_PRESS); /* action before due read */
        complete(request, NO);
        request = poll(); CHECK(request.operation == MACDRV_AX_SNAPSHOT);
        snapshot = response(request); CHECK(!macdrv_ax_publish(&snapshot));
        test_ms += MACDRV_AX_DEADLINE_MS + 1; drain(); /* expired publication */
        CHECK(!ax_queue_action(element, MACDRV_AX_PRESS, nil));
        ax_windows[0].reader_until_ms = test_ms + MACDRV_AX_CACHE_MS;
        request = poll(); CHECK(request.operation == MACDRV_AX_SNAPSHOT);
        complete(request, YES); /* fresh read recovers admission */
        element = [ax_windows[0].elements objectAtIndex:0];
        CHECK(ax_queue_action(element, MACDRV_AX_PRESS, nil));
        request = poll(); complete(request, NO);
        test_ms += MACDRV_AX_SNAPSHOT_MS;
        request = poll(); CHECK(request.operation == MACDRV_AX_SNAPSHOT);
        snapshot = response(request); snapshot.nodes[0].id = 0; /* invalid tree */
        CHECK(!macdrv_ax_publish(&snapshot)); drain();
        CHECK(![ax_windows[0].elements count]);
        test_ms += MACDRV_AX_SNAPSHOT_MS;
        request = poll(); CHECK(request.operation == MACDRV_AX_SNAPSHOT);
        complete(request, YES);
        element = [ax_windows[0].elements objectAtIndex:0];
        CHECK(ax_queue_action(element, MACDRV_AX_PRESS, nil));
    }
    printf("AX refresh/deadline %s: %u checks PASS\n", argc > 1 ? argv[1] : "refresh", checks);
    ax_unregister_window(window); [window release];
}
return 0;
}
