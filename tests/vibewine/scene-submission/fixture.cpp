// Production methods are inserted verbatim by run.py. Queue/window/Metal
// adapters expose ordering and ownership only; this fixture is not a renderer.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#define STDMETHODCALLTYPE
#define DXMT_HOTPATCHABLE
#define D3D11_ASSERT(x) require((x), "production context-state assertion")
using UINT = unsigned;
using HRESULT = int;
using HANDLE = void *;
using HMONITOR = int;
constexpr HRESULT S_OK=0, E_FAIL=-1, DXGI_ERROR_INVALID_CALL=-2, DXGI_STATUS_OCCLUDED=1;
constexpr UINT DXGI_PRESENT_TEST=1, DXGI_SWAP_EFFECT_SEQUENTIAL=1;
constexpr bool FALSE=false;
enum D3D11_CONTEXT_TYPE {D3D11_CONTEXT_TYPE_ALL};
enum class CommandBufferState {Idle};
struct DXGI_PRESENT_PARAMETERS {};
struct DXGI_SWAP_CHAIN_DESC1 {UINT Width=64, Height=64, SwapEffect=1;};
using d3d11_device_mutex = std::recursive_mutex;
namespace dxmt {using mutex = std::mutex;}
static void require(bool ok, const char *message) {if (!ok) throw std::runtime_error(message);}
struct Hooks {
  std::function<void(bool,uint64_t)> begin_chunk, end_chunk;
  std::function<void()> acquire;
} hooks;
struct Journal {
  std::vector<std::string> events;
  bool admitted=false;
  bool require_admission=true;
  int credit_releases=0, waits=0, advances=0, boundaries=0, semaphores=0, statistics=0;
  uint64_t logical_frame=4;
  void note(std::string s) {events.push_back(std::move(s));}
} journal;
struct CpuFence {
  void wait(uint64_t) {journal.waits++; journal.admitted=true; journal.note("admit");}
  void signal(uint64_t) {journal.credit_releases++; journal.note("credit");}
};
namespace cs2trace {
struct ApiScope {
  uint32_t gen=0; int result=0;
  template<class... T> ApiScope(T...) {}
  template<class F> HRESULT invoke(F &&f) {return f();}
};
struct Present {template<class... T> Present(T...) {}};
struct Scope {int type; uint64_t object;};
enum class CommitReason {Present,ExplicitFlush};
struct ReasonScope {template<class... T> ReasonScope(T...) {}};
uint32_t generation() {return 0;}
uint64_t now() {return 0;}
template<class... T> void record(T...) {}
}
namespace wsi {
bool minimized=false, foreground=true;
bool isMinimized(int) {return minimized;}
bool isForeground(int) {return foreground;}
int getWindowMonitor(int) {return 1;}
}
struct Texture {uint32_t width() {return 64;} uint32_t height() {return 64;}};
struct Backbuffer {std::shared_ptr<Texture> texture() {return value;} std::shared_ptr<Texture> value=std::make_shared<Texture>();};
struct Configuration {};
struct PresentationState {std::shared_ptr<Configuration> configuration;};
struct Presenter {
  bool valid=true;
  void changeGammaRamp(int) {}
  PresentationState synchronizeLayerProperties() {
    journal.note("configuration");
    return {valid ? std::make_shared<Configuration>() : nullptr};
  }
};
struct MTLDXGIOutput {int GetGammaRamp() {return 0;}};
struct Target {
  MTLDXGIOutput *ptr() {return nullptr;}
  explicit operator bool() {return false;}
};
enum class ScalerType {Spatial};
struct ScalerInfo {ScalerType type; uint32_t input_width,input_height,output_width,output_height;};
struct Statistics {ScalerInfo last_scaler_info{};};
struct Queue;
struct ArgumentEncodingContext {
  Queue *owner;
  template<class F> void updateFrameStatistics(F f) {Statistics stats; f(stats);}
  template<class... T> void upscale(T...) {journal.note("upscale");}
  template<class... T> void present(T...) {
    journal.note("acquire");
    if (hooks.acquire) hooks.acquire();
  }
  Queue &queue() {return *owner;}
  uint64_t currentFrameId() {return journal.logical_frame;}
};
struct Command {
  virtual void execute(ArgumentEncodingContext &)=0;
  virtual ~Command()=default;
};
template<class F> struct LambdaCommand : Command {
  F f;
  LambdaCommand(F &&fn) : f(std::move(fn)) {}
  void execute(ArgumentEncodingContext &ctx) override {f(ctx);}
};
struct Chunk {
  uint64_t signal_frame_latency_fence_=~0ull, frame=journal.logical_frame;
  bool scene=false;
  std::vector<std::unique_ptr<Command>> commands;
  template<class F> void emitcc(F &&f) {
    journal.note("append-present");
    commands.push_back(std::make_unique<LambdaCommand<F>>(std::forward<F>(f)));
  }
};
struct Queue {
  std::unique_ptr<Chunk> current=std::make_unique<Chunk>();
  std::vector<std::unique_ptr<Chunk>> submitted;
  Statistics statistics;
  uint64_t traceSerial() {return 1;}
  uint64_t CurrentFrameSeq() {return journal.logical_frame+1;}
  uint64_t GetCurrentEventSeqId() {return 1;}
  int GetSharedEventListener() {return 1;}
  int event=0;
  uint32_t GetMaxLatency() {return 3;}
  Chunk *CurrentChunk() {return current.get();}
  void CommitCurrentChunk() {
    require(journal.credit_releases==0, "frame admission released before final retirement");
    journal.note(current->commands.empty() ? "submit-scene" : "submit-present");
    if (current->commands.empty() && journal.require_admission) require(journal.admitted, "scene submitted before admission");
    submitted.push_back(std::move(current)); current=std::make_unique<Chunk>();
  }
  uint64_t AdvanceFrame() {journal.advances++; return journal.logical_frame++;}
  void PresentBoundary(uint64_t, uint32_t) {journal.boundaries++; journal.note("boundary");}
  void execute(size_t index) {
    auto &chunk=*submitted.at(index);
    const bool present=!chunk.commands.empty();
    if (hooks.begin_chunk) hooks.begin_chunk(present,index);
    ArgumentEncodingContext ctx{this};
    for (auto &cmd : chunk.commands) cmd->execute(ctx);
    if (hooks.end_chunk) hooks.end_chunk(present,index);
  }
  void retire(size_t index) {submitted.at(index)->commands.clear();}
};
void ReleaseSemaphore(int,int,void *) {journal.semaphores++;}
void MTLSharedEvent_setWin32EventAtValue(int,int,HANDLE,uint64_t) {}
struct ContextBase {
  virtual void Flush()=0;
  virtual void Flush1(D3D11_CONTEXT_TYPE,HANDLE)=0;
  virtual void PrepareFlush()=0;
  virtual void Commit()=0;
};
struct Context : ContextBase {
  struct State {Queue &cmd_queue; bool has_dirty_op_since_last_event=false;} ctx_state;
  Queue &cmd_queue;
  d3d11_device_mutex mutex;
  bool promote_flush=false;
  CommandBufferState cmdbuf_state=CommandBufferState::Idle;
  explicit Context(Queue &q) : ctx_state{q}, cmd_queue(q) {}
  bool InvalidateCurrentPass(bool = false) {journal.note("close-pass"); return false;}
  // FLUSH
  // FLUSH1
  // PREPARE_FLUSH
  // COMMIT
};
struct Device {
  Queue q;
  d3d11_device_mutex mutex;
  Device &GetDXMTDevice() {return *this;}
  Queue &queue() {return q;}
};
struct SwapchainBase {virtual HRESULT Present1(UINT,UINT,const DXGI_PRESENT_PARAMETERS *)=0;};
template<bool EnableMetalFX> struct Swapchain : SwapchainBase {
  Device *device_;
  Context *device_context_;
  Presenter *presenter;
  uint64_t trace_serial_=1, presentation_count_=4;
  int hWnd=1, present_semaphore_=1, metalfx_scaler=1;
  bool handle_alt_tab_=false;
  struct {bool Windowed=true;} fullscreen_desc_;
  Target target_;
  dxmt::mutex buffer_mutex_;
  DXGI_SWAP_CHAIN_DESC1 desc_;
  std::shared_ptr<Backbuffer> backbuffer_=std::make_shared<Backbuffer>();
  std::shared_ptr<Backbuffer> upscaled_backbuffer_=std::make_shared<Backbuffer>();
  std::unique_ptr<CpuFence> frame_latency_fence_=std::make_unique<CpuFence>();
  uint64_t frame_latency=1;
  void SetFullscreenState(bool,void *) {}
  bool NeedsDisplayTimingRefresh(HMONITOR) {return false;}
  void RefreshDisplayTiming(HMONITOR) {}
  double PresentationDuration(UINT) {return 0;}
  void UpdateStatistics(const Statistics &,uint64_t) {journal.statistics++;}
  // SYNC_FRAME_STATE
  // SYNC_FRAME
  // PRESENT
};

