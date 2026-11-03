/* Self-written diagnostic using public AppKit, Metal and QuartzCore APIs.
 * No imported sample implementation. See native_drawable_control.md for scope.
 */
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>
#import <CoreGraphics/CoreGraphics.h>
#include <dispatch/dispatch.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

enum { WarmFrames = 30, MeasuredFrames = 180, DeadlineSeconds = 25 };
static int stopped, emitted, processStatus = 1;

static double seconds(clockid_t clock) {
  struct timespec value;
  if (clock_gettime(clock, &value)) return NAN;
  return value.tv_sec + value.tv_nsec * 1e-9;
}
static int compareDouble(const void *a, const void *b) {
  double x = *(const double *)a, y = *(const double *)b;
  return (x > y) - (x < y);
}
static NSDictionary *stats(double *samples, NSUInteger count) {
  if (!count) return @{ @"count": @0 };
  double sorted[MeasuredFrames], sum = 0;
  for (NSUInteger i = 0; i < count; ++i) {
    sorted[i] = samples[i]; sum += samples[i];
  }
  qsort(sorted, count, sizeof(double), compareDouble);
  double median = count % 2 ? sorted[count / 2]
      : (sorted[count / 2 - 1] + sorted[count / 2]) / 2;
  return @{ @"count": @(count), @"mean_ms": @(sum / count),
            @"median_ms": @(median),
            @"p95_ms": @(sorted[(NSUInteger)ceil(count * .95) - 1]) };
}

@interface NativeControl : NSObject <NSApplicationDelegate, NSWindowDelegate> {
  NSWindow *window;
  CAMetalLayer *metalLayer;
  id<MTLDevice> device;
  id<MTLCommandQueue> queue;
}
@end

