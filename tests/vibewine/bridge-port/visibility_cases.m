/* Actual production dispatch/cache; AppKit fakes assert main and no cache lock. */
static struct W7VSRequest request(CAMetalLayer *layer) {
 struct W7VSRequest r={.size=sizeof(r),.version=W7VS_VERSION,.generation=7,.layer=(uintptr_t)layer};
 assert(w7vs_dispatch(&r)==W7VS_REPLY); return r;
}
int main(void) { @autoreleasepool {
 async_blocks=[NSMutableArray new]; fake_screens=@[];
 assert(!w7ds_skip_occluded_enabled);
 CAMetalLayer *layer=[CAMetalLayer new]; NSView *view=[NSView new]; NSWindow *window=[NSWindow new];
 execute_on_main(^{ layer.delegate=view;view->fakeWindow=window;window->fakeVisible=YES;
   window->fakeOnSpace=YES;window->fakeWindowNumber=123;window->fakeOcclusion=NSWindowOcclusionStateVisible; });
 unsigned reads=atomic_load(&appkit_reads),queued=atomic_load(&async_count);
 struct W7VSRequest a=request(layer);assert(a.status==W7VS_UNKNOWN && !a.snapshot.sequence);
 for(unsigned i=0;i<100;i++)assert(request(layer).status==W7VS_UNKNOWN);
 assert(atomic_load(&async_count)==queued+1 && atomic_load(&appkit_reads)==reads);
 assert(!w7ds_should_skip_drawable(layer)); /* Diagnostic never enables skip. */
 drain_async();a=request(layer);
 assert(a.status==W7VS_OK && a.snapshot.layer_identity && a.snapshot.sequence==1 && a.snapshot.window_id==123);
 assert((a.snapshot.flags&(W7VS_HAS_WINDOW|W7VS_ON_ACTIVE_SPACE|W7VS_WINDOW_VISIBLE|W7VS_OCCLUSION_VISIBLE))==
        (W7VS_HAS_WINDOW|W7VS_ON_ACTIVE_SPACE|W7VS_WINDOW_VISIBLE|W7VS_OCCLUSION_VISIBLE));
 uint64_t identity=a.snapshot.layer_identity;
 reads=atomic_load(&appkit_reads);queued=atomic_load(&async_count);
 for(unsigned i=0;i<100;i++)assert(request(layer).status==W7VS_OK);
 assert(atomic_load(&async_count)==queued && atomic_load(&appkit_reads)==reads);
 /* A missed notification: visible remains true but active Space/occlusion change. */
 execute_on_main(^{window->fakeOnSpace=NO;window->fakeOcclusion=0;});
 atomic_fetch_add(&policy_now,W7VS_REFRESH_NS+1);
 a=request(layer);assert(a.status==W7VS_OK && a.snapshot.sequence==1);
 assert(atomic_load(&async_count)==queued+1);drain_async();a=request(layer);
 assert(a.status==W7VS_OK && a.snapshot.sequence==2 && (a.snapshot.flags&W7VS_WINDOW_VISIBLE) &&
       !(a.snapshot.flags&(W7VS_ON_ACTIVE_SPACE|W7VS_OCCLUSION_VISIBLE)));
 /* Main does not drain: preserve stale observation; never block the reader. */
 atomic_fetch_add(&policy_now,W7VS_EXPIRE_NS+1);queued=atomic_load(&async_count);reads=atomic_load(&appkit_reads);
 for(unsigned i=0;i<100;i++) {a=request(layer);assert(a.status==W7VS_STALE && a.snapshot.age_ns>W7VS_EXPIRE_NS);}
 assert(atomic_load(&async_count)==queued+1 && atomic_load(&appkit_reads)==reads);drain_async();
 /* No host is explicit fresh missing-host evidence, not visible=false proof. */
 execute_on_main(^{view->fakeWindow=nil;});atomic_fetch_add(&policy_now,W7VS_REFRESH_NS+1);
 request(layer);drain_async();a=request(layer);assert(a.status==W7VS_OK && !(a.snapshot.flags&W7VS_HAS_WINDOW));
 /* Clock regression is stale, not fresh; malformed requests do not queue. */
 uint64_t before=atomic_load(&policy_now);atomic_store(&policy_now,1);assert(request(layer).status==W7VS_STALE);
 drain_async();atomic_store(&policy_now,before);request(layer);drain_async();
 struct W7VSRequest bad={.size=sizeof(bad),.version=W7VS_VERSION,.generation=0,.layer=(uintptr_t)layer};
 queued=atomic_load(&async_count);assert(w7vs_dispatch(&bad)==W7VS_REPLY && bad.status==W7VS_BAD_REQUEST);
 bad.generation=7;bad.reserved=1;assert(w7vs_dispatch(&bad)==W7VS_REPLY && bad.status==W7VS_BAD_REQUEST);
 bad.size--;assert(!w7vs_dispatch(&bad));assert(atomic_load(&async_count)==queued);
 /* New associated-state lifetime at same address receives a new identity. */
 void *storage=calloc(1,class_getInstanceSize([CAMetalLayer class]));
 CAMetalLayer *first=objc_constructInstance([CAMetalLayer class],storage);request(first);drain_async();
 uint64_t first_id=request(first).snapshot.layer_identity;objc_destructInstance(first);
 CAMetalLayer *second=objc_constructInstance([CAMetalLayer class],storage);assert(second==first);
 assert(request(second).status==W7VS_UNKNOWN);drain_async();
 assert(request(second).snapshot.layer_identity!=first_id && first_id!=identity);
 objc_destructInstance(second);free(storage);
 assert(!w7ds_skip_occluded_enabled && !w7ds_should_skip_drawable(layer));
 [layer release];[view release];[window release];
 puts("PASS: production visibility transport async main ownership, default/skip independence, cadence, Space evidence, stale/unknown and lifetime identity");
 }return 0;}
