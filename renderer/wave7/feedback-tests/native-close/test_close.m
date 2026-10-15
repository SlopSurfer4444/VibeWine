#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <assert.h>
#include <Block.h>
#include "wave7_feedback.inc"
#ifndef EXPECT_CLOSE_QUEUED
#define EXPECT_CLOSE_QUEUED 1
#endif
@interface TestDrawable : NSObject { @public MTLDrawablePresentedHandler handler; }
- (void)addPresentedHandler:(MTLDrawablePresentedHandler)block;
- (double)presentedTime;
- (void)fire;
@end
@implementation TestDrawable
- (void)addPresentedHandler:(MTLDrawablePresentedHandler)block { handler=Block_copy(block); }
- (double)presentedTime { return 123.125; }
- (void)fire { assert(handler); handler((id<MTLDrawable>)self); }
- (void)dealloc { if(handler) Block_release(handler); [super dealloc]; }
@end
static struct W7FBRequest call(unsigned op,uint64_t session) {
 struct W7FBRequest r={0};r.size=sizeof(r);r.version=W7FB_VERSION;r.op=op;r.session=session;
 assert(w7fb_dispatch(&r)==W7FB_REPLY);return r;
}
static uint64_t open_session(void) {
 struct W7FBRequest r={0};r.size=sizeof(r);r.version=W7FB_VERSION;r.op=W7FB_OPEN;
 r.record.generation=10;r.record.swapchain_serial=20;
 assert(w7fb_dispatch(&r)==W7FB_REPLY&&r.status==W7FB_OK);return r.session;
}
static TestDrawable *register_drawable(uint64_t s,uint64_t frame) {
 TestDrawable *d=[[TestDrawable alloc]init];struct W7FBRequest r={0};
 r.size=sizeof(r);r.version=W7FB_VERSION;r.op=W7FB_REGISTER;r.session=s;r.drawable=(uintptr_t)d;
 r.record.generation=10;r.record.swapchain_serial=20;r.record.frame_id=frame;
 assert(w7fb_dispatch(&r)==W7FB_REPLY&&r.status==W7FB_OK);return d;
}
int main(void) {@autoreleasepool {
 struct W7FBRequest capability=call(W7FB_QUERY,0);assert(capability.status==W7FB_OK);
#if EXPECT_CLOSE_QUEUED
 assert(capability.capabilities==(W7FB_CAP_PRESENTED_HANDLER | W7FB_CAP_CLOSE_QUEUED_DISCARD));
#else
 assert(capability.capabilities==W7FB_CAP_PRESENTED_HANDLER);
#endif
 // Deterministic order reproduces callback queuing between empty POLL and CLOSE.
 uint64_t s=open_session();TestDrawable *d=register_drawable(s,1);
 struct W7FBRequest r=call(W7FB_POLL,s);assert(r.status==W7FB_EMPTY&&r.stats.queued==0);
 dispatch_semaphore_t fired=dispatch_semaphore_create(0);
 uintptr_t callback_item=(uintptr_t)d;
 dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT,0),^{[(TestDrawable *)callback_item fire];dispatch_semaphore_signal(fired);});
 assert(dispatch_semaphore_wait(fired,dispatch_time(DISPATCH_TIME_NOW,5*NSEC_PER_SEC))==0);dispatch_release(fired);
 r=call(W7FB_STATUS,s);assert(r.stats.queued==1&&r.stats.pending==1&&r.stats.completed==1);
 W7FBSession *session=w7fb_find(s);assert(session);
 r=call(W7FB_CLOSE,s);assert(r.status==W7FB_OK&&r.stats.queued==EXPECT_CLOSE_QUEUED);
 assert(r.stats.completed==1&&r.stats.pending==1&&r.stats.overflow==0);
 // pending counts live copied blocks even after their callbacks fired.
 pthread_mutex_lock(&session->lock);assert(session->closed&&session->count==0);pthread_mutex_unlock(&session->lock);
 printf("empty-POLL/callback/CLOSE queued=%llu pending=%llu completed=%llu\n",r.stats.queued,r.stats.pending,r.stats.completed);
 [d fire]; // duplicate callback after CLOSE remains safe and does not enqueue.
 [d release];assert(w7fb_live_sessions==1);[session release];assert(w7fb_live_sessions==0);
 r=call(W7FB_STATUS,s);assert(r.status==W7FB_NO_SESSION);
 // Unfired pending block is separate: CLOSE pending 1, queued 0, completed 0.
 s=open_session();d=register_drawable(s,2);session=w7fb_find(s);
 r=call(W7FB_CLOSE,s);assert(r.stats.queued==0&&r.stats.pending==1&&r.stats.completed==0);
 [d fire];pthread_mutex_lock(&session->lock);assert(session->count==0&&session->counters.completed==1&&session->counters.pending==1);pthread_mutex_unlock(&session->lock);
 [d release];[session release];assert(w7fb_live_sessions==0);
 // Unfired cancellation still releases session without a worker.
 s=open_session();d=register_drawable(s,3);r=call(W7FB_CLOSE,s);assert(r.stats.queued==0&&r.stats.pending==1);[d release];assert(w7fb_live_sessions==0);
 // Overflow is a distinct loss count; CLOSE occupancy cannot include it.
 s=open_session();for(unsigned i=0;i<65;i++){d=register_drawable(s,100+i);[d fire];[d release];}
 r=call(W7FB_STATUS,s);assert(r.stats.queued==64&&r.stats.overflow==1&&r.stats.pending==0&&r.stats.completed==65);
 r=call(W7FB_CLOSE,s);assert(r.stats.queued==(EXPECT_CLOSE_QUEUED?64:0)&&r.stats.overflow==1&&r.stats.pending==0);assert(w7fb_live_sessions==0);
 puts("PASS: actual production-inc close accounting, block lifetime, post-close callback and separate overflow");
}return 0;}
