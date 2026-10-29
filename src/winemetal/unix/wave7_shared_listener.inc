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
#ifndef W7SL_NEW_TICKET
#define W7SL_NEW_TICKET() [[W7SLTicket alloc] init]
#endif
#ifndef W7SL_NEW_SOURCE
#define W7SL_NEW_SOURCE(context) CFRunLoopSourceCreate(NULL, 0, context)
#endif
extern NTSTATUS NtSetEvent(void *handle, void *prev_state);
@class W7SLSession, W7SLTicket;
static pthread_mutex_t w7sl_registry_lock = PTHREAD_MUTEX_INITIALIZER;
static W7SLSession *w7sl_registry;
static uint64_t w7sl_next_id = 1;
static _Atomic unsigned w7sl_live_sessions;
static _Atomic unsigned w7sl_live_tickets;

@interface W7SLSession : NSObject {
@public
  pthread_mutex_t lock;
  pthread_cond_t wake;
  uint64_t identity;
  BOOL closed, started, cancelled, fallback;
  uint64_t next_ticket_id;
  MTLSharedEventListener *listener;
  CFRunLoopRef runloop;
  CFRunLoopSourceRef source;
  W7SLTicket *head, *tail;
  W7SLTicket *pending;
  W7SLSession *registry_next;
}
@end
@interface W7SLTicket : NSObject {
@public
  uint64_t identity;
  W7SLTicket *next, *pending_next;
  void *event;
  BOOL queued;
}
@end
@implementation W7SLSession
- (id)init {
  if ((self = [super init])) {
    pthread_mutex_init(&lock, NULL); pthread_cond_init(&wake, NULL); next_ticket_id = 1;
    atomic_fetch_add(&w7sl_live_sessions, 1);
  }
  return self;
}
- (void)dealloc {
  /* Only start owns runloop/source; only the registry owns an open session.
   * Pending tickets are drained by start or close before their last owner exits.
   * Native notification blocks capture IDs only and cannot retain this session. */
  [listener release];
  pthread_cond_destroy(&wake);
  pthread_mutex_destroy(&lock);
  atomic_fetch_sub(&w7sl_live_sessions, 1);
  [super dealloc];
}
@end
@implementation W7SLTicket
- (id)init {
  if ((self = [super init])) atomic_fetch_add(&w7sl_live_tickets, 1);
  return self;
}
- (void)dealloc { atomic_fetch_sub(&w7sl_live_tickets, 1); [super dealloc]; }
@end

static W7SLSession *w7sl_find(uint64_t identity) {
  W7SLSession *result = nil;
  pthread_mutex_lock(&w7sl_registry_lock);
  for (W7SLSession *s = w7sl_registry; s; s = s->registry_next)
    if (s->identity == identity) { result = [s retain]; break; }
  pthread_mutex_unlock(&w7sl_registry_lock);
  return result;
}
/* Caller holds lock. The pending list owns the ticket; the ready queue borrows
 * it. Its identity is never reused, even after its Windows handle is reused. */
static void w7sl_enqueue(W7SLSession *s, W7SLTicket *ticket) {
  if (ticket->queued) return;
  ticket->queued = YES;
  if (s->tail) s->tail->next = ticket;
  else s->head = ticket;
  s->tail = ticket;
  pthread_cond_signal(&s->wake);
  if (s->source) CFRunLoopSourceSignal(s->source);
  if (s->runloop) CFRunLoopWakeUp(s->runloop);
}
static void w7sl_notify(uint64_t session_id, uint64_t ticket_id) {
  W7SLSession *s = w7sl_find(session_id);
  if (!s) return;
  pthread_mutex_lock(&s->lock);
  if (!s->closed) {
    for (W7SLTicket *ticket = s->pending; ticket; ticket = ticket->pending_next)
      if (ticket->identity == ticket_id) { w7sl_enqueue(s, ticket); break; }
  }
  pthread_mutex_unlock(&s->lock);
  [s release];
}
/* Caller holds lock and its own session reference. Called only by the PE
 * listener thread, or by the PE destroy caller draining terminal cancellation.
 * No NtSetEvent can occur after destroy returns. */
