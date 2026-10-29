// Generated method bodies are copied verbatim by run.py. Metal/COM dependencies
// below expose access ordering; they do not model GPU correctness.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#define STDMETHODCALLTYPE
#define unlikely(x) (x)
#define likely(x) (x)
#define ERR(...) ((void)0)

struct Attempt {
  std::mutex mutex;
  std::condition_variable cv;
  bool spinning = false, finished = false;
};
thread_local Attempt *attempt = nullptr;
// Substitute only the processor pause hint with an observable yield. The real
// compare-exchange, owner loop, acquire/release ordering and recursion stay intact.
#define DXMT_ARCH_X86
#define _mm_pause fixture_pause
static void fixture_pause() {
  if (attempt) {
    std::lock_guard guard(attempt->mutex);
    attempt->spinning = true;
    attempt->cv.notify_one();
  }
  std::this_thread::yield();
}
namespace dxmt {
namespace this_thread {
uint32_t get_id() {
  static std::atomic<uint32_t> next{1};
  thread_local uint32_t id = next.fetch_add(1);
  return id;
}
}
// Visibility only: inspect real ownership without replacing mutex operations.
#define private public
// MUTEX_DECL
#undef private
// MUTEX_IMPL
// NULL_MUTEX
}
using dxmt::d3d11_device_mutex;
using dxmt::null_mutex;

static void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
static std::function<void()> access = [] {};
template<class T> struct Rc : std::shared_ptr<T> {
  using Base = std::shared_ptr<T>;
  using Base::Base;
  Rc() = default;
  Rc(T *p) : Base(p) {}
  Rc &operator=(T *p) { this->reset(p); return *this; }
};
enum WMTPixelFormat {
  WMTPixelFormatInvalid, WMTPixelFormatRG16Uint, WMTPixelFormatRG16Float,
  WMTPixelFormatRG16Sint, WMTPixelFormatRG16Snorm, WMTPixelFormatRG16Unorm,
  WMTPixelFormatRG32Uint, WMTPixelFormatRG32Float, WMTPixelFormatRG32Sint,
  WMTPixelFormatRGBA16Sint, WMTPixelFormatRGBA16Snorm, WMTPixelFormatRGBA16Uint,
  WMTPixelFormatRGBA16Unorm, WMTPixelFormatRGBA16Float
};
enum {WMTTextureType2D, WMTTextureUsageShaderRead = 1, WMTTextureUsageShaderWrite = 2,
      WMTResourceStorageModePrivate = 3, kIntrapassControlBitIgnoreUAVWaW = 1};
