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
struct publish_probe { struct macdrv_ax_snapshot snapshot; int result; };
static void *foreign_publish(void *value) {
    struct publish_probe *probe=value;
    probe->result=macdrv_ax_publish(&probe->snapshot);
    return NULL;
}
int main(int argc,char **argv)
{
@autoreleasepool
{
    BOOL action=argc>1&&!strcmp(argv[1],"action"), abandoned=argc>1&&!strcmp(argv[1],"abandoned"), exhausted=argc>1&&!strcmp(argv[1],"exhausted");
    setenv("WINE_MACDRV_AX_BRIDGE","1",1);
    WineWindow *window=[WineWindow new];window->visible=YES;
    ax_register_window(window,(void *)(uintptr_t)0x1234);
    struct macdrv_ax_request request=poll();CHECK(request.operation==MACDRV_AX_SNAPSHOT);
    struct macdrv_ax_snapshot snapshot=response(request);
    CHECK(!macdrv_ax_publish(&snapshot));drain();
    struct ax_window_cache *cache=ax_find_locked(request.hwnd,request.generation);
    WineAXElement *old=[[cache->elements objectAtIndex:0] retain];
    CHECK([old isAccessibilityElement]);
    if(action) CHECK([old accessibilityPerformPress]);
    else {
        /* Leave a real accepted action queued, then make the snapshot due. */
        ax_action_count=0;test_ms+=MACDRV_AX_SNAPSHOT_MS;cache->eligibility_ms=test_ms;
    }
    request=poll();CHECK(request.operation==(action?MACDRV_AX_PRESS:MACDRV_AX_SNAPSHOT));
    snapshot=response(request);
    if(!action) CHECK([old accessibilityPerformPress] && ax_action_count==1);
    test_ms+=MACDRV_AX_DEADLINE_MS+8;cache->eligibility_ms=test_ms;
    struct macdrv_ax_request validate=request;validate.operation=MACDRV_AX_VALIDATE;
    CHECK(!macdrv_ax_poll(&validate)&&validate.result==MACDRV_AX_STALE);
    CHECK(ax_quarantined&&!ax_action_count);
    CHECK(poll().operation==MACDRV_AX_NONE);
    if(action) {
        CHECK(ax_permanent_quarantine&&!ax_snapshot_expired);
        CHECK(macdrv_ax_publish(&snapshot)==-1 && ax_quarantined);
        CHECK(poll().operation==MACDRV_AX_NONE);
    } else {
        CHECK(ax_snapshot_expired&&ax_outstanding.serial==request.serial);
        CHECK(![old isAccessibilityElement] && ![old accessibilityPerformPress]);
        CHECK(!cache->revision&&!cache->received_ms);
        struct publish_probe probe={snapshot,0};pthread_t thread;
        CHECK(!pthread_create(&thread,NULL,foreign_publish,&probe));CHECK(!pthread_join(thread,NULL));
        CHECK(probe.result==-1 && ax_quarantined && ax_snapshot_expired);
        struct macdrv_ax_snapshot wrong=snapshot;wrong.serial++;
        CHECK(macdrv_ax_publish(&wrong)==-1 && ax_quarantined);
        wrong=snapshot;wrong.generation++;
        CHECK(macdrv_ax_publish(&wrong)==-1 && ax_quarantined);
        wrong=snapshot;wrong.operation=MACDRV_AX_PRESS;
        CHECK(macdrv_ax_publish(&wrong)==-1 && ax_quarantined);
        if(abandoned) {
            /* Matches ax_provider_abandoned: no completion is ever published. */
            for(unsigned i=0;i<20;i++){test_ms+=MACDRV_AX_CACHE_MS;CHECK(poll().operation==MACDRV_AX_NONE);}
            CHECK(ax_quarantined&&ax_outstanding.serial==request.serial);
        } else if(exhausted) {
            /* A later permanent cause cannot be erased by a valid old ack. */
            ax_generation_counter=UINT64_MAX;
            WineWindow *overflow=[WineWindow new];ax_register_window(overflow,(void *)(uintptr_t)0x9999);
            CHECK(ax_permanent_quarantine);
            CHECK(macdrv_ax_publish(&snapshot)==-1&&ax_quarantined);
            CHECK(poll().operation==MACDRV_AX_NONE);[overflow release];
        } else {
            CHECK(macdrv_ax_publish(&snapshot)==-1); /* Ack rejects data. */
            CHECK(!ax_quarantined&&!ax_snapshot_expired&&!ax_outstanding.operation);
            CHECK(!cache->revision&&!cache->received_ms && ![old isAccessibilityElement]);
            request=poll();CHECK(request.operation==MACDRV_AX_SNAPSHOT);
            CHECK(!ax_action_count);struct macdrv_ax_snapshot fresh=response(request);
            CHECK(!macdrv_ax_publish(&fresh));drain();
            WineAXElement *current=[cache->elements objectAtIndex:0];
            CHECK([current isAccessibilityElement]&&current!=old);
            CHECK(![old isAccessibilityElement]&&! [old accessibilityPerformPress]);
            /* Expire newer read; old exact completion cannot clear its lease. */
            test_ms+=MACDRV_AX_SNAPSHOT_MS;cache->eligibility_ms=test_ms;
            struct macdrv_ax_request newer=poll();CHECK(newer.operation==MACDRV_AX_SNAPSHOT);
            test_ms+=MACDRV_AX_DEADLINE_MS;cache->eligibility_ms=test_ms;
            CHECK(poll().operation==MACDRV_AX_NONE&&ax_quarantined);
            CHECK(macdrv_ax_publish(&snapshot)==-1&&ax_outstanding.serial==newer.serial&&ax_quarantined);
            struct macdrv_ax_snapshot last=response(newer);
            /* Completed destroyed-window read can release work, never publish its tree. */
            ax_unregister_window(window);
            CHECK(macdrv_ax_publish(&last)==-1&&!ax_quarantined&&!ax_outstanding.operation);
        }
    }
    ax_unregister_window(window);[old release];[window release];drain();
    printf("completion mode=%s checks=%u PASS\n",argc>1?argv[1]:"read",checks);
}
return 0;
}
