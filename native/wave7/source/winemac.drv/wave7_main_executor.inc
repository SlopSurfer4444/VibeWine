/* Optional synchronous bridge executor using Wine's request source, including
 * WineAppWaitQueryResponseMode. Never dispatches PE events on the caller.
 * Start/stop are Cocoa-main lifecycle hooks. Stop cancels queued work; work
 * already admitted on main finishes before its caller is released. */
#include <pthread.h>

@interface W7MainRequest : NSObject
{
@public
    dispatch_block_t payload;
    dispatch_semaphore_t completion;
    W7MainRequest *next;
    BOOL cancelled;
    int result;
}
@end
@implementation W7MainRequest
- (void)dealloc
{
    [payload release];
    if (completion) dispatch_release(completion);
    [super dealloc];
}
@end

static pthread_mutex_t w7me_lock = PTHREAD_MUTEX_INITIALIZER;
static W7MainRequest *w7me_pending;
static BOOL w7me_accepting, w7me_stopped;

void macdrv_main_executor_start_v1(void)
{
    NSCAssert([NSThread isMainThread], @"executor startup requires Cocoa main");
    pthread_mutex_lock(&w7me_lock);
    if (!w7me_stopped) w7me_accepting = YES;
    pthread_mutex_unlock(&w7me_lock);
}

void macdrv_main_executor_stop_v1(void)
{
    NSCAssert([NSThread isMainThread], @"executor shutdown requires Cocoa main");
    pthread_mutex_lock(&w7me_lock);
    w7me_accepting = NO;
    w7me_stopped = YES;
    W7MainRequest *pending = w7me_pending;
    w7me_pending = nil;
    for (W7MainRequest *r = pending; r; r = r->next) r->cancelled = YES;
    pthread_mutex_unlock(&w7me_lock);
    while (pending)
    {
        W7MainRequest *r = pending;
        pending = r->next;
        /* Queue and waiting worker both own r. Discard caller captures before
         * waking it; the queued wrapper subsequently only observes cancelled. */
        dispatch_block_t payload = r->payload;
        r->payload = nil;
        [payload release];
        dispatch_semaphore_signal(r->completion);
    }
}

__attribute__((visibility("default")))
int macdrv_run_on_main_v1(dispatch_block_t block)
{
    if (!block) return 0;
    if ([NSThread isMainThread])
    {
        pthread_mutex_lock(&w7me_lock);
        BOOL accepting = w7me_accepting;
        pthread_mutex_unlock(&w7me_lock);
        if (!accepting) return 0;
        @try { block(); return 1; }
        @catch (id exception) { (void)exception; return 0; }
    }
    @autoreleasepool
    {
        W7MainRequest *request = [[W7MainRequest alloc] init];
        if (!request) return 0;
        request->completion = dispatch_semaphore_create(0);
        request->payload = [block copy];
        if (!request->completion || !request->payload) { [request release]; return 0; }
        pthread_mutex_lock(&w7me_lock);
        if (!w7me_accepting)
        {
            pthread_mutex_unlock(&w7me_lock);
            [request release];
            return 0;
        }
        request->next = w7me_pending;
        w7me_pending = request;
        /* Enqueue and shutdown admission are serialized. OnMainThreadAsync
         * only appends to the request queue; it never waits for Cocoa main. */
        @try
        {
        OnMainThreadAsync(^{
            pthread_mutex_lock(&w7me_lock);
            if (request->cancelled)
            {
                pthread_mutex_unlock(&w7me_lock);
                return;
            }
            W7MainRequest **link = &w7me_pending;
            while (*link && *link != request) link = &(*link)->next;
            if (*link) *link = request->next;
            dispatch_block_t payload = request->payload;
            request->payload = nil;
            pthread_mutex_unlock(&w7me_lock);
            @try { payload(); request->result = 1; }
            @catch (id exception) { (void)exception; request->result = 0; }
            @finally
            {
                [payload release];
                dispatch_semaphore_signal(request->completion);
            }
        });
        }
        @catch (id exception)
        {
            (void)exception;
            w7me_pending = request->next;
            request->cancelled = YES;
            pthread_mutex_unlock(&w7me_lock);
            dispatch_block_t payload = request->payload;
            request->payload = nil;
            [payload release];
            [request release];
            return 0;
        }
        pthread_mutex_unlock(&w7me_lock);
        dispatch_semaphore_wait(request->completion, DISPATCH_TIME_FOREVER);
        int result = request->result;
        [request release];
        return result;
    }
}
