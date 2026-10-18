/* Replacement for the !DXMT_NATIVE shared-event listener implementation.
 * Opaque integer handles preserve the PE ABI. Registry lookup retains native
 * state; callers never dereference an address supplied by a stale handle. */
#include <pthread.h>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <CoreFoundation/CoreFoundation.h>
#ifndef W7SL_NEW_LISTENER
#define W7SL_NEW_LISTENER() [[MTLSharedEventListener alloc] init]
#endif
#ifndef W7SL_BEFORE_RUN
#define W7SL_BEFORE_RUN(session) ((void)0)
#endif
extern NTSTATUS NtSetEvent(void *handle, void *prev_state);
@class W7SLSession, W7SLTicket;
static pthread_mutex_t w7sl_registry_lock = PTHREAD_MUTEX_INITIALIZER;
static W7SLSession *w7sl_registry;
static uint64_t w7sl_next_id = 1;
static _Atomic unsigned w7sl_live_sessions;

@interface W7SLSession : NSObject {
@public
  pthread_mutex_t lock;
  uint64_t identity;
  BOOL closed, started;
  MTLSharedEventListener *listener;
  CFRunLoopRef runloop;
  CFRunLoopSourceRef source;
  W7SLTicket *head, *tail;
  W7SLSession *registry_next;
}
@end
@interface W7SLTicket : NSObject {
@public
  W7SLSession *session;
  W7SLTicket *next;
  void *event;
  BOOL delivered;
}
- (void)notify;
@end
@implementation W7SLSession
- (id)init {
  if ((self = [super init])) { pthread_mutex_init(&lock, NULL); atomic_fetch_add(&w7sl_live_sessions, 1); }
  return self;
}
- (void)dealloc {
  /* Only start owns runloop/source; only the registry owns an open session.
   * Queued tickets are drained by start or close before their last owner exits. */
  [listener release];
  pthread_mutex_destroy(&lock);
  atomic_fetch_sub(&w7sl_live_sessions, 1);
  [super dealloc];
}
@end
@implementation W7SLTicket
- (void)notify {
  pthread_mutex_lock(&session->lock);
  if (!session->closed && !delivered) {
    delivered = YES;
    [self retain]; /* queue ownership; released by source or close */
    if (session->tail) session->tail->next = self;
    else session->head = self;
    session->tail = self;
    if (session->source) CFRunLoopSourceSignal(session->source);
    if (session->runloop) CFRunLoopWakeUp(session->runloop);
  }
  pthread_mutex_unlock(&session->lock);
}
- (void)dealloc { [session release]; [super dealloc]; }
@end

