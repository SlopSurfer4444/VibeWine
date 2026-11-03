/* Original diagnostic, written for this experiment using public Apple APIs.
 * See native_present_pipeline.md. No external sample implementation imported.
 */
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>
#import <CoreGraphics/CoreGraphics.h>
#include <dispatch/dispatch.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#include <time.h>
#include <unistd.h>

namespace {
constexpr unsigned Width = 1280, Height = 720, Slots = 3;
constexpr unsigned WarmFrames = 90, MeasuredFrames = 90, DeadlineSeconds = 45;
constexpr size_t RowBytes = Width * 4, ImageBytes = RowBytes * Height;
constexpr auto WaitBound = std::chrono::seconds(3);
std::atomic<bool> stopped{false}, emitted{false};
int processStatus = 1;
bool corruptFrame = false;

double clockSeconds(clockid_t clock) {
  timespec value{};
  if (clock_gettime(clock, &value)) return NAN;
  return double(value.tv_sec) + double(value.tv_nsec) * 1e-9;
}
bool elapsed(double start, double end) {
  return std::isfinite(start) && std::isfinite(end) && end >= start;
}
NSDictionary *stats(const std::vector<double> &values) {
  if (values.empty()) return @{ @"count": @0 };
  std::vector<double> sorted(values);
  std::sort(sorted.begin(), sorted.end());
  double sum = 0;
  for (double value : sorted) sum += value;
  size_t n = sorted.size();
  double median = n % 2 ? sorted[n / 2] : (sorted[n / 2 - 1] + sorted[n / 2]) / 2;
  return @{ @"count": @(n), @"mean_ms": @(sum / n), @"median_ms": @(median),
            @"p95_ms": @(sorted[size_t(std::ceil(n * .95)) - 1]) };
}
/* Two GPU queues and the same physical resources in all cases; fresh event per phase. */
struct Resources {
  id<MTLCommandQueue> renderQueue = nil, presentQueue = nil;
  id<MTLDevice> device = nil; /* Borrowed; owner outlives all phases. */
  id<MTLTexture> renderTarget = nil;
  id<MTLComputePipelineState> pipeline = nil;
  std::array<id<MTLTexture>, Slots> textures{};
  std::array<id<MTLBuffer>, Slots> snapshots{};
  ~Resources() {
    for (id<MTLTexture> texture : textures) [texture release];
    for (id<MTLBuffer> buffer : snapshots) [buffer release];
    [renderTarget release]; [pipeline release]; [renderQueue release]; [presentQueue release];
  }
};
struct Frame {
  uint32_t identity = 0;
  unsigned index = 0, slot = 0;
};
struct Sample {
  double renderWall = 0, renderCPU = 0, acquireWall = 0, acquireCPU = 0;
  double presentWall = 0, presentCPU = 0, renderGPU = 0, presentGPU = 0;
  double verifyWall = 0;
  bool renderDone = false, presentDone = false, verified = false;
};
struct Phase {
  Resources *resources;
  CAMetalLayer *layer;
  id<MTLSharedEvent> ready;
  unsigned count, iterations;
  uint32_t base;
  bool decoupled, corrupt;
  std::mutex mutex;
  std::condition_variable changed;
  bool cancelled = false, producerFinished = false;
  const char *error = nullptr;
  std::array<bool, Slots> available{{true, true, true}};
  std::array<uint32_t, Slots> owners{};
  std::array<Frame, Slots> ring{};
  unsigned head = 0, tail = 0, queued = 0, maxQueued = 0, inFlight = 0, maxInFlight = 0;
  unsigned produced = 0, requested = 0, renderCommitted = 0, presentCommitted = 0;
  unsigned renderCompleted = 0, presentCompleted = 0, verified = 0, corruptions = 0;
  std::vector<Sample> samples;
  Phase(Resources *r, CAMetalLayer *l, unsigned n, unsigned it, uint32_t first,
        bool split, bool inject) : resources(r), layer(l), count(n), iterations(it),
        base(first), decoupled(split), corrupt(inject), samples(n) {
    ready = [r->device newSharedEvent];
  }
  ~Phase() { [ready release]; }
  void failLocked(const char *message) {
    if (!error) error = message;
    cancelled = true;
    changed.notify_all();
  }
  void fail(const char *message) {
    std::lock_guard<std::mutex> guard(mutex);
    failLocked(message);
  }
  template<class Predicate> bool wait(std::unique_lock<std::mutex> &lock,
                                       Predicate predicate, const char *message) {
    const auto deadline = std::chrono::steady_clock::now() + WaitBound;
    while (!predicate() && !cancelled) {
      if (stopped.load()) { failLocked("window_closed"); break; }
      if (changed.wait_until(lock, std::min(deadline,
          std::chrono::steady_clock::now() + std::chrono::milliseconds(25))) ==
          std::cv_status::timeout && std::chrono::steady_clock::now() >= deadline &&
          !predicate()) { failLocked(message); break; }
    }
    return !cancelled && predicate();
  }
};

/* All 921600 pixels are checked against a frame-specific, non-flat 8x8 tile. */
bool verifyImage(const uint8_t *pixels, uint32_t identity, unsigned iterations) {
  std::array<uint8_t, RowBytes * 8> rows{};
  for (unsigned y = 0; y < 8; ++y) {
    for (unsigned x = 0; x < 8; ++x) {
      uint32_t v = identity * 747796405u + x * 2891336453u + y * 277803737u;
      for (unsigned j = 0; j < iterations; ++j)
        v = (v ^ (v >> 13)) * 1664525u + 1013904223u;
      uint8_t *out = rows.data() + y * RowBytes + x * 4;
      out[0] = uint8_t((v >> 16) ^ identity);
      out[1] = uint8_t(v >> 8); out[2] = uint8_t(v); out[3] = 255;
    }
    for (unsigned x = 8; x < Width; x += 8)
      std::memcpy(rows.data() + y * RowBytes + x * 4, rows.data() + y * RowBytes, 32);
  }
  for (unsigned y = 0; y < Height; ++y)
    if (std::memcmp(pixels + y * RowBytes, rows.data() + (y % 8) * RowBytes, RowBytes))
      return false;
  return true;
}

bool produce(Phase *p, unsigned index) {
  Frame frame{p->base + index, index, index % Slots};
  {
    std::unique_lock<std::mutex> lock(p->mutex);
    if (!p->wait(lock, [&] { return p->available[frame.slot]; }, "slot_wait_timeout")) return false;
    p->available[frame.slot] = false; p->owners[frame.slot] = frame.identity;
    ++p->inFlight; p->maxInFlight = std::max(p->maxInFlight, p->inFlight);
  }
  const double startWall = clockSeconds(CLOCK_MONOTONIC);
  const double startCPU = clockSeconds(CLOCK_THREAD_CPUTIME_ID);
  id<MTLCommandBuffer> command = [p->resources->renderQueue commandBuffer];
  id<MTLComputeCommandEncoder> encoder = command ? [command computeCommandEncoder] : nil;
  if (!encoder) { p->fail("nil_render_command_or_encoder"); return false; }
  uint32_t parameters[2] = {frame.identity, p->iterations};
  [encoder setComputePipelineState:p->resources->pipeline];
  [encoder setTexture:p->resources->renderTarget atIndex:0];
  [encoder setBytes:parameters length:sizeof(parameters) atIndex:0];
  [encoder dispatchThreads:MTLSizeMake(Width, Height, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
  [encoder endEncoding];
  /* Serial producer queue protects the shared target from the next frame. */
  id<MTLBlitCommandEncoder> snapshot = [command blitCommandEncoder];
  if (!snapshot) { p->fail("nil_producer_snapshot_encoder"); return false; }
  [snapshot copyFromTexture:p->resources->renderTarget sourceSlice:0 sourceLevel:0
      sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(Width, Height, 1)
      toTexture:p->resources->textures[frame.slot] destinationSlice:0 destinationLevel:0
      destinationOrigin:MTLOriginMake(0, 0, 0)];
  [snapshot endEncoding];
  [command encodeSignalEvent:p->ready value:uint64_t(frame.identity) + 1];
  [command addCompletedHandler:^(id<MTLCommandBuffer> done) {
    @autoreleasepool {
      const double a = [done GPUStartTime], b = [done GPUEndTime];
      std::lock_guard<std::mutex> guard(p->mutex);
      Sample &sample = p->samples[frame.index];
      if (sample.renderDone) p->failLocked("duplicate_render_completion");
      sample.renderDone = true;
      if ([done status] != MTLCommandBufferStatusCompleted || [done error])
        p->failLocked("render_command_failed");
      else if (!elapsed(a, b) || a <= 0) p->failLocked("invalid_render_gpu_timestamp");
      else sample.renderGPU = (b - a) * 1000;
      ++p->renderCompleted; p->changed.notify_all();
    }
  }];
  { std::lock_guard<std::mutex> guard(p->mutex); ++p->renderCommitted; }
  [command commit];
  const double endCPU = clockSeconds(CLOCK_THREAD_CPUTIME_ID);
  const double endWall = clockSeconds(CLOCK_MONOTONIC);
  {
    std::unique_lock<std::mutex> lock(p->mutex);
    if (!elapsed(startCPU, endCPU) || !elapsed(startWall, endWall)) {
      p->failLocked("invalid_render_cpu_clock"); return false;
    }
    p->samples[index].renderCPU = (endCPU - startCPU) * 1000;
    p->samples[index].renderWall = (endWall - startWall) * 1000;
    if (!p->wait(lock, [&] { return p->queued < Slots; }, "ring_publish_timeout")) return false;
    p->ring[p->tail] = frame; p->tail = (p->tail + 1) % Slots;
    ++p->queued; ++p->produced; p->maxQueued = std::max(p->maxQueued, p->queued);
    p->changed.notify_all();
  }
  return true;
}

bool consume(Phase *p) {
  Frame frame;
  {
    std::unique_lock<std::mutex> lock(p->mutex);
    if (!p->wait(lock, [&] { return p->queued != 0 || p->producerFinished; }, "ring_consume_timeout"))
      return false;
    if (!p->queued) { p->failLocked("producer_finished_before_all_frames"); return false; }
    frame = p->ring[p->head]; p->head = (p->head + 1) % Slots; --p->queued;
    if (frame.index != p->requested || frame.identity != p->base + p->requested ||
        p->owners[frame.slot] != frame.identity || p->available[frame.slot]) {
      p->failLocked("request_or_slot_identity_mismatch"); return false;
    }
    p->changed.notify_all();
  }
  const double acquireWall = clockSeconds(CLOCK_MONOTONIC);
  const double acquireCPU = clockSeconds(CLOCK_THREAD_CPUTIME_ID);
  /* No mutex held across this potentially blocking API. */
  id<CAMetalDrawable> drawable = [p->layer nextDrawable];
  const double acquiredCPU = clockSeconds(CLOCK_THREAD_CPUTIME_ID);
  const double acquiredWall = clockSeconds(CLOCK_MONOTONIC);
  if (!drawable) { p->fail("nil_drawable"); return false; }
  if (!elapsed(acquireCPU, acquiredCPU) || !elapsed(acquireWall, acquiredWall)) {
    p->fail("invalid_acquire_clock"); return false;
  }
  {
    std::lock_guard<std::mutex> guard(p->mutex);
    if (p->cancelled || stopped.load()) { p->failLocked("cancelled_after_acquire"); return false; }
  }
  const double encodeWall = clockSeconds(CLOCK_MONOTONIC);
  const double encodeCPU = clockSeconds(CLOCK_THREAD_CPUTIME_ID);
  id<MTLCommandBuffer> command = [p->resources->presentQueue commandBuffer];
  if (!command) { p->fail("nil_present_command"); return false; }
  [command encodeWaitForEvent:p->ready value:uint64_t(frame.identity) + 1];
  id<MTLBlitCommandEncoder> encoder = [command blitCommandEncoder];
  if (!encoder) { p->fail("nil_blit_encoder"); return false; }
  id<MTLTexture> texture = [drawable texture];
  if ([texture width] != Width || [texture height] != Height ||
      [texture pixelFormat] != MTLPixelFormatBGRA8Unorm) {
    [encoder endEncoding]; p->fail("drawable_texture_mismatch"); return false;
  }
  [encoder copyFromTexture:p->resources->textures[frame.slot] sourceSlice:0 sourceLevel:0
      sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(Width, Height, 1)
      toTexture:texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
  /* Snapshot is taken from the drawable itself, before its one present request. */
  [encoder copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
      sourceSize:MTLSizeMake(Width, Height, 1) toBuffer:p->resources->snapshots[frame.slot]
      destinationOffset:0 destinationBytesPerRow:RowBytes destinationBytesPerImage:ImageBytes];
  [encoder endEncoding];
  [command addCompletedHandler:^(id<MTLCommandBuffer> done) {
    @autoreleasepool {
      const double a = [done GPUStartTime], b = [done GPUEndTime];
      bool valid = [done status] == MTLCommandBufferStatusCompleted && ![done error];
      bool goodTime = elapsed(a, b) && a > 0;
      uint8_t *pixels = static_cast<uint8_t *>([p->resources->snapshots[frame.slot] contents]);
      bool injected = p->corrupt && frame.index == 0 && valid && pixels;
      if (injected) pixels[0] ^= 1; /* Negative control: exactly one bit, one frame. */
      double start = clockSeconds(CLOCK_MONOTONIC);
      bool match = valid && pixels && verifyImage(pixels, frame.identity, p->iterations);
      double end = clockSeconds(CLOCK_MONOTONIC);
      std::lock_guard<std::mutex> guard(p->mutex);
      Sample &sample = p->samples[frame.index];
      if (sample.presentDone || p->available[frame.slot] || p->owners[frame.slot] != frame.identity)
        p->failLocked("completion_or_slot_identity_mismatch");
      sample.presentDone = true;
      if (!valid) p->failLocked("present_command_failed");
      else if (!pixels) p->failLocked("nil_readback_buffer_contents");
      else if (!goodTime) p->failLocked("invalid_present_gpu_timestamp");
      else if (!match) p->failLocked("golden_image_mismatch");
      else if (!elapsed(start, end)) p->failLocked("invalid_verify_clock");
      else { sample.verified = true; ++p->verified; }
      sample.presentGPU = goodTime ? (b - a) * 1000 : 0;
      sample.verifyWall = elapsed(start, end) ? (end - start) * 1000 : 0;
      if (injected) ++p->corruptions;
      ++p->presentCompleted;
      /* Release only after completion status, identity and every pixel were checked. */
      p->available[frame.slot] = true; --p->inFlight; p->changed.notify_all();
    }
  }];
  {
    std::lock_guard<std::mutex> guard(p->mutex);
    ++p->requested; ++p->presentCommitted;
    p->samples[frame.index].acquireWall = (acquiredWall - acquireWall) * 1000;
    p->samples[frame.index].acquireCPU = (acquiredCPU - acquireCPU) * 1000;
  }
  [command presentDrawable:drawable];
  [command commit];
  const double endCPU = clockSeconds(CLOCK_THREAD_CPUTIME_ID);
  const double endWall = clockSeconds(CLOCK_MONOTONIC);
  {
    std::lock_guard<std::mutex> guard(p->mutex);
    if (!elapsed(encodeCPU, endCPU) || !elapsed(encodeWall, endWall))
      p->failLocked("invalid_present_cpu_clock");
    else {
      p->samples[frame.index].presentWall = (endWall - encodeWall) * 1000;
      p->samples[frame.index].presentCPU = (endCPU - encodeCPU) * 1000;
    }
  }
  return true;
}

/* There is no per-frame CPU wait for either GPU command buffer. */
void producer(Phase *p) {
  for (unsigned i = 0; i < p->count; ++i) {
    @autoreleasepool { if (!produce(p, i)) break; }
  }
  std::lock_guard<std::mutex> guard(p->mutex);
  p->producerFinished = true; p->changed.notify_all();
}
void consumer(Phase *p) {
  for (unsigned i = 0; i < p->count; ++i) {
    @autoreleasepool { if (!consume(p)) break; }
  }
}
void coupled(Phase *p) {
  for (unsigned i = 0; i < p->count; ++i) {
    bool made = false, consumed = false;
    @autoreleasepool { made = produce(p, i); }
    if (!made) break;
    @autoreleasepool { consumed = consume(p); }
    if (!consumed) break;
  }
  std::lock_guard<std::mutex> guard(p->mutex);
  p->producerFinished = true; p->changed.notify_all();
}

[[noreturn]] void undrainedExit() {
  /* Context and its resources MUST NOT destruct while callbacks may reference them. */
  if (!emitted.exchange(true)) {
    const char message[] = "{\"schema\":\"native-present-pipeline-v1\",\"status\":\"failed\",\"error\":\"gpu_callback_drain_timeout_context_preserved_until_process_exit\"}\n";
    (void)write(STDOUT_FILENO, message, sizeof(message) - 1);
  }
  _exit(1);
}
NSDictionary *runPhase(Resources *resources, CAMetalLayer *layer, unsigned count,
    unsigned iterations, uint32_t base, bool split, bool inject, bool *success) {
  Phase *p = new Phase(resources, layer, count, iterations, base, split, inject);
  if (!p->ready) p->fail("nil_phase_event");
  const double start = clockSeconds(CLOCK_MONOTONIC);
  if (!p->error && split) {
    std::thread renderer(producer, p), presenter(consumer, p);
    renderer.join(); presenter.join();
  } else if (!p->error) {
    std::thread worker(coupled, p); worker.join();
  }
  {
    std::unique_lock<std::mutex> lock(p->mutex);
    /* A cancellation does not skip draining commands that were already committed. */
    if (!p->changed.wait_for(lock, WaitBound, [&] {
        return p->renderCompleted == p->renderCommitted && p->presentCompleted == p->presentCommitted;
      })) undrainedExit();
    if (p->renderCommitted != count || p->presentCommitted != count || p->produced != count ||
        p->requested != count || p->verified != count || p->queued != 0 || p->inFlight != 0)
      p->failLocked("incomplete_frame_accounting");
  }
  const double end = clockSeconds(CLOCK_MONOTONIC);
  if (!elapsed(start, end) || end <= start) p->fail("invalid_total_clock");
  std::vector<double> renderWall, renderCPU, acquireWall, acquireCPU, presentWall, presentCPU;
  std::vector<double> renderGPU, presentGPU, verifyWall;
  for (const Sample &s : p->samples) {
    if (s.renderDone) renderGPU.push_back(s.renderGPU);
    if (s.presentDone) { presentGPU.push_back(s.presentGPU); verifyWall.push_back(s.verifyWall); }
    if (!s.verified) continue;
    renderWall.push_back(s.renderWall); renderCPU.push_back(s.renderCPU);
    acquireWall.push_back(s.acquireWall); acquireCPU.push_back(s.acquireCPU);
    presentWall.push_back(s.presentWall); presentCPU.push_back(s.presentCPU);
  }
  *success = !p->error;
  NSDictionary *result = @{ @"status": *success ? @"complete" : @"failed",
    @"error": p->error ? [NSString stringWithUTF8String:p->error] : (id)[NSNull null],
    @"frames_offered": @(count), @"first_frame_identity": @(base),
    @"last_frame_identity": @(base + count - 1), @"render_committed": @(p->renderCommitted),
    @"render_completed": @(p->renderCompleted), @"ordered_present_requests": @(p->requested),
    @"present_completed": @(p->presentCompleted), @"full_images_verified": @(p->verified),
    @"pixels_per_verified_image": @(Width * Height), @"corrupted_frames": @(p->corruptions),
    @"max_reserved_slots": @(p->maxInFlight), @"max_queued_items": @(p->maxQueued),
    @"elapsed_start_to_all_completions_ms": @(elapsed(start, end) ? (end - start) * 1000 : 0),
    @"render_encode_wall": stats(renderWall), @"render_encode_thread_cpu": stats(renderCPU),
    @"next_drawable_wall": stats(acquireWall), @"next_drawable_thread_cpu": stats(acquireCPU),
    @"present_encode_wall": stats(presentWall), @"present_encode_thread_cpu": stats(presentCPU),
    @"render_command_gpu": stats(renderGPU), @"present_command_gpu": stats(presentGPU),
    @"full_image_verify_wall": stats(verifyWall) };
  delete p; /* Both workers joined; both committed-callback counts have drained. */
  return result;
}

NSString *shaderSource() {
  return @"#include <metal_stdlib>\nusing namespace metal;\n"
    "kernel void tile(texture2d<float, access::write> out [[texture(0)]],\n"
    " constant uint2 &params [[buffer(0)]], uint2 xy [[thread_position_in_grid]]) {\n"
    " uint v=params.x*747796405u+(xy.x&7u)*2891336453u+(xy.y&7u)*277803737u;\n"
    " for(uint j=0;j<params.y;++j) v=(v^(v>>13))*1664525u+1013904223u;\n"
    " uint r=v&255u,g=(v>>8)&255u,b=((v>>16)^params.x)&255u;\n"
    " out.write(float4(float(r)/255.0f,float(g)/255.0f,float(b)/255.0f,1.0f),xy);\n}\n";
}
} // namespace

@interface NativePipeline : NSObject <NSApplicationDelegate, NSWindowDelegate> {
  NSWindow *window;
  CAMetalLayer *metalLayer;
  id<MTLDevice> device;
  Resources *resources;
}
@end

@implementation NativePipeline
- (NSDictionary *)snapshot {
  NSScreen *screen = [window screen];
  NSNumber *displayID = [[screen deviceDescription] objectForKey:@"NSScreenNumber"];
  CGDisplayModeRef mode = displayID ? CGDisplayCopyDisplayMode([displayID unsignedIntValue]) : nullptr;
  double refresh = mode ? CGDisplayModeGetRefreshRate(mode) : 0;
  if (mode) CGDisplayModeRelease(mode);
  NSSize content = [[window contentView] bounds].size;
  return @{ @"display_sync_enabled": @([metalLayer displaySyncEnabled]),
    @"maximum_drawable_count": @([metalLayer maximumDrawableCount]),
    @"framebuffer_only": @([metalLayer framebufferOnly]),
    @"presents_with_transaction": @([metalLayer presentsWithTransaction]),
    @"allows_next_drawable_timeout": @([metalLayer allowsNextDrawableTimeout]),
    @"feedback_handler_installed": @NO, @"window_visible": @([window isVisible]),
    @"window_occlusion_visible": @(([window occlusionState] & NSWindowOcclusionStateVisible) != 0),
    @"window_key": @([window isKeyWindow]), @"app_active": @([NSApp isActive]),
    @"window_client_width_points": @(content.width), @"window_client_height_points": @(content.height),
    @"drawable_width_pixels": @([metalLayer drawableSize].width),
    @"drawable_height_pixels": @([metalLayer drawableSize].height),
    @"backing_scale_factor": @([window backingScaleFactor]),
    @"display_id": displayID ?: (id)[NSNull null], @"configured_refresh_hz": @(refresh),
    @"configured_refresh_available": @(refresh > 0),
    @"screen_maximum_frames_per_second": @(screen ? [screen maximumFramesPerSecond] : 0) };
}
- (BOOL)settingsValid:(NSDictionary *)s {
  return ![[s objectForKey:@"display_sync_enabled"] boolValue] &&
    [[s objectForKey:@"maximum_drawable_count"] unsignedIntValue] == Slots &&
    ![[s objectForKey:@"framebuffer_only"] boolValue] &&
    ![[s objectForKey:@"presents_with_transaction"] boolValue] &&
    [[s objectForKey:@"allows_next_drawable_timeout"] boolValue] &&
    [[s objectForKey:@"drawable_width_pixels"] unsignedIntValue] == Width &&
    [[s objectForKey:@"drawable_height_pixels"] unsignedIntValue] == Height &&
    [[s objectForKey:@"window_visible"] boolValue] &&
    [[s objectForKey:@"window_occlusion_visible"] boolValue];
}
- (void)finish:(NSDictionary *)report status:(int)status {
  NSError *error = nil;
  NSData *json = [NSJSONSerialization dataWithJSONObject:report options:0 error:&error];
  if (!json || [json length] >= 65535) {
    json = [@"{\"status\":\"failed\",\"error\":\"invalid_or_oversized_json\"}"
        dataUsingEncoding:NSUTF8StringEncoding]; status = 1;
  }
  if (!emitted.exchange(true)) {
    fwrite([json bytes], 1, [json length], stdout); fputc('\n', stdout); fflush(stdout);
  }
  processStatus = status;
  [window orderOut:nil]; [window close]; [NSApp stop:nil];
  [NSApp postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint
      modifierFlags:0 timestamp:0 windowNumber:0 context:nil subtype:0 data1:0 data2:0] atStart:NO];
}
- (BOOL)windowShouldClose:(NSWindow *)sender {
  (void)sender; stopped.store(true); return YES;
}
- (void)measure {
  NSMutableArray *cases = [NSMutableArray array];
  NSString *failure = nil;
  const auto visibilityDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  for (;;) {
    __block NSDictionary *actual = nil;
    dispatch_sync(dispatch_get_main_queue(), ^{ actual = [[self snapshot] retain]; });
    bool ready = [self settingsValid:actual];
    [actual release];
    if (ready) break;
    if (stopped.load()) { failure = @"window_closed_during_startup"; break; }
    if (std::chrono::steady_clock::now() >= visibilityDeadline) {
      failure = @"initial_visibility_or_settings_timeout"; break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  const bool splitOrder[4] = {false, true, true, false};
  for (unsigned index = 0; index < 8 && !failure; ++index) {
    @autoreleasepool {
      /* Same identities give C/D the same image content for each workload. */
      uint32_t nextIdentity = 1;
      bool split = splitOrder[index % 4], success = true;
      unsigned iterations = index < 4 ? 1 : 4096;
      __block NSDictionary *before = nil, *after = nil;
      dispatch_sync(dispatch_get_main_queue(), ^{ before = [[self snapshot] retain]; });
      if (![self settingsValid:before]) failure = @"settings_or_visibility_readback_before";
      NSMutableDictionary *result = [NSMutableDictionary dictionaryWithDictionary:@{
        @"case_index": @(index), @"cpu_mode": split ? @"decoupled" : @"coupled",
        @"workload": index < 4 ? @"light" : @"heavy", @"integer_iterations": @(iterations),
        @"before": before }];
      [before release];
      if (!failure) {
        NSDictionary *warm = runPhase(resources, metalLayer, WarmFrames, iterations,
            nextIdentity, split, false, &success);
        nextIdentity += WarmFrames;
        [result setObject:warm forKey:@"warmup"];
        if (!success) failure = [warm objectForKey:@"error"];
      }
      if (!failure) {
        /* Warmup workers and all callbacks are fully drained before this timer. */
        NSDictionary *measured = runPhase(resources, metalLayer, MeasuredFrames, iterations,
            nextIdentity, split, corruptFrame && index == 0, &success);
        nextIdentity += MeasuredFrames;
        [result setObject:measured forKey:@"measured"];
        if (!success) failure = [measured objectForKey:@"error"];
      }
      dispatch_sync(dispatch_get_main_queue(), ^{ after = [[self snapshot] retain]; });
      [result setObject:after forKey:@"after"];
      if (!failure && ![self settingsValid:after]) failure = @"settings_or_visibility_readback_after";
      [after release];
      [result setObject:failure ? @"failed" : @"complete" forKey:@"status"];
      if (failure) [result setObject:failure forKey:@"error"];
      [cases addObject:result];
      /* Keep failure alive beyond this per-case autorelease pool. */
      if (failure) [failure retain];
    }
  }
  NSDictionary *report = @{ @"schema": @"native-present-pipeline-v1",
    @"status": failure ? @"failed" : @"complete", @"error": failure ?: (id)[NSNull null],
    @"device": [device name], @"cases": cases, @"case_order": @"light_C_D_D_C_heavy_C_D_D_C",
    @"warmup_frames_per_case": @(WarmFrames), @"measured_frames_per_case": @(MeasuredFrames),
    @"gpu_queues": @2, @"shared_render_targets": @1, @"snapshot_textures": @(Slots),
    @"snapshot_buffers": @(Slots), @"private_texture_bytes": @(ImageBytes * (Slots + 1)),
    @"ring_capacity": @(Slots), @"snapshot_storage": @"shared", @"offscreen_storage": @"private",
    @"snapshot_bytes_per_slot": @(ImageBytes), @"corrupt_frame_requested": @(corruptFrame),
    @"deadline_seconds": @(DeadlineSeconds), @"cpu_wait_bound_seconds": @3,
    @"physical_scanout_measured": @NO, @"presented_handler_installed": @NO,
    @"gpu_dependency": @"render_shared_target_copy_to_slot_signal_event_present_queue_wait_copy_to_drawable_readback_present",
    @"only_experimental_variable": @"one_coupled_cpu_worker_or_two_decoupled_cpu_workers" };
  dispatch_sync(dispatch_get_main_queue(), ^{ [self finish:report status:failure ? 1 : 0]; });
  [failure release];
}
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
  (void)notification;
  device = MTLCreateSystemDefaultDevice(); resources = new Resources;
  if (device) {
    resources->renderQueue = [device newCommandQueue]; resources->presentQueue = [device newCommandQueue];
    resources->device = device;
  }
  NSError *error = nil;
  id<MTLLibrary> library = device ? [device newLibraryWithSource:shaderSource() options:nil error:&error] : nil;
  id<MTLFunction> function = [library newFunctionWithName:@"tile"];
  resources->pipeline = function ? [device newComputePipelineStateWithFunction:function error:&error] : nil;
  [function release]; [library release];
  MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
      width:Width height:Height mipmapped:NO];
  [descriptor setStorageMode:MTLStorageModePrivate];
  [descriptor setUsage:MTLTextureUsageShaderWrite];
  resources->renderTarget = [device newTextureWithDescriptor:descriptor];
  [descriptor setUsage:MTLTextureUsageUnknown];
  bool allocated = resources->renderTarget != nil;
  for (unsigned slot = 0; slot < Slots; ++slot) {
    resources->textures[slot] = [device newTextureWithDescriptor:descriptor];
    resources->snapshots[slot] = [device newBufferWithLength:ImageBytes options:MTLResourceStorageModeShared];
    if (!resources->textures[slot] || !resources->snapshots[slot]) allocated = false;
  }
  if (!device || !resources->renderQueue || !resources->presentQueue ||
      !resources->pipeline || !allocated || [resources->pipeline maxTotalThreadsPerThreadgroup] < 64) {
    [self finish:@{ @"schema": @"native-present-pipeline-v1", @"status": @"failed",
        @"error": @"metal_resource_or_pipeline_unavailable",
        @"detail": error ? [error localizedDescription] : (id)[NSNull null] } status:1]; return;
  }
  window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, Width, Height)
      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
      backing:NSBackingStoreBuffered defer:NO];
  [window setReleasedWhenClosed:NO]; [window setDelegate:self];
  [window setTitle:@"Native present pipeline (45 second bound)"];
  metalLayer = [[CAMetalLayer alloc] init];
  [metalLayer setDevice:device]; [metalLayer setPixelFormat:MTLPixelFormatBGRA8Unorm];
  [metalLayer setFramebufferOnly:NO]; [metalLayer setAllowsNextDrawableTimeout:YES];
  [metalLayer setDisplaySyncEnabled:NO]; [metalLayer setMaximumDrawableCount:Slots];
  [metalLayer setPresentsWithTransaction:NO]; [metalLayer setContentsScale:1];
  [metalLayer setDrawableSize:CGSizeMake(Width, Height)];
  NSView *view = [window contentView]; [view setWantsLayer:YES]; [view setLayer:metalLayer];
  [metalLayer setFrame:[view bounds]];
  [window center]; [window makeKeyAndOrderFront:nil]; [NSApp activateIgnoringOtherApps:YES];
  [CATransaction flush];
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    @autoreleasepool { [self measure]; }
  });
}
- (void)dealloc {
  [window setDelegate:nil]; [window release]; [metalLayer release];
  delete resources; [device release]; [super dealloc];
}
@end

int main(int argc, const char *argv[]) {
  @autoreleasepool {
    if (argc == 2 && std::strcmp(argv[1], "--corrupt-frame") == 0) corruptFrame = true;
    else if (argc != 1) {
      fputs("{\"schema\":\"native-present-pipeline-v1\",\"status\":\"failed\",\"error\":\"usage_optional_corrupt_frame_only\"}\n", stdout);
      return 1;
    }
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, DeadlineSeconds * NSEC_PER_SEC),
        dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
      if (!emitted.exchange(true)) {
        const char message[] = "{\"schema\":\"native-present-pipeline-v1\",\"status\":\"failed\",\"error\":\"45_second_watchdog\"}\n";
        (void)write(STDOUT_FILENO, message, sizeof(message) - 1);
      }
      _exit(124);
    });
    [NSApplication sharedApplication]; [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    NativePipeline *delegate = [[NativePipeline alloc] init];
    [NSApp setDelegate:delegate]; [NSApp run]; [NSApp setDelegate:nil]; [delegate release];
  }
  return processStatus;
}