enum class TextureAllocationFlag {GpuPrivate};
template<class T> struct Flags { void set(T) {} };
struct WMTTextureInfo {
  uint32_t width, height, depth, array_length, mipmap_level_count;
  WMTPixelFormat pixel_format;
  uint32_t sample_count, type, usage, options;
};
struct WMTFXTemporalScalerInfo {
  WMTPixelFormat color_format, output_format, depth_format, motion_format;
  bool auto_exposure;
  uint32_t input_width, input_height, output_width, output_height;
  bool input_content_properties_enabled;
  float input_content_min_scale, input_content_max_scale;
  bool requires_synchronous_initialization;
};
struct WMTFXTemporalScalerProps {
  uint32_t input_content_width, input_content_height;
  bool reset, reversed;
  float motion_vector_scale_x, motion_vector_scale_y, jitter_x, jitter_y, exposure;
};
struct TemporalScaler { TemporalScaler(int, const WMTFXTemporalScalerInfo &) { access(); } };
struct Texture {
  WMTPixelFormat format = WMTPixelFormatRG16Float;
  int fullView = 1;
  Texture() = default;
  Texture(const WMTTextureInfo &, int) { access(); }
  uint32_t width() { access(); return 64; }
  uint32_t height() { access(); return 64; }
  WMTPixelFormat pixelFormat() { access(); return format; }
  int allocate(Flags<TextureAllocationFlag>) { access(); return 0; }
  void rename(int) { access(); }
  struct View {WMTPixelFormat format; int type, firstMiplevel, miplevelCount, firstArraySlice, arraySize;};
  int createView(View) { return 1; }
};
struct D3D11ResourceCommon {
  Rc<Texture> value = new Texture;
  Rc<Texture> texture() { access(); return value; }
};
struct MTL_TEMPORAL_UPSCALE_D3D11_DESC {
  D3D11ResourceCommon *Color, *Output, *Depth, *MotionVector, *ExposureTexture = nullptr;
  bool AutoExposure = true, MotionVectorInDisplayRes = false;
  uint32_t InputContentWidth = 64, InputContentHeight = 64;
  bool InReset = false, DepthReversed = false;
  float MotionVectorScaleX = 1, MotionVectorScaleY = 1, JitterOffsetX = 0, JitterOffsetY = 0, PreExposure = 1;
};
enum class ScalerType {Temporal};
struct ScalerInfo {
  ScalerType type;
  bool auto_exposure;
  uint32_t input_width, input_height, output_width, output_height;
  bool motion_vector_highres;
};
struct Statistics {ScalerInfo last_scaler_info;};
struct ArgumentEncodingContext {
  std::vector<int> controls;
  void setIntrapassBarrierControl(int value) { controls.push_back(value); }
  template<class F> void updateFrameStatistics(F fn) { Statistics stats{}; fn(stats); }
  struct Motion { template<class... T> void dispatch(T &&...) {} } mv_scale_cmd;
  template<class... T> void upscaleTemporal(T &&...) {}
};
struct Device {int GetMTLDevice() { access(); return 0; }};
namespace cs2trace {
enum class CommitReason {GPUIdle};
struct ReasonScope {ReasonScope(CommitReason, uint64_t) {}};
struct SyncScope {uint64_t gen; int type; uint64_t object, queue, frame; ~SyncScope() {}};
uint64_t operationGeneration(uint64_t) { return 0; }
}
struct Queue {
  int waits = 0, shutdowns = 0;
  bool healthy = true, joined = false;
  std::vector<int> *order = nullptr;
  uint64_t traceSerial() { return 0; }
  uint64_t CurrentSeqId() { access(); if (order) order->push_back(0); return 7; }
  uint64_t CurrentFrameSeq() { return 1; }
  bool WaitCPUFence(uint64_t seq) {
    access(); require(seq == 7, "wrong fence"); ++waits;
    if (order) order->push_back(3);
    return healthy;
  }
  void ShutdownAndJoin() {
    access(); require(!healthy && waits > 0, "shutdown without failed wait");
    require(order && order->back() == 3, "shutdown before fence result");
    ++shutdowns; joined = true; order->push_back(4);
  }
};
template<class Mutex> struct Context {
  using device_mutex_t = Mutex;
  Mutex mutex;
  Device owned_device;
  Device *device = &owned_device;
  ArgumentEncodingContext enc;
  int invalidations = 0, emissions = 0;
  std::vector<int> dependency_order;
  bool InvalidateCurrentPass() { access(); dependency_order.push_back(1); invalidations++; return false; }
  template<class F> void EmitOP(F fn) { access(); emissions++; fn(enc); }
};
struct WaitBase {virtual void WaitUntilGPUIdle() = 0; virtual ~WaitBase() = default;};
struct Immediate : WaitBase, Context<d3d11_device_mutex> {
  Queue cmd_queue;
  int commits = 0;
  Immediate() { cmd_queue.order = &dependency_order; }
  void Commit() { access(); dependency_order.push_back(2); commits++; }
  // WAIT_IDLE
};
struct ExtBase {
  virtual void TemporalUpscale(const MTL_TEMPORAL_UPSCALE_D3D11_DESC *) = 0;
  virtual void BeginUAVOverlap() = 0;
  virtual void EndUAVOverlap() = 0;
  virtual ~ExtBase() = default;
};
template<class ContextInternalState> struct Extension : ExtBase {
  ContextInternalState *ctx_;
  explicit Extension(ContextInternalState *ctx) : ctx_(ctx) {}
  // CACHE_DECL
  std::vector<CachedTemporalScaler> scaler_cache_;
  // FORMAT_METHOD
  // EXT_METHODS
};

// A worker must reach either the real lock's contended spin loop or finish.
// There is no sleep-based assertion: the main thread owns the mutex until that
// handshake, and only then releases it. Production owner_ is the access oracle.
template<class F> bool contended(d3d11_device_mutex &mutex, F fn) {
  Attempt state;
  std::atomic<bool> touched{false};
  bool unlocked = false;
  access = [&] {
    touched.store(true);
    require(mutex.owner_.load() == dxmt::this_thread::get_id(), "unlocked production operation");
  };
  mutex.lock();
  std::thread worker([&] {
    attempt = &state;
    try { fn(); } catch (const std::runtime_error &) { unlocked = true; }
    attempt = nullptr;
    std::lock_guard guard(state.mutex);
    state.finished = true;
    state.cv.notify_one();
  });
  bool handshake;
  {
    std::unique_lock guard(state.mutex);
    handshake = state.cv.wait_for(guard, std::chrono::seconds(3), [&] { return state.spinning || state.finished; });
  }
  const bool blocked = !touched.load();
  mutex.unlock();
  worker.join();
  access = [] {};
  require(handshake, "worker did not reach observed seam");
  require(unlocked || state.spinning, "worker did not block on owned mutex");
  require(unlocked || blocked, "operation ran before release");
  require(touched.load(), "operation never reached dependency");
  return unlocked;
}