static W7SLSession *w7sl_find(uint64_t identity) {
  W7SLSession *result = nil;
  pthread_mutex_lock(&w7sl_registry_lock);
  for (W7SLSession *s = w7sl_registry; s; s = s->registry_next)
    if (s->identity == identity) { result = [s retain]; break; }
  pthread_mutex_unlock(&w7sl_registry_lock);
  return result;
}
/* Caller holds lock and its own session reference. No NtSetEvent after close. */
static void w7sl_drain(W7SLSession *s, BOOL signal, unsigned limit) {
  while (s->head && limit--) {
    W7SLTicket *ticket = s->head;
    s->head = ticket->next; ticket->next = nil;
    if (!s->head) s->tail = nil;
    if (signal) NtSetEvent(ticket->event, NULL);
    [ticket release];
  }
}
static void w7sl_source_perform(void *context) {
  W7SLSession *s = (W7SLSession *)context;
  pthread_mutex_lock(&s->lock);
  if (s->closed) CFRunLoopStop(s->runloop);
  else {
    w7sl_drain(s, YES, 64); /* bound each source invocation */
    if (s->head) { CFRunLoopSourceSignal(s->source); CFRunLoopWakeUp(s->runloop); }
  }
  pthread_mutex_unlock(&s->lock);
}
static NTSTATUS _MTLSharedEvent_setWin32EventAtValue(void *obj) {
  struct unixcall_mtlsharedevent_setevent *p = obj;
  W7SLSession *s = w7sl_find(p->shared_event_listener);
  if (!s) return STATUS_SUCCESS;
  pthread_mutex_lock(&s->lock);
  MTLSharedEventListener *listener = s->closed ? nil : [s->listener retain];
  pthread_mutex_unlock(&s->lock);
  if (listener && p->shared_event) {
    W7SLTicket *ticket = [[W7SLTicket alloc] init];
    if (ticket) {
      ticket->session = [s retain]; ticket->event = (void *)(uintptr_t)p->event_handle;
      [(id<MTLSharedEvent>)(uintptr_t)p->shared_event notifyListener:listener atValue:p->value
        block:^(id<MTLSharedEvent> event, uint64_t value) {
          (void)event; (void)value;
          [ticket notify];
        }];
      [ticket release];
    }
  }
  [listener release]; [s release];
  return STATUS_SUCCESS;
}
static NTSTATUS _SharedEventListener_create(void *obj) {
  struct unixcall_generic_obj_ret *p = obj;
  p->ret = 0;
  W7SLSession *s = [[W7SLSession alloc] init];
  if (!s) return STATUS_SUCCESS;
  s->listener = W7SL_NEW_LISTENER();
  if (!s->listener) { [s release]; return STATUS_SUCCESS; }
  pthread_mutex_lock(&w7sl_registry_lock);
  if (w7sl_next_id) {
    s->identity = w7sl_next_id++;
    s->registry_next = w7sl_registry; w7sl_registry = s;
    p->ret = (obj_handle_t)s->identity;
  }
  pthread_mutex_unlock(&w7sl_registry_lock);
  if (!p->ret) [s release];
  return STATUS_SUCCESS;
}
static NTSTATUS _SharedEventListener_start(void *obj) {
  struct unixcall_generic_obj_noret *p = obj;
  W7SLSession *s = w7sl_find(p->handle);
  if (!s) return STATUS_SUCCESS; /* destroy-before-start or stale token */
  pthread_mutex_lock(&s->lock);
  if (s->closed || s->started) { pthread_mutex_unlock(&s->lock); [s release]; return STATUS_SUCCESS; }
  s->started = YES;
  CFRunLoopSourceContext context = {0}; context.info = s; context.perform = w7sl_source_perform;
  s->source = CFRunLoopSourceCreate(NULL, 0, &context);
  if (!s->source) { s->started = NO; pthread_mutex_unlock(&s->lock); [s release]; return STATUS_SUCCESS; }
  s->runloop = (CFRunLoopRef)CFRetain(CFRunLoopGetCurrent());
  CFRunLoopRef runloop = s->runloop;
  CFRunLoopSourceRef source = s->source;
  CFRunLoopAddSource(runloop, source, kCFRunLoopDefaultMode);
  if (s->head) CFRunLoopSourceSignal(source);
  pthread_mutex_unlock(&s->lock);
  for (;;) {
    pthread_mutex_lock(&s->lock); BOOL closed = s->closed; pthread_mutex_unlock(&s->lock);
    if (closed) break;
    /* A stop just before entry may be consumed by CF. The signalled source and
     * finite wait guarantee that close cannot strand the PE thread's join. */
    W7SL_BEFORE_RUN(s);
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.25, true);
  }
  pthread_mutex_lock(&s->lock);
  s->source = NULL; s->runloop = NULL;
  pthread_mutex_unlock(&s->lock);
  CFRunLoopRemoveSource(runloop, source, kCFRunLoopDefaultMode);
  CFRunLoopSourceInvalidate(source);
  CFRelease(source); CFRelease(runloop);
  [s release];
  return STATUS_SUCCESS;
}
static NTSTATUS _SharedEventListener_destroy(void *obj) {
  struct unixcall_generic_obj_noret *p = obj;
  MTLSharedEventListener *retired_listener = nil;
  W7SLSession *s = w7sl_find(p->handle);
  if (!s) return STATUS_SUCCESS;
  pthread_mutex_lock(&s->lock);
  if (!s->closed) {
    s->closed = YES;
    w7sl_drain(s, NO, ~0u);
    /* A registering thread owns its separate listener reference. Detach here
     * to avoid any listener/block/session retention cycle after shutdown. */
    retired_listener = s->listener; s->listener = nil;
    if (s->source) CFRunLoopSourceSignal(s->source);
    if (s->runloop) { CFRunLoopStop(s->runloop); CFRunLoopWakeUp(s->runloop); }
    pthread_mutex_lock(&w7sl_registry_lock);
    W7SLSession **cursor = &w7sl_registry;
    while (*cursor && *cursor != s) cursor = &(*cursor)->registry_next;
    if (*cursor) { *cursor = s->registry_next; s->registry_next = nil; [s release]; }
    pthread_mutex_unlock(&w7sl_registry_lock);
  }
  pthread_mutex_unlock(&s->lock);
  [retired_listener release];
  [s release];
  return STATUS_SUCCESS;
}