@implementation NativeControl
/* Called only on the AppKit thread; raw configured refresh may be zero. */
- (NSDictionary *)snapshot {
  NSScreen *screen = [window screen];
  NSNumber *displayID = [[screen deviceDescription] objectForKey:@"NSScreenNumber"];
  CGDisplayModeRef mode = displayID ? CGDisplayCopyDisplayMode([displayID unsignedIntValue]) : NULL;
  double refresh = mode ? CGDisplayModeGetRefreshRate(mode) : 0;
  if (mode) CGDisplayModeRelease(mode);
  NSSize content = [[window contentView] bounds].size;
  return @{ @"display_sync_enabled": @([metalLayer displaySyncEnabled]),
    @"maximum_drawable_count": @([metalLayer maximumDrawableCount]),
    @"presents_with_transaction": @([metalLayer presentsWithTransaction]),
    @"allows_next_drawable_timeout": @([metalLayer allowsNextDrawableTimeout]),
    @"feedback_handler_installed": @NO,
    @"window_visible": @([window isVisible]),
    @"window_occlusion_visible": @(([window occlusionState] & NSWindowOcclusionStateVisible) != 0),
    @"window_key": @([window isKeyWindow]), @"app_active": @([NSApp isActive]),
    @"window_client_width_points": @(content.width),
    @"window_client_height_points": @(content.height),
    @"drawable_width_pixels": @([metalLayer drawableSize].width),
    @"drawable_height_pixels": @([metalLayer drawableSize].height),
    @"backing_scale_factor": @([window backingScaleFactor]),
    @"display_id": displayID ?: (id)[NSNull null],
    @"configured_refresh_hz": @(refresh),
    @"configured_refresh_available": @(refresh > 0),
    @"screen_maximum_frames_per_second": @(screen ? [screen maximumFramesPerSecond] : 0) };
}
- (void)finish:(NSDictionary *)report status:(int)status {
  /* Serialize and emit only once, including if the independent watchdog wins. */
  NSError *error = nil;
  NSData *json = [NSJSONSerialization dataWithJSONObject:report options:0 error:&error];
  if (!json || [json length] >= 65535) {
    json = [@"{\"status\":\"failed\",\"error\":\"invalid_or_oversized_json\"}"
              dataUsingEncoding:NSUTF8StringEncoding];
    status = 1;
  }
  if (!__atomic_exchange_n(&emitted, 1, __ATOMIC_SEQ_CST)) {
    fwrite([json bytes], 1, [json length], stdout); fputc('\n', stdout); fflush(stdout);
  }
  processStatus = status;
  [window orderOut:nil];
  [window close];
  [NSApp stop:nil];
  /* Wake an event wait after stop; no second application/process is touched. */
  [NSApp postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined
      location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:0
      context:nil subtype:0 data1:0 data2:0] atStart:NO];
}
- (BOOL)windowShouldClose:(NSWindow *)sender {
  (void)sender;
  __atomic_store_n(&stopped, 1, __ATOMIC_RELAXED);
  return YES;
}
- (void)measure {
  NSMutableArray *cases = [NSMutableArray array];
  NSString *failure = nil;
  for (NSUInteger index = 0; index < 4 && !failure; ++index) {
    BOOL sync = index >= 2;
    NSUInteger drawableCount = 2 + index % 2;
    __block NSDictionary *before = nil;
    dispatch_sync(dispatch_get_main_queue(), ^{
      [CATransaction begin]; [CATransaction setDisableActions:YES];
      [metalLayer setDisplaySyncEnabled:sync];
      [metalLayer setMaximumDrawableCount:drawableCount];
      [metalLayer setPresentsWithTransaction:NO];
      [CATransaction commit]; [CATransaction flush];
      before = [[self snapshot] retain];
    });
    NSMutableDictionary *result = [NSMutableDictionary dictionaryWithDictionary:@{
      @"requested_display_sync_enabled": @(sync),
      @"requested_maximum_drawable_count": @(drawableCount),
      @"warmup_frames_requested": @(WarmFrames),
      @"measured_frames_requested": @(MeasuredFrames), @"before": before }];
    [before release];
    NSDictionary *actual = [result objectForKey:@"before"];
    if ([[actual objectForKey:@"display_sync_enabled"] boolValue] != sync ||
        [[actual objectForKey:@"maximum_drawable_count"] unsignedIntegerValue] != drawableCount ||
        [[actual objectForKey:@"presents_with_transaction"] boolValue] ||
        ![[actual objectForKey:@"allows_next_drawable_timeout"] boolValue] ||
        [[actual objectForKey:@"drawable_width_pixels"] integerValue] != 1280 ||
        [[actual objectForKey:@"drawable_height_pixels"] integerValue] != 720) {
      failure = @"layer_setting_readback_mismatch";
    }
    double wall[MeasuredFrames], cpu[MeasuredFrames], frameCPU[MeasuredFrames], gpu[MeasuredFrames];
    double completionWait[MeasuredFrames];
    NSUInteger measured = 0, warmed = 0;
    for (NSUInteger frame = 0; frame < WarmFrames + MeasuredFrames && !failure; ++frame) {
      @autoreleasepool {
        if (__atomic_load_n(&stopped, __ATOMIC_RELAXED)) { failure = @"window_closed"; break; }
        double frameStartCPU = seconds(CLOCK_THREAD_CPUTIME_ID);
        double startCPU = seconds(CLOCK_THREAD_CPUTIME_ID);
        double startWall = seconds(CLOCK_MONOTONIC);
        id<CAMetalDrawable> drawable = [metalLayer nextDrawable];
        double endWall = seconds(CLOCK_MONOTONIC);
        double endCPU = seconds(CLOCK_THREAD_CPUTIME_ID);
        if (!drawable) { failure = @"nil_drawable"; break; }
        id<MTLCommandBuffer> command = [queue commandBuffer];
        if (!command) { failure = @"nil_command_buffer"; break; }
        MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = [drawable texture];
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(.05, .15 + .15 * (frame % 2), .25, 1);
        id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
        if (!encoder) { failure = @"nil_render_encoder"; break; }
        [encoder endEncoding];
        [command presentDrawable:drawable];
        [command commit];
        double waitStart = seconds(CLOCK_MONOTONIC);
        [command waitUntilCompleted]; /* Explicitly serializes GPU submissions. */
        double waitEnd = seconds(CLOCK_MONOTONIC);
        double doneCPU = seconds(CLOCK_THREAD_CPUTIME_ID);
        if ([command status] != MTLCommandBufferStatusCompleted || [command error]) {
          failure = @"command_buffer_failed";
          if ([command error]) [result setObject:[[command error] localizedDescription]
                                        forKey:@"command_error"];
          break;
        }
        double gpuStart = [command GPUStartTime], gpuEnd = [command GPUEndTime];
        if (!isfinite(startWall) || !isfinite(endWall) || endWall < startWall ||
            !isfinite(startCPU) || !isfinite(endCPU) || endCPU < startCPU ||
            !isfinite(frameStartCPU) || !isfinite(doneCPU) || doneCPU < frameStartCPU ||
            !isfinite(waitStart) || !isfinite(waitEnd) || waitEnd < waitStart ||
            !isfinite(gpuStart) || !isfinite(gpuEnd) || gpuStart <= 0 || gpuEnd < gpuStart) {
          failure = @"invalid_clock_or_gpu_timestamp"; break;
        }
        if (frame < WarmFrames) { ++warmed; continue; }
        wall[measured] = (endWall - startWall) * 1000;
        cpu[measured] = (endCPU - startCPU) * 1000;
        frameCPU[measured] = (doneCPU - frameStartCPU) * 1000;
        gpu[measured] = (gpuEnd - gpuStart) * 1000;
        completionWait[measured] = (waitEnd - waitStart) * 1000;
        ++measured;
      }
    }
    [result setObject:@(warmed) forKey:@"warmup_frames_completed"];
    [result setObject:@(measured) forKey:@"measured_frames_completed"];
    [result setObject:stats(wall, measured) forKey:@"next_drawable_wall"];
    [result setObject:stats(cpu, measured) forKey:@"next_drawable_worker_thread_cpu"];
    [result setObject:stats(frameCPU, measured) forKey:@"frame_worker_thread_cpu"];
    [result setObject:stats(gpu, measured) forKey:@"command_buffer_gpu_duration"];
    [result setObject:stats(completionWait, measured) forKey:@"wait_until_completed_wall"];
    dispatch_sync(dispatch_get_main_queue(), ^{
      [result setObject:[self snapshot] forKey:@"after"];
    });
    NSDictionary *after = [result objectForKey:@"after"];
    if (!failure && ([[after objectForKey:@"display_sync_enabled"] boolValue] != sync ||
        [[after objectForKey:@"maximum_drawable_count"] unsignedIntegerValue] != drawableCount ||
        [[after objectForKey:@"presents_with_transaction"] boolValue] ||
        ![[after objectForKey:@"allows_next_drawable_timeout"] boolValue] ||
        [[after objectForKey:@"drawable_width_pixels"] integerValue] != 1280 ||
        [[after objectForKey:@"drawable_height_pixels"] integerValue] != 720)) {
      failure = @"layer_setting_readback_mismatch_after_case";
    }
    [result setObject:failure ? @"failed" : @"complete" forKey:@"status"];
    if (failure) [result setObject:failure forKey:@"error"];
    [cases addObject:result];
  }
  NSDictionary *report = @{ @"schema": @"native-drawable-control-v1",
    @"status": failure ? @"failed" : @"complete", @"error": failure ?: (id)[NSNull null],
    @"device": [device name], @"cases": cases, @"deadline_seconds": @(DeadlineSeconds),
    @"feedback_mode": @"off_no_presented_handler",
    @"submission_mode": @"clear_present_commit_waitUntilCompleted_each_frame",
    @"gpu_serialization_limitation": @YES, @"physical_fps_measured": @NO,
    @"configured_refresh_zero_means_unavailable": @YES };
  dispatch_sync(dispatch_get_main_queue(), ^{ [self finish:report status:failure ? 1 : 0]; });
}
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
  (void)notification;
  device = MTLCreateSystemDefaultDevice();
  queue = [device newCommandQueue];
  if (!device || !queue) {
    [self finish:@{ @"status": @"failed", @"error": @"metal_device_or_queue_unavailable" } status:1];
    return;
  }
  window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1280, 720)
      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
      backing:NSBackingStoreBuffered defer:NO];
  [window setReleasedWhenClosed:NO]; [window setDelegate:self];
  [window setTitle:@"Native drawable control (25 second bound)"];
  metalLayer = [[CAMetalLayer alloc] init];
  [metalLayer setDevice:device]; [metalLayer setPixelFormat:MTLPixelFormatBGRA8Unorm];
  [metalLayer setFramebufferOnly:NO]; [metalLayer setAllowsNextDrawableTimeout:YES];
  [metalLayer setContentsScale:1]; [metalLayer setDrawableSize:CGSizeMake(1280, 720)];
  NSView *view = [window contentView];
  [view setWantsLayer:YES]; [view setLayer:metalLayer];
  [metalLayer setFrame:[view bounds]];
  [window center]; [window makeKeyAndOrderFront:nil]; [NSApp activateIgnoringOtherApps:YES];
  [CATransaction flush];
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    @autoreleasepool { [self measure]; }
  });
}
- (void)dealloc {
  [window setDelegate:nil]; [window release]; [metalLayer release];
  [queue release]; [device release]; [super dealloc];
}
@end

int main(void) {
  @autoreleasepool {
    /* Independent own-process deadline also covers a stuck GPU or AppKit call. */
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, DeadlineSeconds * NSEC_PER_SEC),
      dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        if (!__atomic_exchange_n(&emitted, 1, __ATOMIC_SEQ_CST)) {
          const char message[] = "{\"schema\":\"native-drawable-control-v1\",\"status\":\"failed\",\"error\":\"25_second_watchdog\"}\n";
          write(STDOUT_FILENO, message, sizeof(message) - 1);
        }
        _exit(124);
      });
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    NativeControl *delegate = [[NativeControl alloc] init];
    [NSApp setDelegate:delegate]; [NSApp run];
    [NSApp setDelegate:nil]; [delegate release];
  }
  return processStatus;
}