#ifndef SCENE_SUBMISSION_FIXTURE_NO_MAIN
template<bool FX> static void scenario(bool dirty,bool promote,bool valid,int failure) {
  journal=Journal{}; hooks=Hooks{}; wsi::minimized=false;
  Device device; Context context(device.q); Presenter presenter;
  Swapchain<FX> swap{ };
  swap.device_=&device; swap.device_context_=&context; swap.presenter=&presenter;
  context.ctx_state.has_dirty_op_since_last_event=dirty;
  context.promote_flush=promote; presenter.valid=valid;
  device.q.current->scene=dirty || promote;
  if (failure==1) swap.backbuffer_.reset();
  if (failure==2) wsi::minimized=true;
  if (failure==4) swap.frame_latency_fence_.reset();
  if (failure==2 || failure==4) journal.require_admission=false;
  const HRESULT result=swap.Present1(failure==5 ? 5 : 0,failure==3 ? DXGI_PRESENT_TEST : 0,nullptr);
  const bool occluded=failure==2;
  const bool rejected=!valid || failure==1 || failure==3 || failure==5;
  if (rejected) {
    require(device.q.submitted.empty(),"failed/test Present submitted work");
    require(journal.waits==0 && journal.advances==0 && journal.boundaries==0 && journal.credit_releases==0,"failed/test Present consumed admission");
    require(result==(failure==3 ? S_OK : failure==1 || failure==5 ? DXGI_ERROR_INVALID_CALL : E_FAIL),"wrong failure result");
    return;
  }
  if (occluded) {
    require(result==DXGI_STATUS_OCCLUDED && device.q.submitted.size()==1,"occluded flush changed");
    require(journal.waits==0 && journal.advances==0 && journal.credit_releases==0,"occluded admission changed");
    return;
  }
  require(result==S_OK,"valid Present failed");
  require(journal.waits==(failure==4 ? 0 : 1),"wrong logical admission count");
  require(journal.advances==1 && journal.boundaries==1 && swap.presentation_count_==5,"logical frame counted twice");
  require(journal.credit_releases==0,"token released before presentation retirement");
  const bool split=DXMT_SCENE_SUBMISSION && (dirty || promote);
  require(device.q.submitted.size()==(split ? 2u : 1u),"wrong split/no-empty submission grouping");
  for (auto &chunk : device.q.submitted) require(chunk->frame==4,"split changed logical capture identity");
  if (split) {
    require(device.q.submitted[0]->signal_frame_latency_fence_==~0ull,"scene chunk owns final frame completion");
    require(device.q.submitted[0]->commands.empty(),"scene chunk consumed final presentation");
    device.q.execute(0); device.q.retire(0);
    require(journal.credit_releases==0,"scene retirement returned final credit");
  }
  require(device.q.submitted.back()->signal_frame_latency_fence_==5,"missing final latency fence");
  hooks.acquire=[&] {require(journal.credit_releases==0,"credit returned before acquire");};
  device.q.execute(device.q.submitted.size()-1);
  require(journal.credit_releases==0,"encoding returned token before retirement");
  device.q.retire(device.q.submitted.size()-1);
  require(journal.credit_releases==(failure==4 ? 0 : 1),"final retirement failed to return exactly one credit");
  require(journal.semaphores==1 && journal.statistics==1,"presentation callback duplicated/lost");
}
int main() {
  try {
    for (int fx=0;fx<2;fx++) for (int dirty=0;dirty<2;dirty++) for (int promote=0;promote<2;promote++) {
      for (int failure=0;failure<6;failure++) {
        if (fx) scenario<true>(dirty,promote,true,failure); else scenario<false>(dirty,promote,true,failure);
      }
      if (fx) scenario<true>(dirty,promote,false,0); else scenario<false>(dirty,promote,false,0);
    }
    std::puts("56 actual-production Present/Flush admission, ownership and failure cases passed");
  } catch (const std::exception &e) {std::fprintf(stderr,"%s\n",e.what()); return 1;}
}
#endif