int main(int argc, char **) {
  try {
    const bool baseline = argc > 1;
    Immediate ctx;
    ctx.mutex.set_protected(true);
    Extension<Immediate> ext(&ctx);
    D3D11ResourceCommon color, output, depth, motion;
    MTL_TEMPORAL_UPSCALE_D3D11_DESC desc{&color, &output, &depth, &motion};
    int unlocked = 0;
    for (auto operation : {std::function<void()>([&] { ctx.WaitUntilGPUIdle(); }),
                           std::function<void()>([&] { ext.BeginUAVOverlap(); }),
                           std::function<void()>([&] { ext.EndUAVOverlap(); }),
                           std::function<void()>([&] { ext.TemporalUpscale(&desc); })}) {
      const bool observed = contended(ctx.mutex, operation);
      require(observed == baseline, "unexpected lock behavior");
      unlocked += observed;
    }
    require(unlocked == (baseline ? 4 : 0), "not all entries observed");
    if (!baseline) {
      // Existing outer locks must retain ownership after recursive entries return.
      access = [&] { require(ctx.mutex.owner_.load() == dxmt::this_thread::get_id(), "recursive access unlocked"); };
      ctx.mutex.lock();
      ctx.WaitUntilGPUIdle(); ext.BeginUAVOverlap(); ext.EndUAVOverlap();
      ext.TemporalUpscale(&desc); // cache hit
      desc.MotionVectorInDisplayRes = true;
      ext.TemporalUpscale(&desc); // new scaler + downscale texture
      color.value.reset(); ext.TemporalUpscale(&desc); // early return releases one level
      require(ctx.mutex.counter_ == 1, "recursive/early-return lock imbalance");
      ctx.mutex.unlock();
      require(ctx.mutex.owner_.load() == 0, "mutex leaked");
      require(ctx.cmd_queue.waits == 2 && ctx.commits == 2, "idle sequencing changed");
      require(ctx.enc.controls == std::vector<int>({1, 0, 1, 0}), "UAV commands changed");
      access = [] {};
      Context<null_mutex> deferred;
      Extension<Context<null_mutex>> deferred_ext(&deferred);
      color.value = new Texture;
      deferred_ext.BeginUAVOverlap(); deferred_ext.EndUAVOverlap(); deferred_ext.TemporalUpscale(&desc);
      require(deferred.emissions == 3, "deferred commands changed");
      // Disabled protection remains a no-op, without changing entry behavior.
      ctx.mutex.set_protected(false);
      ext.BeginUAVOverlap(); ext.EndUAVOverlap(); ctx.WaitUntilGPUIdle();
      require(ctx.cmd_queue.shutdowns == 0 && !ctx.cmd_queue.joined, "healthy wait shut down queue");
      // The full production failed-fence guard runs under the real contended
      // mutex; every queue endpoint asserts ownership through access().
      Immediate failed;
      failed.mutex.set_protected(true);
      failed.cmd_queue.healthy = false;
      require(!contended(failed.mutex, [&] { failed.WaitUntilGPUIdle(); }), "failed wait was unlocked");
      require(failed.cmd_queue.waits == 1 && failed.cmd_queue.shutdowns == 1 && failed.cmd_queue.joined,
              "failed wait did not shut down and join");
      require(failed.invalidations == 1 && failed.commits == 1 &&
              failed.dependency_order == std::vector<int>({0, 1, 2, 3, 4}), "failed idle sequencing changed");
      require(failed.mutex.owner_.load() == 0, "failed wait leaked mutex");
    }
    std::printf("context-locks PASS: %s; unlocked_entries=%d\n",
                baseline ? "baseline exposes all four unlocked entries" : "contention, recursion, cache, early return, deferred, disabled", unlocked);
  } catch (const std::exception &error) {
    std::fprintf(stderr, "context-locks FAIL: %s\n", error.what()); return 1;
  }
}
