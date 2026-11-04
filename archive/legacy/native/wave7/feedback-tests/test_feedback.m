#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <assert.h>
#include <Block.h>
#include "wave7_feedback.inc"
@interface TestDrawable : NSObject { @public MTLDrawablePresentedHandler handler; double time; }
- (void)addPresentedHandler:(MTLDrawablePresentedHandler)block;
- (double)presentedTime;
- (void)fire;
@end
@implementation TestDrawable
- (void)addPresentedHandler:(MTLDrawablePresentedHandler)block { handler=Block_copy(block); }
- (double)presentedTime { return time; }
- (void)fire { handler((id<MTLDrawable>)self); }
- (void)dealloc { if(handler) Block_release(handler); [super dealloc]; }
@end
static struct W7FBRequest req(unsigned op, uint64_t session) {
  struct W7FBRequest r = {0}; r.size=sizeof(r); r.version=W7FB_VERSION;
  r.op=op; r.session=session; return r;
}
static uint64_t open_session(void) {
  struct W7FBRequest r=req(W7FB_OPEN,0); r.record.generation=10; r.record.swapchain_serial=20;
  assert(w7fb_dispatch(&r)==W7FB_REPLY && r.status==W7FB_OK); return r.session;
}
static void close_session(uint64_t s) {
  struct W7FBRequest r=req(W7FB_CLOSE,s); assert(w7fb_dispatch(&r)==W7FB_REPLY && r.status==W7FB_OK);
}
static struct W7FBRequest register_drawable(uint64_t s, TestDrawable *d, uint64_t frame) {
  struct W7FBRequest r=req(W7FB_REGISTER,s); r.drawable=(uintptr_t)d;
  r.record.generation=10; r.record.swapchain_serial=20; r.record.frame_id=frame;
  r.record.config_revision=30; r.record.drawable_width=2560; r.record.drawable_height=1440;
  assert(w7fb_dispatch(&r)==W7FB_REPLY); return r;
}
int main(void) { @autoreleasepool {
  struct W7FBRequest r=req(W7FB_QUERY,0);
  assert(w7fb_dispatch(&r)==W7FB_REPLY && r.status==W7FB_OK);
  r.version=99; assert(w7fb_dispatch(&r)==0);
  r=req(W7FB_CAPTURE_STATUS,0); assert(w7fb_dispatch(&r)==W7FB_REPLY && r.status==W7FB_UNSUPPORTED);
  const char *mock=getenv("W7FB_CAPTURE_MOCK");
  if (mock) {
    void *lib=dlopen(mock,RTLD_NOW|RTLD_GLOBAL); assert(lib);
    void (*set_complete)(int)=(void (*)(int))dlsym(lib,"wave7_test_set_capture_complete"); assert(set_complete);
    r=req(W7FB_CAPTURE_STATUS,0); w7fb_dispatch(&r); assert(r.status==W7FB_PENDING);
    set_complete(1); w7fb_dispatch(&r); assert(r.status==W7FB_OK);
    set_complete(-1); w7fb_dispatch(&r); assert(r.status==W7FB_PENDING);
  }
  uint64_t s=open_session();
  r=req(W7FB_REGISTER,s); r.record.generation=9; r.record.swapchain_serial=20;
  w7fb_dispatch(&r); assert(r.status==W7FB_STALE);
  NSObject *unsupported=[[NSObject alloc] init];
  r=req(W7FB_REGISTER,s); r.record.generation=10; r.record.swapchain_serial=20; r.drawable=(uintptr_t)unsupported;
  w7fb_dispatch(&r); assert(r.status==W7FB_UNSUPPORTED); [unsupported release];
  TestDrawable *d=[[TestDrawable alloc] init]; d->time=123.125;
  r=register_drawable(s,d,100); assert(r.status==W7FB_OK && r.stats.pending==1);
  [d fire]; [d fire]; [d release]; /* duplicate callback suppressed */
  r=req(W7FB_POLL,s); w7fb_dispatch(&r);
  assert(r.status==W7FB_OK && r.record.frame_id==100 && r.record.generation==10);
  assert(r.record.swapchain_serial==20 && r.record.config_revision==30);
  assert(r.record.drawable_width==2560 && r.record.drawable_height==1440);
  assert(r.record.presented_seconds==123.125 && r.record.clock_domain==W7FB_CLOCK_CA_HOST_SECONDS);
  assert(r.stats.completed==1 && r.stats.pending==0);
  r=req(W7FB_POLL,s); w7fb_dispatch(&r); assert(r.status==W7FB_EMPTY);
  d=[[TestDrawable alloc] init]; r=register_drawable(s,d,101); [d release];
  r=req(W7FB_STATUS,s); w7fb_dispatch(&r); assert(r.stats.abandoned==1 && r.stats.pending==0);
  for(unsigned i=0;i<65;i++) { d=[[TestDrawable alloc] init]; register_drawable(s,d,200+i); [d fire]; [d release]; }
  r=req(W7FB_STATUS,s); w7fb_dispatch(&r); assert(r.stats.queued==64 && r.stats.overflow==1);
  TestDrawable *pending[65];
  for(unsigned i=0;i<65;i++) { pending[i]=[[TestDrawable alloc] init]; r=register_drawable(s,pending[i],300+i); assert(r.status==(i<64?W7FB_OK:W7FB_LIMIT)); }
  assert(r.stats.pending==64);
  close_session(s); assert(w7fb_live_sessions==1);
  for(unsigned i=0;i<65;i++) { if(i<64) [pending[i] fire]; [pending[i] release]; }
  assert(w7fb_live_sessions==0);
  r=req(W7FB_POLL,s); w7fb_dispatch(&r); assert(r.status==W7FB_NO_SESSION);
  uint64_t concurrent=open_session();
  TestDrawable *parallel[32];
  for(unsigned i=0;i<32;i++) { parallel[i]=[[TestDrawable alloc] init]; register_drawable(concurrent,parallel[i],400+i); }
  dispatch_group_t group=dispatch_group_create();
  dispatch_queue_t queue=dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT,0);
  dispatch_group_async(group,queue,^{ close_session(concurrent); });
  for(unsigned i=0;i<32;i++) { uintptr_t item=(uintptr_t)parallel[i]; dispatch_group_async(group,queue,^{ [(TestDrawable *)item fire]; [(TestDrawable *)item release]; }); }
  dispatch_group_wait(group,DISPATCH_TIME_FOREVER); dispatch_release(group);
  assert(w7fb_live_sessions==0);
  uint64_t closed_ids[16]; TestDrawable *retained[16];
  for(unsigned i=0;i<16;i++) { closed_ids[i]=open_session(); retained[i]=[[TestDrawable alloc] init]; register_drawable(closed_ids[i],retained[i],500+i); close_session(closed_ids[i]); }
  r=req(W7FB_OPEN,0); r.record.generation=10; r.record.swapchain_serial=20; w7fb_dispatch(&r); assert(r.status==W7FB_LIMIT);
  for(unsigned i=0;i<16;i++) [retained[i] release];
  assert(w7fb_live_sessions==0);
  uint64_t ids[16]; for(unsigned i=0;i<16;i++) ids[i]=open_session();
  r=req(W7FB_OPEN,0); r.record.generation=10; r.record.swapchain_serial=20; w7fb_dispatch(&r); assert(r.status==W7FB_LIMIT);
  for(unsigned i=0;i<16;i++) close_session(ids[i]);
  assert(w7fb_live_sessions==0);
  puts("PASS: optional ABI, identity, clock, duplicate, abandonment, bounded ring/pending/sessions, close-before-callback");
} return 0; }
