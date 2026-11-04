#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <CoreFoundation/CoreFoundation.h>
#include <stdatomic.h>
#include <pthread.h>
#include <assert.h>
#include <Block.h>
#include <unistd.h>
typedef int NTSTATUS;
typedef uint64_t obj_handle_t;
#define STATUS_SUCCESS 0
struct unixcall_generic_obj_ret { obj_handle_t ret; };
struct unixcall_generic_obj_noret { obj_handle_t handle; };
struct unixcall_mtlsharedevent_setevent { obj_handle_t shared_event,event_handle,shared_event_listener; uint64_t value; };
static _Thread_local int listener_thread;
struct Probe { _Atomic unsigned signals,wrong_thread; };
NTSTATUS NtSetEvent(void *handle, void *prev) {
  (void)prev; struct Probe *p=handle;
  atomic_fetch_add(&p->signals,1);
  if(!listener_thread) atomic_fetch_add(&p->wrong_thread,1);
  return 0;
}
static _Atomic int pause_before_run, pause_notify;
static dispatch_semaphore_t paused,resume_run;
static void before_run(void *session) {
  (void)session;
  if(atomic_exchange(&pause_before_run,0)) {
    dispatch_semaphore_signal(paused);
    assert(!dispatch_semaphore_wait(resume_run,dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC)));
  }
}
#define W7SL_BEFORE_RUN(s) before_run(s)
static _Atomic unsigned live_mock_listeners;
@interface TestNativeListener : NSObject { NSMutableArray *blocks; }
- (void)remember:(MTLSharedEventNotificationBlock)block;
@end
@implementation TestNativeListener
- (id)init { if((self=[super init])) { blocks=[[NSMutableArray alloc] init]; atomic_fetch_add(&live_mock_listeners,1); } return self; }
- (void)remember:(MTLSharedEventNotificationBlock)block { void *copy=Block_copy(block); NSValue *entry=[[NSValue alloc] initWithBytes:&copy objCType:@encode(void *)]; [blocks addObject:entry]; [entry release]; }
- (void)dealloc {
  /* Model listener disposal invoking and releasing copied Metal callbacks. */
  for(NSValue *entry in blocks) { MTLSharedEventNotificationBlock block=(MTLSharedEventNotificationBlock)[entry pointerValue]; block((id<MTLSharedEvent>)self,0); Block_release(block); }
  [blocks release]; atomic_fetch_sub(&live_mock_listeners,1); [super dealloc];
}
@end
#define W7SL_NEW_LISTENER() ((MTLSharedEventListener *)[[TestNativeListener alloc] init])
#include "wave7_shared_listener.inc"
@interface TestEvent : NSObject { @public MTLSharedEventNotificationBlock notification; }
- (void)notifyListener:(MTLSharedEventListener *)listener atValue:(uint64_t)value block:(MTLSharedEventNotificationBlock)block;
- (void)fire;
@end
@implementation TestEvent
- (void)notifyListener:(MTLSharedEventListener *)listener atValue:(uint64_t)value block:(MTLSharedEventNotificationBlock)block {
 (void)listener; (void)value; assert(!notification);
 if(atomic_exchange(&pause_notify,0)) { dispatch_semaphore_signal(paused); assert(!dispatch_semaphore_wait(resume_run,dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC))); }
 [(TestNativeListener *)listener remember:block]; notification=Block_copy(block);
}
- (void)fire { if(notification) notification((id<MTLSharedEvent>)self,1); }
- (void)dealloc { if(notification) Block_release(notification); [super dealloc]; }
@end
static uint64_t create_listener(void) { struct unixcall_generic_obj_ret p={0}; _SharedEventListener_create(&p); assert(p.ret); return p.ret; }
static void close_listener(uint64_t id) { struct unixcall_generic_obj_noret p={id}; _SharedEventListener_destroy(&p); }
static TestEvent *register_event(uint64_t id,struct Probe *probe) {
 TestEvent *event=[[TestEvent alloc] init];
 struct unixcall_mtlsharedevent_setevent p={(uintptr_t)event,(uintptr_t)probe,id,1};
 _MTLSharedEvent_setWin32EventAtValue(&p); return event;
}
struct Registration { uint64_t id; struct Probe *probe; TestEvent *result; };
static void *register_thread(void *arg) { struct Registration *r=arg; r->result=register_event(r->id,r->probe); return NULL; }
struct Start { uint64_t id; dispatch_semaphore_t done; pthread_t thread; };
static void *start_thread(void *context) { @autoreleasepool {
 struct Start *s=context; listener_thread=1;
 struct unixcall_generic_obj_noret p={s->id}; _SharedEventListener_start(&p);
 dispatch_semaphore_signal(s->done);
} return NULL; }
static void start(struct Start *s,uint64_t id) { s->id=id; s->done=dispatch_semaphore_create(0); assert(!pthread_create(&s->thread,NULL,start_thread,s)); }
static void join(struct Start *s) { assert(!dispatch_semaphore_wait(s->done,dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC))); assert(!pthread_join(s->thread,NULL)); dispatch_release(s->done); }
static void await_signals(struct Probe *p,unsigned count) { for(unsigned i=0;i<2000 && atomic_load(&p->signals)<count;i++) usleep(1000); assert(atomic_load(&p->signals)==count); assert(!atomic_load(&p->wrong_thread)); }
static void await_started(uint64_t id) { for(unsigned i=0;i<2000;i++) { W7SLSession *s=w7sl_find(id); assert(s); pthread_mutex_lock(&s->lock); BOOL ready=s->runloop!=NULL; pthread_mutex_unlock(&s->lock); [s release]; if(ready)return; usleep(1000); } assert(0); }
int main(void) { @autoreleasepool {
 paused=dispatch_semaphore_create(0); resume_run=dispatch_semaphore_create(0);
 /* Early close: stale handle start is a no-op, not an infinite join. */
 uint64_t id=create_listener(); close_listener(id); struct Start a; start(&a,id); join(&a); assert(!atomic_load(&w7sl_live_sessions)); assert(!atomic_load(&live_mock_listeners));
 /* Completion before start must return immediately, queue, then signal on PE thread. */
 struct Probe p={0}; id=create_listener(); TestEvent *e=register_event(id,&p); [e fire]; [e fire];
 assert(!atomic_load(&p.signals)); start(&a,id); await_signals(&p,1); close_listener(id); join(&a); [e release]; assert(!atomic_load(&w7sl_live_sessions)); assert(!atomic_load(&live_mock_listeners));
 /* Late callbacks retain native state but never signal a closed Windows handle. */
 id=create_listener(); e=register_event(id,&p); close_listener(id); [e fire]; [e release]; assert(atomic_load(&p.signals)==1); assert(!atomic_load(&w7sl_live_sessions)); assert(!atomic_load(&live_mock_listeners));
 /* Stop issued in the precise published-runloop/before-run gap. */
 id=create_listener(); atomic_store(&pause_before_run,1); start(&a,id);
 assert(!dispatch_semaphore_wait(paused,dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC)));
 close_listener(id); dispatch_semaphore_signal(resume_run); join(&a); assert(!atomic_load(&w7sl_live_sessions)); assert(!atomic_load(&live_mock_listeners));
 /* Close while native registration owns its separate listener reference. */
 id=create_listener(); struct Probe registration_probe={0}; struct Registration registration={id,&registration_probe,nil};
 pthread_t registering; atomic_store(&pause_notify,1); assert(!pthread_create(&registering,NULL,register_thread,&registration));
 assert(!dispatch_semaphore_wait(paused,dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC))); close_listener(id);
 dispatch_semaphore_signal(resume_run); assert(!pthread_join(registering,NULL));
 [registration.result fire]; [registration.result release]; assert(!atomic_load(&registration_probe.signals)); assert(!atomic_load(&w7sl_live_sessions)); assert(!atomic_load(&live_mock_listeners));
 /* Duplicate start returns while original start remains owned until close. */
 id=create_listener(); start(&a,id); await_started(id); struct Start duplicate; start(&duplicate,id); join(&duplicate); close_listener(id); join(&a);
 /* 130 pre-start records exercise bounded source batches without dropping events. */
 struct Probe many={0}; id=create_listener(); TestEvent *events[130];
 for(unsigned i=0;i<130;i++) { events[i]=register_event(id,&many); [events[i] fire]; }
 start(&a,id); await_signals(&many,130); close_listener(id); join(&a);
 for(unsigned i=0;i<130;i++) [events[i] release]; assert(!atomic_load(&w7sl_live_sessions)); assert(!atomic_load(&live_mock_listeners));
 /* Concurrent independent callback/start/close schedules; all joins bounded. */
 for(unsigned n=0;n<300;n++) {
   struct Probe race={0}; id=create_listener(); TestEvent *items[8];
   for(unsigned i=0;i<8;i++) items[i]=register_event(id,&race);
   if(n%3==0) close_listener(id);
   start(&a,id);
   dispatch_group_t group=dispatch_group_create(); dispatch_queue_t q=dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT,0);
   for(unsigned i=0;i<8;i++) { uintptr_t pointer=(uintptr_t)items[i]; dispatch_group_async(group,q,^{ [(TestEvent *)pointer fire]; [(TestEvent *)pointer release]; }); }
   dispatch_group_async(group,q,^{ close_listener(id); });
   assert(!dispatch_group_wait(group,dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC))); dispatch_release(group);
   join(&a); assert(!atomic_load(&race.wrong_thread)); assert(!atomic_load(&w7sl_live_sessions)); assert(!atomic_load(&live_mock_listeners));
   TestEvent *stale=register_event(id,&race); assert(!stale->notification); [stale release];
 }
 dispatch_release(paused); dispatch_release(resume_run);
 puts("PASS: early-close/start, pre-start callbacks, late callbacks, duplicate starts, deterministic stop-before-run, 130-event batching, 300 concurrent shutdown schedules, PE thread affinity, full native disposal");
} return 0; }
