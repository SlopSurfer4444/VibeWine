/* Included once by winemetal_ds3.c after its framework imports. MRC required. */
#include "wave7_feedback.h"
#include <pthread.h>
#include <dlfcn.h>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

static pthread_mutex_t w7fb_registry_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t w7fb_next_id = 1;
static unsigned w7fb_live_sessions;
@class W7FBSession;
static W7FBSession *w7fb_sessions[W7FB_SESSION_CAPACITY];

@interface W7FBSession : NSObject {
@public
  pthread_mutex_t lock;
  uint64_t identity, generation, serial;
  BOOL closed;
  struct W7FBStats counters;
  struct W7FBRecord ring[W7FB_RING_CAPACITY];
  unsigned head, count;
}
@end
@implementation W7FBSession
- (id)init {
  if ((self = [super init])) pthread_mutex_init(&lock, NULL);
  return self;
}
- (void)dealloc {
  pthread_mutex_destroy(&lock);
  pthread_mutex_lock(&w7fb_registry_lock);
  --w7fb_live_sessions;
  pthread_mutex_unlock(&w7fb_registry_lock);
  [super dealloc];
}
@end

/* The copied Metal block owns this ticket, which owns only the native session.
 * Destroying an uninvoked block releases its pending ticket too. No drawable,
 * request pointer, PE callback or raw swapchain is captured. */
@interface W7FBTicket : NSObject {
@public
  W7FBSession *session;
  struct W7FBRecord record;
  BOOL fired;
}
- (void)complete:(id<MTLDrawable>)drawable;
@end
@implementation W7FBTicket
- (void)complete:(id<MTLDrawable>)drawable {
  /* Read supplied callback argument; never capture the registered drawable. */
  double seconds = [drawable presentedTime];
  pthread_mutex_lock(&session->lock);
  if (!fired) {
    fired = YES;
    ++session->counters.completed;
    if (!session->closed) {
      struct W7FBRecord result = record;
      result.presented_seconds = seconds;
      result.clock_domain = W7FB_CLOCK_CA_HOST_SECONDS;
      if (session->count == W7FB_RING_CAPACITY) {
        ++session->counters.overflow; /* drop newest; preserve queued order */
      } else {
        session->ring[(session->head + session->count) % W7FB_RING_CAPACITY] = result;
        ++session->count;
      }
    }
  }
  pthread_mutex_unlock(&session->lock);
}
- (void)dealloc {
  pthread_mutex_lock(&session->lock);
  if (!fired) ++session->counters.abandoned;
  --session->counters.pending;
  pthread_mutex_unlock(&session->lock);
  [session release];
  [super dealloc];
}
@end

