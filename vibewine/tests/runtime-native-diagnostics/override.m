/* Foundation-only mocks for the extracted, unmodified WineMetalLayer method.
 * No AppKit window, Metal device, Wine runtime, or live controls are used. */
#import <Foundation/Foundation.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static unsigned delegate_gets, superview_gets, window_gets, queue_gets;
static unsigned startup_calls, create_calls, post_calls, release_calls, super_calls;
static unsigned raw_calls, cpu_calls;
static void *surface = (void *)(uintptr_t)0x1234;
static int fixture_clock(clockid_t clock, struct timespec *ts)
{
    uint64_t ns;
    if (clock == CLOCK_MONOTONIC_RAW) ns = UINT64_C(1000000000) + UINT64_C(100000000) * raw_calls++;
    else ns = UINT64_C(1000000) * cpu_calls++;
    ts->tv_sec = ns / UINT64_C(1000000000);
    ts->tv_nsec = ns % UINT64_C(1000000000);
    return 0;
}
#define W7PD_CLOCK_GETTIME fixture_clock
/* DIAGNOSTIC_HEADER */

@protocol CAMetalDrawable @end
@interface FixtureDrawable : NSObject <CAMetalDrawable> @end
@implementation FixtureDrawable @end
static id<CAMetalDrawable> fixture_drawable;

@interface NSView : NSObject { NSView *_superview; id _window; }
@property(assign) NSView *superview;
@property(assign) id window;
@end
@implementation NSView
- (NSView *)superview { superview_gets++; return _superview; }
- (void)setSuperview:(NSView *)value { _superview = value; }
- (id)window { window_gets++; return _window; }
- (void)setWindow:(id)value { _window = value; }
@end
@interface WineMetalView : NSView @end
@implementation WineMetalView @end
@interface WineContentView : NSView @end
@implementation WineContentView @end

#define CLIENT_SURFACE_PRESENTED 22
typedef void *macdrv_view;
typedef struct macdrv_event { int type; id window; struct { void *client_surface; } client_surface_presented; } macdrv_event;
@interface WineEventQueue : NSObject
- (void)postEvent:(macdrv_event *)event;
@end
@implementation WineEventQueue
- (void)postEvent:(macdrv_event *)event {
    if (event->type != CLIENT_SURFACE_PRESENTED || event->client_surface_presented.client_surface != surface) abort();
    post_calls++;
}
@end
@interface WineWindow : NSObject { WineEventQueue *_queue; }
@property(assign) WineEventQueue *queue;
@end
@implementation WineWindow
- (WineEventQueue *)queue { queue_gets++; return _queue; }
- (void)setQueue:(WineEventQueue *)value { _queue = value; }
@end
static void *macdrv_get_view_d3dmetal_client_surface(macdrv_view view) { return surface; }
static macdrv_event *macdrv_create_event(int type, WineWindow *window) {
    macdrv_event *event = calloc(1, sizeof(*event));
    event->type = type; event->window = window; create_calls++; return event;
}
static void macdrv_release_event(macdrv_event *event) { release_calls++; free(event); }
static void w7vis_startup_from_layer(id layer) { startup_calls++; }
@interface FixtureMetalLayer : NSObject { id _delegate; }
@property(assign) id delegate;
- (id<CAMetalDrawable>)nextDrawable;
@end
@implementation FixtureMetalLayer
- (id)delegate { delegate_gets++; return _delegate; }
- (void)setDelegate:(id)value { _delegate = value; }
- (id<CAMetalDrawable>)nextDrawable { super_calls++; return fixture_drawable; }
@end
@interface WineMetalLayer : FixtureMetalLayer @end
/* EXTRACTED_IMPLEMENTATION */

int main(int argc, char **argv)
{
    @autoreleasepool {
        bool posting = argc > 1 && !strcmp(argv[1], "post");
        WineMetalLayer *layer = [WineMetalLayer new];
        WineMetalView *view = [WineMetalView new];
        WineContentView *content = [WineContentView new];
        WineWindow *window = [WineWindow new];
        WineEventQueue *queue = [WineEventQueue new];
        NSObject *other = [NSObject new];
        fixture_drawable = [FixtureDrawable new];
        view.superview = content; view.window = window; window.queue = queue;
        layer.delegate = posting ? view : other;
        id<CAMetalDrawable> result = [layer nextDrawable];
        printf("{\"delegate\":%u,\"superview\":%u,\"window\":%u,\"queue\":%u,\"startup\":%u,\"create\":%u,\"post\":%u,\"release\":%u,\"super\":%u,\"same_drawable\":%s,\"raw_calls\":%u,\"cpu_calls\":%u}\n",
            delegate_gets, superview_gets, window_gets, queue_gets, startup_calls, create_calls,
            post_calls, release_calls, super_calls, result == fixture_drawable ? "true" : "false", raw_calls, cpu_calls);
        [(NSObject *)fixture_drawable release];
        [other release]; [queue release]; [window release]; [content release]; [view release]; [layer release];
    }
    return 0;
}
