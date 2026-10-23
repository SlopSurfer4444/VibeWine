/* Compiled with the actual display cache, guard, diagnostics and native thunk.
 * AppKit objects are Foundation fakes whose getters assert main ownership. */
static bool next_nil;
static unsigned native_calls;
@implementation W7TestMetalLayer (AcquisitionFixture)
- (id)nextDrawable { ++native_calls; return next_nil ? nil : self; }
@end
struct unixcall_generic_obj_obj_ret { obj_handle_t handle, ret; };
#include "wave7_acquire_diag.inc"
/* PRODUCTION_THUNK */

static uintptr_t acquire(CAMetalLayer *layer) {
    struct unixcall_generic_obj_obj_ret p={(uintptr_t)layer,123};
    assert(_MetalLayer_nextDrawable(&p)==0);
    return p.ret;
}
static void configure(CAMetalLayer *layer,NSView *view,NSWindow *window,BOOL visible) {
    execute_on_main(^{
        layer.delegate=view; view->fakeWindow=window;
        window->fakeVisible=visible; window->fakeOcclusion=visible?NSWindowOcclusionStateVisible:0;
        w7ds_prime_layer_main(layer);
    });
}
static void notify(NSWindow *window,NSString *name,BOOL visible,BOOL mini) {
    execute_on_main(^{
        window->fakeVisible=visible; window->fakeMini=mini;
        window->fakeOcclusion=visible&&!mini?NSWindowOcclusionStateVisible:0;
        [[NSNotificationCenter defaultCenter] postNotificationName:name object:window];
    });
}
int main(void) { @autoreleasepool {
    async_blocks=[NSMutableArray new]; fake_screens=@[];
    static struct W7ADFile sink; w7ad_file=&sink; w7ad_pid=getpid();
    w7ds_skip_occluded_enabled=YES;
    CAMetalLayer *layer=[CAMetalLayer new]; NSView *view=[NSView new]; NSWindow *window=[NSWindow new];
    configure(layer,view,window,NO);
    w7ds_skip_occluded_enabled=NO;
    assert(acquire(layer)); assert(acquire(layer)); assert(native_calls==2 && !sink.header.skipped_occluded);

    w7ds_skip_occluded_enabled=YES; configure(layer,view,window,NO);
    /* Default-off successes do not consume enabled-mode bootstrap. Nil attempts
     * likewise cannot prevent a later first content frame. */
    next_nil=true; assert(!acquire(layer)); assert(!acquire(layer)); next_nil=false;
    assert(acquire(layer)); unsigned calls=native_calls;
    unsigned reads=atomic_load(&appkit_reads), requests=atomic_load(&async_count);
    uint64_t started=sink.header.started, finished=sink.header.finished;
    for(unsigned i=0;i<100;i++) assert(!acquire(layer));
    assert(native_calls==calls && sink.header.skipped_occluded==100);
    assert(sink.header.started==started && sink.header.finished==finished);
    assert(atomic_load(&appkit_reads)==reads && atomic_load(&async_count)==requests);

    /* Stable occlusion refreshes asynchronously, without forced native probes. */
    atomic_fetch_add(&policy_now,W7DS_VISIBILITY_REFRESH_NS+1);
    assert(!acquire(layer)); assert(atomic_load(&async_count)==requests+1);
    assert(!acquire(layer)); assert(atomic_load(&async_count)==requests+1);
    drain_async(); assert(!acquire(layer)); assert(native_calls==calls);
    /* A window becomes visible without delivering its notification: the
     * queued main refresh, not a fabricated native probe, repairs admission. */
    execute_on_main(^{ window->fakeVisible=YES; window->fakeOcclusion=NSWindowOcclusionStateVisible; });
    atomic_fetch_add(&policy_now,W7DS_VISIBILITY_REFRESH_NS+1);
    assert(!acquire(layer)); drain_async(); assert(acquire(layer));
    notify(window,NSWindowDidChangeOcclusionStateNotification,NO,NO);
    calls=native_calls;
    /* A missed/unserviced refresh cannot suppress native recovery forever. */
    atomic_fetch_add(&policy_now,W7DS_VISIBILITY_EXPIRE_NS+1);
    assert(acquire(layer)); assert(native_calls==calls+1);
    drain_async(); assert(!acquire(layer));
    uint64_t saved_now=atomic_load(&policy_now); atomic_store(&policy_now,0);
    requests=atomic_load(&async_count);
    for(unsigned i=0;i<100;i++) assert(acquire(layer));
    assert(atomic_load(&async_count)==requests);
    atomic_store(&policy_now,saved_now); assert(!acquire(layer));

    /* Notification recovery, minimizing, app hiding/unhiding and closing. */
    notify(window,NSWindowDidChangeOcclusionStateNotification,YES,NO); assert(acquire(layer));
    notify(window,NSWindowDidMiniaturizeNotification,YES,YES); assert(!acquire(layer));
    notify(window,NSWindowDidDeminiaturizeNotification,YES,NO); assert(acquire(layer));
    notify(window,NSApplicationDidHideNotification,NO,NO); assert(!acquire(layer));
    notify(window,NSApplicationDidUnhideNotification,YES,NO); assert(acquire(layer));
    NSWindow *new_host=[NSWindow new];
    configure(layer,view,new_host,NO); assert(acquire(layer)); assert(!acquire(layer));
    /* Hidden replacement views require the Wine event hook; unknown admission
     * lets the actual native call run rather than waiting for visible content. */
    execute_on_main(^{ view->fakeHidden=YES; w7ds_refresh_layer_main(layer); });
    assert(acquire(layer)); assert(acquire(layer));
    execute_on_main(^{ view->fakeHidden=NO; w7ds_refresh_layer_main(layer); });
    assert(!acquire(layer));
    configure(layer,view,window,YES); assert(acquire(layer));
    execute_on_main(^{ [[NSNotificationCenter defaultCenter] postNotificationName:NSWindowWillCloseNotification object:window]; });
    assert(!acquire(layer));

    /* No hosting window remains unknown/fail-open, including after bootstrap. */
    execute_on_main(^{ view->fakeWindow=nil; w7ds_refresh_layer_main(layer); });
    assert(acquire(layer)); assert(acquire(layer));
    CAMetalLayer *cold=[CAMetalLayer new]; assert(acquire(cold)); assert(acquire(cold)); [cold release];

    /* A worker must be able to skip with main blocked on the renderer lock. */
    configure(layer,view,window,NO);
    assert(acquire(layer)); /* renewed host bootstrap */
    pthread_mutex_lock(&device_lock); main_blocked=dispatch_semaphore_create(0);
    pthread_t blocked; assert(!pthread_create(&blocked,NULL,blocked_main,NULL));
    assert(!dispatch_semaphore_wait(main_blocked,dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC)));
    alarm(2); assert(!acquire(layer)); alarm(0);
    pthread_mutex_unlock(&device_lock); pthread_join(blocked,NULL); dispatch_release(main_blocked);

    /* Runtime-supported destruction/reconstruction at precisely the same address
     * proves associated bootstrap/cache state is tied to object lifetime. */
    void *storage=calloc(1,class_getInstanceSize([CAMetalLayer class])); assert(storage);
    CAMetalLayer *first=objc_constructInstance([CAMetalLayer class],storage); assert(first);
    NSWindow *other=[NSWindow new]; configure(first,view,other,NO);
    assert(acquire(first)); assert(!acquire(first));
    objc_destructInstance(first);
    CAMetalLayer *reused=objc_constructInstance([CAMetalLayer class],storage); assert(reused==first);
    configure(reused,view,other,NO); assert(acquire(reused)); assert(!acquire(reused));
    objc_destructInstance(reused); free(storage);
    drain_async(); [other release]; [new_host release]; [layer release]; [view release]; [window release];
    w7ad_file=NULL;
    puts("PASS: actual cache/guard/thunk default-off, bootstrap, nil retry, skip, recovery, unknown, main inversion and exact pointer reuse");
} return 0; }
