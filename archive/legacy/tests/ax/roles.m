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
#include "../../source/dlls/winemac.drv/ax_native.inc"
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
    WineWindow *window = [WineWindow new]; window->visible = YES;
    ax_register_window(window, (void *)(uintptr_t)0x1234);
    struct macdrv_ax_request request = poll();
    struct macdrv_ax_snapshot snapshot = {0};
    snapshot.version = MACDRV_AX_VERSION; snapshot.size = sizeof(snapshot);
    snapshot.operation = request.operation; snapshot.hwnd = request.hwnd;
    snapshot.generation = request.generation; snapshot.revision = request.revision; snapshot.serial = request.serial;
    snapshot.count = 3;
    for (unsigned i=0; i<3; i++) {
      snapshot.nodes[i].id=i+1; snapshot.nodes[i].token=100+i;
      snapshot.nodes[i].role = i==0 ? MACDRV_AX_ROLE_GROUP : i==1 ? MACDRV_AX_ROLE_CHECKBOX : MACDRV_AX_ROLE_RADIO;
      snapshot.nodes[i].flags= MACDRV_AX_ENABLED | (i ? MACDRV_AX_PRESSABLE : 0);
      snapshot.nodes[i].parent = i ? 1 : 0;
    }
    CHECK(macdrv_ax_snapshot_valid(&snapshot));
    CHECK(!macdrv_ax_publish(&snapshot)); drain();
    struct ax_window_cache *cache=ax_find_locked(request.hwnd,request.generation);
    WineAXElement *group=[cache->elements objectAtIndex:0], *check=[cache->elements objectAtIndex:1], *radio=[cache->elements objectAtIndex:2];
    CHECK([[group accessibilityRole] isEqualToString:NSAccessibilityGroupRole]);
    CHECK([[check accessibilityRole] isEqualToString:NSAccessibilityCheckBoxRole]);
    CHECK([[radio accessibilityRole] isEqualToString:NSAccessibilityRadioButtonRole]);
    CHECK([[group accessibilityChildren] count]==2 && [check accessibilityParent]==group);
    CHECK(![group accessibilityPerformPress]);
    for (unsigned i=0; i<2; i++) {
      WineAXElement *element=i ? radio : check;
      for (unsigned state=0; state<3; state++) {
        element->ax_node.flags=MACDRV_AX_ENABLED | MACDRV_AX_PRESSABLE | (state==1?MACDRV_AX_CHECKED:state==2?MACDRV_AX_MIXED:0);
        CHECK([[element accessibilityValue] isKindOfClass:[NSNumber class]]);
        CHECK([[element accessibilityValue] intValue] == (int)state);
        CHECK([element accessibilityPerformPress]);
        CHECK(ax_actions[ax_action_count-1].token==element->ax_token);
        ax_action_count=0;
      }
      CHECK(!ax_queue_action(element,MACDRV_AX_SET_VALUE,@"no"));
      CHECK(![element isAccessibilitySelectorAllowed:@selector(setAccessibilityValue:)]);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
      CHECK(![element accessibilityIsAttributeSettable:NSAccessibilityValueAttribute]);
#pragma clang diagnostic pop
      for (unsigned deny=0; deny<3; deny++) {
        element->ax_node.flags=MACDRV_AX_PRESSABLE | (deny ? MACDRV_AX_ENABLED:0) | (deny==1?MACDRV_AX_READONLY:deny==2?MACDRV_AX_PASSWORD:0);
        CHECK(![element accessibilityPerformPress]);
        CHECK(![element isAccessibilitySelectorAllowed:@selector(accessibilityPerformPress)]);
      }
    }
    snapshot.nodes[0].flags |= MACDRV_AX_CHECKED; CHECK(!macdrv_ax_snapshot_valid(&snapshot));
    snapshot.nodes[0].flags &= ~MACDRV_AX_CHECKED;
    snapshot.nodes[1].flags |= MACDRV_AX_CHECKED | MACDRV_AX_MIXED; CHECK(!macdrv_ax_snapshot_valid(&snapshot));
    snapshot.nodes[1].flags &= ~(MACDRV_AX_CHECKED | MACDRV_AX_MIXED);
    snapshot.nodes[2].flags |= MACDRV_AX_MIXED; CHECK(macdrv_ax_snapshot_valid(&snapshot));
    snapshot.nodes[2].role=999; CHECK(!macdrv_ax_snapshot_valid(&snapshot));
    request.version=2; CHECK(macdrv_ax_poll(&request)==-1);
    check=[check retain]; radio=[radio retain];
    ax_unregister_window(window);
    CHECK(![check isAccessibilityElement] && ![radio isAccessibilityElement]);
    CHECK(![check accessibilityPerformPress] && ![radio accessibilityPerformPress]);
    CHECK([check accessibilityValue]==nil && [radio accessibilityValue]==nil);
    [check release]; [radio release]; [window release]; drain();
    printf("roles checks=%u PASS\n",checks);
}
return 0;
}
