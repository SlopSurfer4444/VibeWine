#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>
#include "winemetal.h"
#include "util_flags.hpp"

using UINT = uint32_t;
using LONG = int32_t;
#define STDMETHODCALLTYPE
#define ERR(...) ((void)0)
struct D3D11_VIEWPORT { float TopLeftX, TopLeftY, Width, Height, MinDepth, MaxDepth; };
struct D3D11_RECT { LONG left, top, right, bottom; };
static void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
struct IMTLD3D11RasterizerState {
  bool scissor = false;
  unsigned queries = 0;
  virtual ~IMTLD3D11RasterizerState() = default;
  virtual bool IsScissorEnabled() { ++queries; return scissor; }
  void SetupRasterizerState(wmtcmd_render_setrasterizerstate &cmd) { cmd.depth_bias = 2; }
};
using ID3D11RasterizerState = IMTLD3D11RasterizerState;
template<class T> struct Cast {
  T *value;
  explicit operator bool() const { return value; }
  T *ptr() const { return value; }
  T *operator->() const { return value; }
};
template<class T> Cast<T> com_cast(T *value) { return {value}; }
struct IMTLD3D11DepthStencilState {
  obj_handle_t GetDepthStencilState(uint8_t flags) { return 55 + flags; }
};
using Packet = std::variant<wmtcmd_render_setdsso, wmtcmd_render_setrasterizerstate,
                            wmtcmd_render_setblendcolor, wmtcmd_render_setviewports,
                            wmtcmd_render_setscissorrects>;
