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
static unsigned int epoch(unsigned int ui_hwnd, unsigned int *ui_requests)
{
    unsigned int requests = 0;
    /* First poll schedules the asynchronous state sweep. Drain it before the
     * remaining worker polls; newly shown UI need not have been ordered at create. */
    struct macdrv_ax_request request = poll();
    drain();
    for (;;)
    {
        if (request.operation)
        {
            CHECK(request.operation == MACDRV_AX_SNAPSHOT);
            requests++;
            if (request.hwnd == ui_hwnd) (*ui_requests)++;
            complete(request, request.hwnd == ui_hwnd);
        }
        request = poll();
        if (!request.operation) break;
        CHECK(requests < MACDRV_AX_MAX_WINDOWS);
    }
    return requests;
}
#ifndef AX_BASELINE
struct worker_probe
{
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    BOOL done, failed;
};
static void *worker_poll(void *arg)
{
    struct worker_probe *probe = arg;
    BOOL failed = NO;
    for (unsigned int i = 0; i < 100; i++)
    {
        struct macdrv_ax_request request = {0};
        request.version = MACDRV_AX_VERSION; request.size = sizeof(request);
        if (macdrv_ax_poll(&request) || request.operation) failed = YES;
    }
    pthread_mutex_lock(&probe->mutex);
    probe->failed = failed; probe->done = YES;
    pthread_cond_signal(&probe->condition);
    pthread_mutex_unlock(&probe->mutex);
    return NULL;
}
#endif
int main(void)
{
@autoreleasepool
{
    setenv("WINE_MACDRV_AX_BRIDGE", "1", 1);
    WineWindow *fixture[3], *explorer[7];
    unsigned int requests = 0, ui_requests = 0;
    for (unsigned int i = 0; i < 3; i++)
    { fixture[i] = [WineWindow new]; ax_register_window(fixture[i], (void *)(uintptr_t)(0x1000 + i)); }
    for (unsigned int i = 0; i < 7; i++)
    { explorer[i] = [WineWindow new]; ax_register_window(explorer[i], (void *)(uintptr_t)(0x2000 + i)); }
    /* Same population as live fixture + explorer: one real provider created
     * before show; two hidden IMEs and seven clipboard/tray/IME/DDE helpers. */
    fixture[0]->visible = YES;
    for (unsigned int cycle = 0; cycle < 8; cycle++)
    { requests += epoch(0x1000, &ui_requests); test_ms += MACDRV_AX_SNAPSHOT_MS; }
#ifdef AX_BASELINE
    CHECK(requests == 80 && ui_requests == 8);
#else
    CHECK(requests == 8 && ui_requests == 8);
#endif
    struct ax_window_cache *cache = ax_find_locked(0x1000, 1);
    CHECK(cache && [cache->elements count] == 5);
    WineAXElement *held = [[cache->elements objectAtIndex:0] retain];
    CHECK([held isAccessibilityElement]);
    fixture[0]->visible = NO; fixture[0]->minimized = YES;
    unsigned int prior_ui = ui_requests;
    epoch(0x1000, &ui_requests);
    CHECK(ui_requests == prior_ui + 1 && [cache->elements objectAtIndex:0] == held);
    test_ms += MACDRV_AX_SNAPSHOT_MS;
    fixture[0]->minimized = NO; fixture[0]->applicationHidden = YES; fixture[0]->savedVisible = YES;
    prior_ui = ui_requests;
    epoch(0x1000, &ui_requests);
    CHECK(ui_requests == prior_ui + 1 && [held isAccessibilityElement]);
#ifndef AX_BASELINE
    /* Ordered out helpers are skipped, explicit view readers still refresh a
     * hidden node, and that demand expires without extending provider cache TTL. */
    WineContentView *view = [WineContentView new];
    view->testWindow = fixture[1]; fixture[1]->view = view;
    CHECK(!ax_children_for_view(view));
    test_ms += MACDRV_AX_SNAPSHOT_MS;
    unsigned int ignored = 0;
    CHECK(epoch(0x1000, &ignored) == 2);
    test_ms += MACDRV_AX_CACHE_MS;
    CHECK(epoch(0x1000, &ignored) == 1);
    fixture[1]->view = nil; view->testWindow = nil; [view release];
    /* A second real window shown long after creation becomes eligible. An empty
     * first provider tree must not permanently suppress later valid nodes. */
    fixture[2]->visible = YES;
    test_ms += MACDRV_AX_SNAPSHOT_MS;
    struct macdrv_ax_request shown = poll(); drain();
    BOOL saw_shown = NO;
    for (unsigned int i = 0; i < 3 && shown.operation; i++)
    {
        if (shown.hwnd == 0x1002) saw_shown = YES;
        complete(shown, shown.hwnd == 0x1000);
        shown = poll();
    }
    CHECK(saw_shown);
    struct ax_window_cache *later = ax_find_locked(0x1002, 3);
    CHECK(later && ![later->elements count]);
    test_ms += MACDRV_AX_SNAPSHOT_MS;
    shown = poll(); drain(); saw_shown = NO;
    for (unsigned int i = 0; i < 3 && shown.operation; i++)
    {
        if (shown.hwnd == 0x1002) saw_shown = YES;
        complete(shown, YES);
        shown = poll();
    }
    CHECK(saw_shown && [later->elements count] == 5);
    fixture[2]->visible = NO;
    test_ms += MACDRV_AX_SNAPSHOT_MS;
    epoch(0x1000, &ignored); /* Asynchronous ordering sample observes order-out. */
    /* Main queue cannot be serviced: poll only returns, enqueues a single sweep,
     * and stops renewing snapshots once observed ordering state is too old. */
    test_ms += MACDRV_AX_CACHE_MS;
    unsigned int reads_before = state_reads;
    struct worker_probe probe = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, NO, NO};
    pthread_t worker;
    CHECK(!pthread_create(&worker, NULL, worker_poll, &probe));
    struct timespec bounded;
    clock_gettime(CLOCK_REALTIME, &bounded); bounded.tv_sec++;
    pthread_mutex_lock(&probe.mutex);
    while (!probe.done)
        if (pthread_cond_timedwait(&probe.condition, &probe.mutex, &bounded)) break;
    BOOL worker_returned = probe.done && !probe.failed;
    pthread_mutex_unlock(&probe.mutex);
    CHECK(worker_returned); /* Failure exits within one second if polling waits on AppKit. */
    CHECK(!pthread_join(worker, NULL));
    pthread_mutex_destroy(&probe.mutex); pthread_cond_destroy(&probe.condition);
    CHECK(state_reads == reads_before && ax_eligibility_pending);
    CHECK(![held isAccessibilityElement]); /* Provider data independently expires. */
    drain();
    CHECK(!ax_eligibility_pending);
    struct macdrv_ax_request request = poll();
    CHECK(request.operation == MACDRV_AX_SNAPSHOT && request.hwnd == 0x1000);
    complete(request, YES);
    /* Eligibility sampling neither releases nor retries an uncertain worker. */
    test_ms += MACDRV_AX_SNAPSHOT_MS;
    request = poll(); drain();
    CHECK(request.operation == MACDRV_AX_SNAPSHOT);
    test_ms += MACDRV_AX_DEADLINE_MS;
    CHECK(poll().operation == MACDRV_AX_NONE && ax_quarantined);
    CHECK(macdrv_ax_publish(&(struct macdrv_ax_snapshot){0}) == -1);
    drain();
    for (unsigned int i = 0; i < 20; i++) CHECK(poll().operation == MACDRV_AX_NONE);
#endif
    [held release];
    for (unsigned int i = 0; i < 3; i++) { ax_unregister_window(fixture[i]); [fixture[i] release]; }
    for (unsigned int i = 0; i < 7; i++) { ax_unregister_window(explorer[i]); [explorer[i] release]; }
    drain();
    printf("%s scheduling: %u checks; steady epochs requests=%u; steady UI requests=8; visible/minimized/app-hidden UI requests=%u; UI nodes=5\n",
#ifdef AX_BASELINE
           "baseline",
#else
           "candidate",
#endif
           checks, requests, ui_requests);
}
    return 0;
}