static void w7sl_drain(W7SLSession *s, BOOL signal, unsigned limit) {
  while (s->head && limit--) {
    W7SLTicket *ticket = s->head;
    s->head = ticket->next; ticket->next = nil;
    if (!s->head) s->tail = nil;
    W7SLTicket **cursor = &s->pending;
    while (*cursor && *cursor != ticket) cursor = &(*cursor)->pending_next;
    if (*cursor) *cursor = ticket->pending_next;
    ticket->pending_next = nil;
    if (signal) NtSetEvent(ticket->event, NULL);
    [ticket release];
  }
}
static void w7sl_discard_pending(W7SLSession *s) {
  s->head = s->tail = nil;
  while (s->pending) {
    W7SLTicket *ticket = s->pending;
    s->pending = ticket->pending_next;
    ticket->next = ticket->pending_next = nil;
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
  if (!s) return (NTSTATUS)0xc0000008; /* STATUS_INVALID_HANDLE */
  /* Reserved existing ABI control: only the exact all-zero triple cancels.
   * Cancellation wakes Windows waiters; it never changes a Metal event value. */
  if (!p->shared_event && !p->event_handle && !p->value) {
    pthread_mutex_lock(&s->lock);
    if (!s->closed) {
      s->cancelled = YES;
      for (W7SLTicket *ticket = s->pending; ticket; ticket = ticket->pending_next)
        w7sl_enqueue(s, ticket);
    }
    pthread_mutex_unlock(&s->lock);
    [s release];
    return STATUS_SUCCESS;
  }
  if (!p->shared_event || !p->event_handle) {
    [s release];
    return (NTSTATUS)0xc000000d; /* STATUS_INVALID_PARAMETER */
  }
  W7SLTicket *ticket = W7SL_NEW_TICKET();
  if (!ticket) {
    [s release];
    /* The legacy PE wrapper is void and cannot report this allocation failure.
     * Do not fake completion or signal on the native callback/caller thread. */
    return (NTSTATUS)0xc0000017; /* STATUS_NO_MEMORY */
  }
  pthread_mutex_lock(&s->lock);
  MTLSharedEventListener *listener = nil;
  if (!s->closed && s->next_ticket_id) {
    ticket->identity = s->next_ticket_id++;
    ticket->event = (void *)(uintptr_t)p->event_handle;
    ticket->pending_next = s->pending; s->pending = ticket;
    if (s->cancelled) w7sl_enqueue(s, ticket);
    else listener = [s->listener retain];
  } else {
    [ticket release]; ticket = nil;
  }
  const uint64_t session_id = s->identity;
  const uint64_t ticket_id = ticket ? ticket->identity : 0;
  pthread_mutex_unlock(&s->lock);
  if (listener) {
    [(id<MTLSharedEvent>)(uintptr_t)p->shared_event notifyListener:listener atValue:p->value
      block:^(id<MTLSharedEvent> event, uint64_t value) {
        (void)event; (void)value;
        w7sl_notify(session_id, ticket_id);
      }];
  }
  [listener release]; [s release];
  return ticket_id ? STATUS_SUCCESS : (NTSTATUS)0xc0000008; /* STATUS_INVALID_HANDLE */
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
  s->source = W7SL_NEW_SOURCE(&context);
  if (!s->source) {
    /* Source allocation is optional for delivery, not a reason to abandon this
     * PE owner. The embedded condition variable needs no CF source/runloop and
     * uses the same locked queue/closed predicates, including pre-start work. */
    s->fallback = YES;
    while (!s->closed) {
      while (!s->closed && !s->head) pthread_cond_wait(&s->wake, &s->lock);
      if (s->closed) break;
      w7sl_drain(s, YES, 64);
      /* Release between batches so close/cancel/registration can make progress. */
      pthread_mutex_unlock(&s->lock);
      pthread_mutex_lock(&s->lock);
    }
    s->fallback = NO;
    pthread_mutex_unlock(&s->lock);
    [s release];
    return STATUS_SUCCESS;
  }
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
    /* A failure can be followed immediately by queue teardown, including before
     * start ran. The destroy unixcall is itself on a PE thread: synchronously
     * deliver terminal wakeups here rather than abandoning them at close. Normal
     * non-cancelled close still suppresses all pending Windows-handle delivery. */
    if (s->cancelled) w7sl_drain(s, YES, ~0u);
    w7sl_discard_pending(s);
    /* A registering thread owns its separate listener reference. Detach here
     * to avoid any listener/block/session retention cycle after shutdown. */
    retired_listener = s->listener; s->listener = nil;
    if (s->source) CFRunLoopSourceSignal(s->source);
    if (s->runloop) { CFRunLoopStop(s->runloop); CFRunLoopWakeUp(s->runloop); }
    pthread_cond_broadcast(&s->wake);
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