struct ArgumentEncodingContext {
  struct Encoder { uint8_t dsv_planar_flags = 2; } encoder;
  std::vector<Packet> packets;
  Encoder *currentRenderEncoder() { return &encoder; }
  template<class T> T &encodeRenderCommand() { packets.emplace_back(T{}); return std::get<T>(packets.back()); }
};
template<class T> struct Data {
  std::shared_ptr<std::vector<T>> values;
  explicit operator bool() const { return bool(values); }
  T *data() const { return values->data(); }
  size_t size() const { return values->size(); }
  T &operator[](size_t i) { return (*values)[i]; }
};
enum class PipelineStage { Vertex, Pixel, Hull, Domain, Geometry };
enum class PipelineKind { Ordinary, Tessellation, Geometry };
enum class DrawCallStatus { Invalid, Ordinary, Tessellation, Geometry };
enum class CommandBufferState { RenderPipelineReady, TessellationRenderPipelineReady, GeometryRenderPipelineReady };
struct Context {
  /* ENUM */
  dxmt::Flags<DirtyState> dirty_state = 0;
  struct {
    struct { bool IndexBuffer = true; } InputAssembler;
    struct {
      IMTLD3D11DepthStencilState *DepthStencilState = nullptr;
      UINT StencilRef = 3, RenderTargetWidth = 640, RenderTargetHeight = 480;
      float BlendFactor[4] = {.1f, .2f, .3f, .4f};
    } OutputMerger;
    struct {
      IMTLD3D11RasterizerState *RasterizerState = nullptr;
      UINT NumViewports = 1, NumScissorRects = 1;
      D3D11_VIEWPORT viewports[16] = {{0, 0, 640, 480, 0, 1}};
      D3D11_RECT scissor_rects[16] = {{-10, 20, 700, 450}};
    } Rasterizer;
  } state_;
  IMTLD3D11RasterizerState default_rs, enabled_rs;
  IMTLD3D11RasterizerState *default_rasterizer_state = &default_rs;
  IMTLD3D11DepthStencilState default_ds;
  IMTLD3D11DepthStencilState *default_depth_stencil_state = &default_ds;
  CommandBufferState cmdbuf_state = CommandBufferState::RenderPipelineReady;
  using mutex_t = std::mutex;
  mutex_t mutex;
  ArgumentEncodingContext enc;
  std::vector<std::function<void(ArgumentEncodingContext &)>> queued;
  bool failed = false, finalize_failed = false, allocation_failed = false;
  unsigned updates = 0, uploads = 0;
  Context() { enabled_rs.scissor = true; }
  bool CommandQueueFailed() const { return failed; }
  template<bool> DrawCallStatus FinalizeCurrentRenderPipeline() {
    return finalize_failed ? DrawCallStatus::Invalid : DrawCallStatus::Ordinary;
  }
  void UpdateVertexBuffer() { ++updates; }
  void UpdateSOTargets() { ++updates; }
  template<PipelineStage, PipelineKind> void UploadShaderStageResourceBinding() { ++uploads; }
  template<class F> void EmitST(F &&f) { queued.emplace_back(std::forward<F>(f)); }
  template<class T> Data<T> AllocateCommandData(size_t count) {
    if (allocation_failed) return {};
    return {std::make_shared<std::vector<T>>(count)};
  }
  void reopen() { /* REOPEN_DIRTY */ }
  // Explicit CPU model of the reset boundary: native ClearState and complete
  // render-pass creation are outside this fixture. The production reopen dirty
  // statement above then requests all initial packets independently.
  void clearAndReopen() {
    state_.Rasterizer.RasterizerState = nullptr;
    state_.Rasterizer.NumScissorRects = 0;
    reopen();
  }
  /* SETTERS */
  /* BASELINE */
  /* PREDRAW */
  DrawCallStatus draw() {
#ifdef PRIMARY_BASELINE
    return PreDrawBaseline<false>();
#else
    return PreDraw<false>();
#endif
  }
};
using Snapshot = std::vector<uint8_t>;
template<class T> void append(Snapshot &bytes, const T &value) {
  const auto *begin = reinterpret_cast<const uint8_t *>(&value);
  bytes.insert(bytes.end(), begin, begin + sizeof(value));
}
// Real wire structs are copied with only process-local payload pointers removed.
// The exact pointed-to wire arrays follow their packet. All reserved bytes are
// initialized to zero by the allocator, so equality compares every packet byte.
static std::vector<Snapshot> drain(Context &ctx) {
  for (auto &f : ctx.queued) f(ctx.enc);
  std::vector<Snapshot> result;
  for (auto &packet : ctx.enc.packets) {
    Snapshot bytes;
    std::visit([&](auto cmd) {
      using T = decltype(cmd);
      if constexpr (std::is_same_v<T, wmtcmd_render_setviewports>) {
        auto *values = reinterpret_cast<const WMTViewport *>(cmd.viewports.get_accessible_or_null());
        cmd.viewports.set(nullptr); append(bytes, cmd);
        for (unsigned i = 0; i < cmd.viewport_count; ++i) append(bytes, values[i]);
      } else if constexpr (std::is_same_v<T, wmtcmd_render_setscissorrects>) {
        auto *values = reinterpret_cast<const WMTScissorRect *>(cmd.scissor_rects.get_accessible_or_null());
        cmd.scissor_rects.set(nullptr); append(bytes, cmd);
        for (unsigned i = 0; i < cmd.rect_count; ++i) append(bytes, values[i]);
      } else append(bytes, cmd);
    }, packet);
    result.push_back(std::move(bytes));
  }
  ctx.enc.packets.clear(); ctx.queued.clear();
  return result;
}
static void parity(Context &candidate, Context &reference, const char *message) {
  require(candidate.draw() == reference.PreDrawBaseline<false>(), "status parity");
  require(drain(candidate) == drain(reference), message);
}
static std::vector<WMTScissorRect> scissors(Context &ctx) {
  for (auto &f : ctx.queued) f(ctx.enc);
  std::vector<WMTScissorRect> result;
  for (auto &p : ctx.enc.packets) if (auto *cmd = std::get_if<wmtcmd_render_setscissorrects>(&p)) {
    auto *values = reinterpret_cast<const WMTScissorRect *>(cmd->scissor_rects.get_accessible_or_null());
    result.insert(result.end(), values, values + cmd->rect_count);
  }
  ctx.enc.packets.clear(); ctx.queued.clear();
  return result;
}
static void stable(unsigned &candidate_queries, unsigned &reference_queries) {
  Context candidate, reference;
  candidate.reopen(); reference.reopen();
  for (unsigned i = 0; i < 1024; ++i) parity(candidate, reference, "stable packet parity");
  candidate_queries = candidate.default_rs.queries;
  reference_queries = reference.default_rs.queries;
  require(reference_queries == 1024, "baseline getter count");
  require(candidate.updates == reference.updates && candidate.uploads == reference.uploads, "resource path parity");
}
static void transitions() {
  Context c, r;
  c.reopen(); r.reopen(); parity(c, r, "initial packets");
  c.RSSetState(&c.enabled_rs); r.RSSetState(&r.enabled_rs);
  require(c.dirty_state.any(Context::DirtyState::Scissors), "enable dirties scissors");
  parity(c, r, "enable packets");
  c.RSSetState(&c.enabled_rs); r.RSSetState(&r.enabled_rs);
  require(!c.dirty_state.any(Context::DirtyState::Scissors), "repeat preserves clean scissors");
  parity(c, r, "repeated enabled packets");
  D3D11_RECT rects[] = {{-10, 20, 700, 450}, {650, 500, 400, 200}};
  c.RSSetScissorRects(2, rects); r.RSSetScissorRects(2, rects);
  require(c.dirty_state.any(Context::DirtyState::Scissors), "rect update dirties scissors");
  D3D11_VIEWPORT views[] = {{0, 0, 800, 600, 0, 1}, {1, 2, 320, 240, .2f, .8f}};
  c.RSSetViewports(2, views); r.RSSetViewports(2, views);
  parity(c, r, "multiple viewport packets");
  c.RSSetState(nullptr); r.RSSetState(nullptr);
  require(c.dirty_state.any(Context::DirtyState::Scissors), "disable dirties scissors");
  parity(c, r, "default reset packets");
  views[0].Width = 123; views[0].Height = 99;
  c.RSSetViewports(1, views); r.RSSetViewports(1, views);
  require(c.dirty_state.any(Context::DirtyState::Scissors), "disabled viewport dirties scissors");
  parity(c, r, "disabled viewport packets");
  c.RSSetState(nullptr); r.RSSetState(nullptr); parity(c, r, "repeated null packets");
  c.state_.OutputMerger.RenderTargetWidth = r.state_.OutputMerger.RenderTargetWidth = 1024;
  c.state_.OutputMerger.RenderTargetHeight = r.state_.OutputMerger.RenderTargetHeight = 768;
  c.reopen(); r.reopen(); parity(c, r, "target resize/reopen packets");
  c.clearAndReopen(); r.clearAndReopen(); parity(c, r, "reset/reopen packets");
  c.cmdbuf_state = r.cmdbuf_state = CommandBufferState::GeometryRenderPipelineReady;
  c.reopen(); r.reopen(); parity(c, r, "geometry packets");
  c.cmdbuf_state = r.cmdbuf_state = CommandBufferState::TessellationRenderPipelineReady;
  c.reopen(); r.reopen(); parity(c, r, "tessellation packets");
}
static void goldens() {
  Context c;
  c.state_.Rasterizer.NumViewports = 3;
  c.state_.Rasterizer.NumScissorRects = 2;
  c.state_.Rasterizer.scissor_rects[1] = {650, 500, 400, 200};
  c.RSSetState(&c.enabled_rs);
  require(c.draw() == DrawCallStatus::Ordinary, "enabled status");
  auto a = scissors(c);
  require(a.size() == 3 && a[0].x == 0 && a[0].y == 20 && a[0].width == 640 && a[0].height == 430 &&
          a[1].x == 640 && a[1].y == 480 && a[1].width == 0 && a[1].height == 0 &&
          a[2].width == 0 && a[2].height == 0, "enabled scissors");
  c.RSSetState(nullptr);
  c.state_.OutputMerger.RenderTargetWidth = c.state_.OutputMerger.RenderTargetHeight = 0;
  c.state_.Rasterizer.viewports[0].Width = 123; c.state_.Rasterizer.viewports[0].Height = 99;
  require(c.draw() == DrawCallStatus::Ordinary, "disabled status");
  auto b = scissors(c);
  require(b.size() == 3 && b[0].width == 123 && b[0].height == 99 && b[2].width == 123, "disabled fallback scissors");
}
static void failures() {
  Context c, r;
  c.failed = r.failed = true; parity(c, r, "queue failure packets");
  require(c.default_rs.queries == 0 && c.updates == 0, "queue failure admission");
  c.failed = r.failed = false; c.finalize_failed = r.finalize_failed = true;
  parity(c, r, "pipeline failure packets");
  c.finalize_failed = r.finalize_failed = false; c.reopen(); r.reopen();
  c.allocation_failed = r.allocation_failed = true;
  // Viewport allocation failure now returns before a scissor query, which is
  // intentionally no longer observable outside the scissor serialization path.
  parity(c, r, "allocation failure packets");
  require(c.dirty_state.any(Context::DirtyState::Scissors), "failed dirty state retained");
  c.allocation_failed = r.allocation_failed = false;
  parity(c, r, "allocation recovery packets");
  c.state_.InputAssembler.IndexBuffer = false;
  auto before = c.default_rs.queries;
  require(c.PreDraw<true>() == DrawCallStatus::Invalid && c.default_rs.queries == before, "missing index admission");
}
int main() {
  try {
    unsigned candidate = 0, reference = 0;
    stable(candidate, reference);
    goldens(); transitions(); failures();
    // After independent packet/data controls pass, the immutable baseline must
    // fail this precise work-saving assertion, never compilation or sanitizers.
    require(candidate == 1, "stable getter economy");
    std::cout << "PASS 4 groups; stable IsScissorEnabled calls " << reference << " -> " << candidate
              << "; exact wire packet and payload parity\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL " << e.what() << '\n'; return 3;
  }
}
