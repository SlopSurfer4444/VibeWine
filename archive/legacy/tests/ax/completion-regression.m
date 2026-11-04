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
#ifdef AX_PRE_REPAIR
#include "references/pre-completion/ax_native.inc"
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
 setenv("WINE_MACDRV_AX_BRIDGE","1",1);
 WineWindow *window=[WineWindow new];window->visible=YES;ax_register_window(window,(void *)(uintptr_t)0x1234);
 struct macdrv_ax_request request=poll();CHECK(request.operation==MACDRV_AX_SNAPSHOT);
 struct macdrv_ax_snapshot snapshot={0};snapshot.version=MACDRV_AX_VERSION;snapshot.size=sizeof(snapshot);
 snapshot.operation=request.operation;snapshot.hwnd=request.hwnd;snapshot.generation=request.generation;snapshot.revision=request.revision;snapshot.serial=request.serial;
 snapshot.count=1;snapshot.nodes[0].id=1;snapshot.nodes[0].token=100;snapshot.nodes[0].role=MACDRV_AX_ROLE_BUTTON;snapshot.nodes[0].flags=MACDRV_AX_ENABLED|MACDRV_AX_PRESSABLE;
 test_ms+=508;
 struct macdrv_ax_request validation=request;validation.operation=MACDRV_AX_VALIDATE;
 CHECK(!macdrv_ax_poll(&validation)&&validation.result==MACDRV_AX_STALE);
 CHECK(ax_quarantined);
 CHECK(macdrv_ax_publish(&snapshot)==-1); /* Read completed; never apply its late data. */
 struct ax_window_cache *cache=ax_find_locked(request.hwnd,request.generation);cache->eligibility_ms=test_ms;
 request=poll();
 CHECK(request.operation==MACDRV_AX_SNAPSHOT); /* Exact known-return must allow new read work. */
 CHECK(!ax_quarantined);ax_unregister_window(window);[window release];drain();
 printf("completed-read regression checks=%u PASS\n",checks);
}
return 0;
}