static W7FBSession *w7fb_find(uint64_t identity) {
  W7FBSession *found = nil;
  pthread_mutex_lock(&w7fb_registry_lock);
  for (unsigned i = 0; i < W7FB_SESSION_CAPACITY; ++i)
    if (w7fb_sessions[i] && w7fb_sessions[i]->identity == identity) {
      found = [w7fb_sessions[i] retain]; break;
    }
  pthread_mutex_unlock(&w7fb_registry_lock);
  return found;
}
static void w7fb_snapshot(W7FBSession *s, struct W7FBRequest *r) {
  r->stats = s->counters;
  r->stats.queued = s->count;
}
static uint64_t w7fb_dispatch(struct W7FBRequest *r) {
  if (!r) return 0;
  if (r->size != sizeof(*r) || r->version != W7FB_VERSION) return 0;
  r->status = W7FB_BAD_REQUEST;
  BOOL supported = NO;
  if (@available(macOS 10.15.4, *)) supported = YES;
  r->capabilities = supported ? (W7FB_CAP_PRESENTED_HANDLER | W7FB_CAP_CLOSE_QUEUED_DISCARD) : 0;
  if (r->op == W7FB_QUERY) { r->status = supported ? W7FB_OK : W7FB_UNSUPPORTED; return W7FB_REPLY; }
  if (r->op == W7FB_CAPTURE_STATUS) {
    int (*query)(void) = (int (*)(void))dlsym(RTLD_DEFAULT, "macdrv_display_capture_cleanup_complete_v1");
    r->status = !query ? W7FB_UNSUPPORTED : (query() == 1 ? W7FB_OK : W7FB_PENDING);
    return W7FB_REPLY;
  }
  if (r->op == W7FB_OPEN) {
    if (!supported) { r->status = W7FB_UNSUPPORTED; return W7FB_REPLY; }
    if (!r->record.generation || !r->record.swapchain_serial) return W7FB_REPLY;
    pthread_mutex_lock(&w7fb_registry_lock);
    if (w7fb_live_sessions == W7FB_SESSION_CAPACITY || w7fb_next_id == 0) {
      pthread_mutex_unlock(&w7fb_registry_lock); r->status = W7FB_LIMIT; return W7FB_REPLY;
    }
    W7FBSession *s = [[W7FBSession alloc] init];
    if (!s) { pthread_mutex_unlock(&w7fb_registry_lock); r->status = W7FB_LIMIT; return W7FB_REPLY; }
    ++w7fb_live_sessions;
    s->identity = w7fb_next_id++;
    s->generation = r->record.generation;
    s->serial = r->record.swapchain_serial;
    for (unsigned i = 0; i < W7FB_SESSION_CAPACITY; ++i)
      if (!w7fb_sessions[i]) { w7fb_sessions[i] = s; break; }
    r->session = s->identity;
    r->status = W7FB_OK;
    pthread_mutex_unlock(&w7fb_registry_lock);
    return W7FB_REPLY;
  }
  W7FBSession *s = w7fb_find(r->session);
  if (!s) { r->status = W7FB_NO_SESSION; return W7FB_REPLY; }
  pthread_mutex_lock(&s->lock);
  uint64_t close_queued = 0;
  if (s->closed) r->status = W7FB_NO_SESSION;
  else if (r->op == W7FB_CLOSE) {
    close_queued = s->count; /* CLOSE reports records discarded at its lock boundary. */
    s->closed = YES; s->count = 0;
    pthread_mutex_lock(&w7fb_registry_lock);
    for (unsigned i = 0; i < W7FB_SESSION_CAPACITY; ++i)
      if (w7fb_sessions[i] == s) { w7fb_sessions[i] = nil; [s release]; break; }
    pthread_mutex_unlock(&w7fb_registry_lock);
    r->status = W7FB_OK;
  } else if (r->op == W7FB_STATUS) r->status = W7FB_OK;
  else if (r->op == W7FB_POLL) {
    if (!s->count) r->status = W7FB_EMPTY;
    else { r->record = s->ring[s->head]; s->head = (s->head + 1) % W7FB_RING_CAPACITY; --s->count; r->status = W7FB_OK; }
  } else if (r->op == W7FB_REGISTER) {
    id<MTLDrawable> drawable = (id<MTLDrawable>)(uintptr_t)r->drawable;
    if (r->record.generation != s->generation || r->record.swapchain_serial != s->serial) r->status = W7FB_STALE;
    else if (!drawable || ![drawable respondsToSelector:@selector(addPresentedHandler:)] ||
             ![drawable respondsToSelector:@selector(presentedTime)]) r->status = W7FB_UNSUPPORTED;
    else if (s->counters.pending >= W7FB_RING_CAPACITY) r->status = W7FB_LIMIT;
    else {
      W7FBTicket *ticket = [[W7FBTicket alloc] init];
      if (!ticket) r->status = W7FB_LIMIT;
      else {
        ticket->session = [s retain]; ticket->record = r->record;
        ++s->counters.accepted; ++s->counters.pending;
        pthread_mutex_unlock(&s->lock);
        /* Metal copies the block; ticket lifetime handles both callback and cancellation. */
        [drawable addPresentedHandler:^(id<MTLDrawable> presented) { [ticket complete:presented]; }];
        [ticket release];
        pthread_mutex_lock(&s->lock);
        r->status = W7FB_OK;
      }
    }
    if (r->status != W7FB_OK) ++s->counters.rejected;
  }
  w7fb_snapshot(s, r);
  if (r->op == W7FB_CLOSE && r->status == W7FB_OK) r->stats.queued = close_queued;
  pthread_mutex_unlock(&s->lock);
  [s release];
  return W7FB_REPLY;
}
