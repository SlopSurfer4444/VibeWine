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
#ifdef AX_BASELINE
#include "references/baseline/ax_native.inc"
#else
#include "../../source/dlls/winemac.drv/ax_native.inc"
#endif
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
int main(void)
{
@autoreleasepool
{
    setenv("WINE_MACDRV_AX_BRIDGE", "1", 1);
    WineWindow *window = [WineWindow new];
    window->visible = YES;
    ax_register_window(window, (void *)(uintptr_t)0x1234);
    struct macdrv_ax_request first = poll();
    CHECK(first.operation == MACDRV_AX_SNAPSHOT);
    struct macdrv_ax_snapshot snapshot = {0};
    snapshot.version = MACDRV_AX_VERSION; snapshot.size = sizeof(snapshot);
    snapshot.operation = first.operation; snapshot.hwnd = first.hwnd;
    snapshot.generation = first.generation; snapshot.revision = first.revision; snapshot.serial = first.serial;
    snapshot.count = 1; snapshot.nodes[0].id = 1; snapshot.nodes[0].token = 100;
    snapshot.nodes[0].role = MACDRV_AX_ROLE_BUTTON; snapshot.nodes[0].flags = MACDRV_AX_ENABLED | MACDRV_AX_PRESSABLE;
    CHECK(!macdrv_ax_publish(&snapshot));
    struct ax_window_cache *cache = ax_find_locked(first.hwnd, first.generation);
    CHECK(cache->pending_serial == first.serial && !cache->revision);
    /* Main queue has not yet applied the admitted copy when next 250ms epoch
     * comes due. A worker poll must not supersede that pending serial. */
    test_ms += MACDRV_AX_SNAPSHOT_MS;
    struct macdrv_ax_request second = poll();
#ifdef AX_BASELINE
    CHECK(second.operation == MACDRV_AX_SNAPSHOT);
    snapshot.revision = second.revision; snapshot.serial = second.serial;
    CHECK(!macdrv_ax_publish(&snapshot));
#else
    CHECK(second.operation == MACDRV_AX_NONE);
    CHECK(cache->pending_serial == first.serial);
#endif
    uint64_t receipt = test_ms;
#ifdef AX_BASELINE
    /* Baseline's first receipt was superseded; compare TTL against last receipt. */
#else
    receipt -= MACDRV_AX_SNAPSHOT_MS;
#endif
    test_ms += 100; drain();
    CHECK(cache->revision && [cache->elements count] == 1);
    WineAXElement *held = [[cache->elements objectAtIndex:0] retain];
    CHECK([held isAccessibilityElement]);
#ifdef AX_BASELINE
    CHECK(cache->revision == second.revision);
    CHECK(cache->received_ms > receipt);
#else
    CHECK(cache->revision == first.revision);
    CHECK(cache->received_ms == receipt);
#endif
    test_ms = receipt + MACDRV_AX_CACHE_MS;
#ifdef AX_BASELINE
    CHECK([held isAccessibilityElement]); /* Queue delay extends baseline TTL. */
#else
    CHECK(![held isAccessibilityElement]); /* Exact true receipt boundary expires. */
#endif
#ifndef AX_BASELINE
    /* A pending apply expires at the original lease boundary. This is already
     * completed worker work, so a later snapshot is safe without quarantine;
     * stale main-queue blocks still cannot replace its newer serial. */
    cache->ordered_in=YES; cache->eligibility_ms=test_ms;
    cache->next_snapshot_ms=0;
    struct macdrv_ax_request third=poll();
    CHECK(third.operation==MACDRV_AX_SNAPSHOT);
    snapshot.revision=third.revision; snapshot.serial=third.serial;
    CHECK(!macdrv_ax_publish(&snapshot));
    test_ms+=MACDRV_AX_DEADLINE_MS;
    cache->eligibility_ms=test_ms;
    struct macdrv_ax_request fourth=poll();
    CHECK(fourth.operation==MACDRV_AX_SNAPSHOT && fourth.serial!=third.serial);
    CHECK(!ax_quarantined && !cache->pending_serial);
    snapshot.revision=fourth.revision; snapshot.serial=fourth.serial;
    CHECK(!macdrv_ax_publish(&snapshot));
    drain(); CHECK(cache->revision==fourth.revision && !cache->pending_serial);
#endif
    ax_unregister_window(window); [held release]; [window release]; drain();
    printf("stability checks=%u baseline=%d PASS\n", checks,
#ifdef AX_BASELINE
      1
#else
      0
#endif
    );
}
return 0;
}
