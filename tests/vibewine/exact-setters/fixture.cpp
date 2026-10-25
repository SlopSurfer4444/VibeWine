#include <array>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "com/com_pointer.hpp"
#include "rc/util_rc_ptr.hpp"
#include "dxmt_binding_set.hpp"
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (0)
#define STDMETHODCALLTYPE
#define ERR(...) ++errors
#define D3D11_ASSERT(x) CHECK(x)
using UINT = unsigned;
using UINT8 = uint8_t;
using BOOL = bool;
using HRESULT = long;
constexpr HRESULT S_OK = 0;
constexpr UINT D3D11_BIND_VERTEX_BUFFER = 1, D3D11_BIND_CONSTANT_BUFFER = 2, D3D11_BIND_SHADER_RESOURCE = 4;
constexpr unsigned D3D11_MAP_WRITE_DISCARD = 0;
using namespace dxmt;
struct RefObject : IUnknown {
  unsigned refs = 0;
  bool convertible = true;
  unsigned AddRef() override { return ++refs; }
  unsigned Release() override { CHECK(refs); return --refs; }
  void AddRefPrivate() { AddRef(); }
  void ReleasePrivate() { Release(); }
  long QueryInterface(int, void **out) override { *out = convertible ? this : nullptr; if (*out) AddRef(); return *out ? 0 : -1; }
};
struct RcObject { unsigned refs = 0; void incRef() { ++refs; } void decRef() { CHECK(refs); --refs; } };
enum class PipelineStage { Vertex, Pixel, Geometry, Hull, Domain, Compute };
enum class PipelineKind { Ordinary, Geometry, Tessellation };
struct InputLayout { uint32_t mask = 1; uint32_t input_slot_mask() { return mask; } };
struct ID3D11InputLayout : RefObject { InputLayout layout; InputLayout *GetManagedInputLayout() { return &layout; } };
using IMTLD3D11InputLayout = ID3D11InputLayout;
struct IMTLD3D11Shader : RefObject {};
using ID3D11VertexShader = IMTLD3D11Shader;
struct ID3D11ClassInstance {};
struct Allocation : RcObject {
  uint64_t address = 0;
  unsigned sub = 0;
  void useSuballocation(unsigned value) { sub = value; }
  uint64_t currentSuballocationOffset() { return sub * 64; }
  uint64_t gpuAddress() { return address; }
};
using BufferAllocation = Allocation;
struct Buffer : RcObject {
  Rc<Allocation> allocation;
  Allocation *current() { return allocation.ptr(); }
  unsigned length() { return 1024; }
  Rc<Allocation> rename(Rc<Allocation> &&next) { auto old = std::move(allocation); allocation = std::move(next); return old; }
};
struct D3D11ResourceCommon : RefObject {
  Buffer storage;
  bool allowed = true;
  UINT flags = D3D11_BIND_VERTEX_BUFFER;
  UINT bindFlags() { return flags; }
  bool hazardsFree() { return true; }
  Rc<Buffer> buffer() { return &storage; }
};
using ID3D11Buffer = D3D11ResourceCommon;
struct DynamicBuffer : RcObject {
  Buffer *buffer;
  std::vector<std::unique_ptr<Allocation>> allocations;
  Rc<Allocation> immediate;
  unsigned next_sub = 0;
  explicit DynamicBuffer(Buffer *value) : buffer(value) {}
  unsigned nextSuballocation() { return std::exchange(next_sub, 0); }
  Rc<Allocation> allocate(uint64_t) { auto value = std::make_unique<Allocation>(); value->address = 0x10000 + allocations.size() * 0x1000; auto *ptr = value.get(); allocations.push_back(std::move(value)); return ptr; }
  void updateImmediateName(uint64_t, Rc<Allocation> next, unsigned, bool) { immediate = std::move(next); }
  void updateImmediateName(uint64_t, Rc<Allocation> next, bool) { immediate = std::move(next); }
  Rc<Allocation> immediateName() { return immediate; }
  void *immediateMappedMemory() { return this; }
};
struct D3D11_MAPPED_SUBRESOURCE { void *pData; UINT RowPitch, DepthPitch; };
#include "production-vb.hpp"
struct DirtySet { bool dirty = false; void set_dirty() { dirty = true; } bool any_dirty() { return dirty; } bool any_bound() { return false; } };
struct StageState { Com<IMTLD3D11Shader> Shader; DirtySet ConstantBuffers, SRVs, Samplers; };
struct Stages : std::array<StageState, 6> { StageState &operator[](PipelineStage stage) { return data()[unsigned(stage)]; } };
struct State {
  struct { Com<IMTLD3D11InputLayout> InputLayout; BindingSet<VERTEX_BUFFER_B, 32> VertexBuffers; } InputAssembler;
  Stages ShaderStages;
  struct { DirtySet UAVs; } ComputeStageUAV, OutputMerger;
  struct { DirtySet Targets; } StreamOutput;
};
struct ResourceAccess { static constexpr int Read = 1; };
struct wmtcmd_render_setbufferoffset { uint64_t offset; unsigned index, type; };
constexpr unsigned SM50_BINDING_INDEX_VERTEX_BUFFER = 0, WMTRenderCommandSetObjectBufferOffset = 1, WMTRenderCommandSetVertexBufferOffset = 2;
struct Entry { uint64_t address; uint32_t stride, length; };
struct ArgumentEncodingContext {
  struct VB { Rc<Buffer> buffer; unsigned offset = 0, stride = 0; };
  std::array<VB, 32> vbuf_;
  unsigned full_binds = 0, offset_binds = 0, uploads = 0, tracks = 0, clears = 0, ends = 0;
  wmtcmd_render_setbufferoffset command{};
  std::function<Entry(unsigned)> read_entry;
  template<class T> T *getMappedArgumentBuffer(uint64_t) {
    static std::array<T, 32> data{};
    read_entry = [](unsigned index) { Entry out{}; static_assert(sizeof(out) == sizeof(T)); std::memcpy(&out, &data[index], sizeof(out)); return out; };
    return data.data();
  }
  template<class T> T &encodeRenderCommand() { ++uploads; return command; }
  uint64_t getFinalArgumentBufferOffset(uint64_t value) { return value; }
  template<PipelineStage, PipelineKind> void makeResident(Buffer *) {}
  template<PipelineStage> void trackBuffer(Allocation *, int) { ++tracks; }
  void clearState() { vbuf_ = {}; ++clears; }
  void endPass() { ++ends; }
  template<class T> void pushDeferredVisibilityQueries(T *) {}
  void popDeferredVisibilityQueries() {}
  /* NATIVE_METHODS */
  void bindVertexBuffer(unsigned slot, unsigned offset, unsigned stride, Rc<Buffer> &&buffer) {
    ++full_binds; bindVertexBufferImpl(slot, offset, stride, std::move(buffer));
  }
  void bindVertexBufferOffset(unsigned slot, unsigned offset, unsigned stride) {
    ++offset_binds; bindVertexBufferOffsetImpl(slot, offset, stride);
  }
};
template<class T> Rc<T> forward_rc(Rc<T> &value) { return std::move(value); }
namespace cs2trace { enum class CommitReason { PassPromotion }; struct ReasonScope { ReasonScope(CommitReason, unsigned, bool) {} }; }
struct VisibilityResultQuery : RcObject, RefObject { void DoDeferredQuery(VisibilityResultQuery *) {} };
struct Staging : RcObject { void useCopySource(uint64_t) {} void useCopyDestination(uint64_t) {} };
struct ID3D11CommandList : RefObject {};
struct MTLD3D11CommandList final : ID3D11CommandList {
  bool promote_flush = false;
  unsigned visibility_query_count = 0;
  struct UsedDynamic { bool latest; Rc<DynamicBuffer> buffer, texture; Rc<Allocation> allocation; unsigned suballocation; };
  std::vector<UsedDynamic> used_dynamic_buffers, used_dynamic_lineartextures;
  std::vector<std::pair<Com<VisibilityResultQuery>, unsigned>> issued_visibility_query;
  std::vector<Com<VisibilityResultQuery>> issued_event_query, issued_timestamp_query;
  std::vector<Rc<Staging>> read_staging_resources, written_staging_resources;
  std::vector<std::function<void(ArgumentEncodingContext &)>> commands;
  template<class F> void Emit(F &&fn) { commands.emplace_back(std::forward<F>(fn)); }
  void Execute(ArgumentEncodingContext &enc) { for (auto &command : commands) command(enc); }
};
struct FixtureDevice {
  std::vector<std::unique_ptr<MTLD3D11CommandList>> owned;
  void CreateCommandList(ID3D11CommandList **out) {
    auto value = std::make_unique<MTLD3D11CommandList>(); value->AddRef(); *out = value.get(); owned.push_back(std::move(value));
  }
};
struct Context {
  using mutex_t = std::recursive_mutex;
  using d3d11_device_mutex = mutex_t;
  mutex_t mutex;
  State state_;
  ArgumentEncodingContext enc;
  unsigned errors = 0, validations = 0, emitted = 0, encoder_opens = 0;
  FixtureDevice device_storage;
  FixtureDevice *device = &device_storage;
  bool recording_deferred = false;
  std::vector<std::function<void(ArgumentEncodingContext &)>> commands;
  /* COMMAND_STATE */
  CommandBufferState cmdbuf_state = CommandBufferState::Idle, previous_render_pipeline_state = CommandBufferState::Idle;
  template<class F> void EmitST(F &&fn) { ++emitted; if (recording_deferred) ctx_state.current_cmdlist->Emit(std::forward<F>(fn)); else commands.emplace_back(std::forward<F>(fn)); }
  template<class F> void EmitOP(F &&fn) { EmitST(std::forward<F>(fn)); }
  void flush() { for (auto &command : commands) command(enc); commands.clear(); }
  bool ValidateIAHazard(D3D11ResourceCommon *resource) { ++validations; return resource->allowed; }
  D3D11ResourceCommon *GetResourceCommon(ID3D11Buffer *resource) { return resource; }
  uint64_t PreAllocateArgumentBuffer(unsigned, unsigned) { return 0; }
  bool Commit() { return false; }
  bool promote_flush = false;
  void *allocated_encoder_argbuf_size_ = nullptr;
  struct {
    struct { unsigned traceSerial() { return 0; } uint64_t CurrentSeqId() { return 1; } } cmd_queue;
    Com<MTLD3D11CommandList> current_cmdlist;
    std::vector<std::pair<unsigned, std::pair<Com<VisibilityResultQuery>, unsigned>>> building_visibility_queries;
    std::vector<unsigned> current_dynamic_buffer_allocations, current_dynamic_texture_allocations;
  } ctx_state;
  Context() { device->CreateCommandList((ID3D11CommandList **)&ctx_state.current_cmdlist); }
  template<class T> std::vector<T> AllocateCommandData(size_t count) { return std::vector<T>(count); }
  template<class T> void End(T *) {}
  /* CONTEXT_METHODS */
  // Production preamble through all independent binding dirty marks; native
  // render-target/pass creation is outside this native fixture's scope.
  bool SwitchToRenderEncoder() { /* REOPEN_DIRTY */ ++encoder_opens; cmdbuf_state = CommandBufferState::RenderEncoderActive; return true; }
  void RestoreEncodingContextState() { /* RESTORE_VERTEX */ }
  void execute_boundary(bool RestoreContextState) {
    ResetEncodingContextState(); flush();
    // A command list may replace the encoding state; restoring uses the actual
    // production VB restoration block, independently of any setter repeat.
    enc.vbuf_[0] = {};
    /* EXECUTE_RESTORE */
  }
  void finish_boundary(bool RestoreDeferredContextState) {
    ResetEncodingContextState(); flush();
    /* FINISH_RESTORE */
  }
  void MapDiscard(Rc<DynamicBuffer> &dynamic) {
    UINT bind_flag = D3D11_BIND_VERTEX_BUFFER, buffer_length = 1024;
    uint64_t current_seq_id = 1, coherent_seq_id = 0;
    D3D11_MAPPED_SUBRESOURCE mapped{}, *pMappedResource = &mapped;
    switch (D3D11_MAP_WRITE_DISCARD) { /* DISCARD_CASE */ }
  }
};
static void vb(Context &ctx, D3D11ResourceCommon *resource, UINT stride = 16, UINT offset = 32) {
  ID3D11Buffer *buffers[] = {resource}; ctx.IASetVertexBuffers(0, 1, buffers, &stride, &offset);
}
static void ready(Context &ctx) { ctx.cmdbuf_state = Context::CommandBufferState::RenderPipelineReady; }
static void layout_repeat() {
  ID3D11InputLayout first, next; Context ctx;
  ctx.IASetInputLayout(&first); ready(ctx);
  const auto refs = first.refs;
  unsigned invalidations = 0;
  for (unsigned i = 0; i < 100; ++i) {
    ready(ctx); ctx.IASetInputLayout(&first);
    invalidations += ctx.cmdbuf_state == Context::CommandBufferState::RenderEncoderActive;
  }
  std::cout << "COUNT layout-repeat invalidations=" << invalidations << '\n';
  CHECK(invalidations == 0);
  CHECK(first.refs == refs);
  ctx.IASetInputLayout(&next); CHECK(ctx.cmdbuf_state == Context::CommandBufferState::RenderEncoderActive);
  ready(ctx); ctx.IASetInputLayout(nullptr); CHECK(ctx.cmdbuf_state == Context::CommandBufferState::RenderEncoderActive);
  ready(ctx); ctx.IASetInputLayout(nullptr); CHECK(ctx.cmdbuf_state == Context::CommandBufferState::RenderPipelineReady);
  ready(ctx); first.convertible = false; ctx.IASetInputLayout(&first);
  CHECK(!ctx.state_.InputAssembler.InputLayout && ctx.cmdbuf_state == Context::CommandBufferState::RenderPipelineReady);
}
static void shader_repeat() {
  IMTLD3D11Shader shader; Context ctx;
  ready(ctx);
  unsigned invalidations = 0;
  for (unsigned i = 0; i < 100; ++i) {
    ready(ctx); ctx.SetShader<PipelineStage::Pixel>(static_cast<IMTLD3D11Shader *>(nullptr), nullptr, 0);
    invalidations += ctx.cmdbuf_state == Context::CommandBufferState::RenderEncoderActive;
  }
  std::cout << "COUNT null-shader-repeat invalidations=" << invalidations << '\n';
  CHECK(invalidations == 0);
  ctx.SetShader<PipelineStage::Pixel>(&shader, nullptr, 0);
  CHECK(ctx.cmdbuf_state == Context::CommandBufferState::RenderEncoderActive);
  ready(ctx); ctx.SetShader<PipelineStage::Pixel>(&shader, nullptr, 0);
  CHECK(ctx.cmdbuf_state == Context::CommandBufferState::RenderPipelineReady);
  ctx.SetShader<PipelineStage::Pixel>(static_cast<IMTLD3D11Shader *>(nullptr), nullptr, 0);
  CHECK(ctx.cmdbuf_state == Context::CommandBufferState::RenderEncoderActive);
  ready(ctx); ctx.SetShader<PipelineStage::Pixel>(static_cast<IMTLD3D11Shader *>(nullptr), nullptr, 1);
  CHECK(ctx.errors == 1 && ctx.cmdbuf_state == Context::CommandBufferState::RenderEncoderActive);
  ctx.cmdbuf_state = Context::CommandBufferState::ComputePipelineReady;
  ctx.SetShader<PipelineStage::Compute>(static_cast<IMTLD3D11Shader *>(nullptr), nullptr, 0);
  CHECK(ctx.cmdbuf_state == Context::CommandBufferState::ComputePipelineReady);
}
static void vb_repeat() {
  D3D11ResourceCommon resource; Context ctx;
  vb(ctx, &resource); ctx.flush(); ctx.state_.InputAssembler.VertexBuffers.clear_dirty();
  const auto before = ctx.emitted;
  for (unsigned i = 0; i < 100; ++i) vb(ctx, &resource);
  std::cout << "COUNT vb-repeat emitted-state-commands=" << ctx.emitted - before << '\n';
  CHECK(ctx.emitted == before && ctx.validations == 101);
  CHECK(!ctx.state_.InputAssembler.VertexBuffers.any_dirty());
  vb(ctx, &resource, 20, 32); vb(ctx, &resource, 20, 36); ctx.flush();
  CHECK(ctx.enc.offset_binds == 2 && ctx.enc.vbuf_[0].stride == 20 && ctx.enc.vbuf_[0].offset == 36);
  CHECK(ctx.state_.InputAssembler.VertexBuffers.test_dirty(0));
  ID3D11Buffer *buffers[] = {&resource};
  const auto unchanged = ctx.emitted; ctx.IASetVertexBuffers(0, 1, buffers, nullptr, nullptr);
  CHECK(ctx.emitted == unchanged && ctx.state_.InputAssembler.VertexBuffers.test_dirty(0));
  resource.allowed = false; vb(ctx, &resource); ctx.flush();
  CHECK(!ctx.state_.InputAssembler.VertexBuffers.test_bound(0) && !ctx.enc.vbuf_[0].buffer);
  const auto unbound = ctx.emitted; vb(ctx, nullptr); CHECK(ctx.emitted == unbound);
}
static void lifecycle() {
  Allocation initial; initial.address = 0x1000;
  ID3D11InputLayout layout; D3D11ResourceCommon resource; resource.storage.allocation = &initial; Context ctx;
  ctx.IASetInputLayout(&layout); vb(ctx, &resource); ctx.flush();
  ctx.state_.InputAssembler.VertexBuffers.clear_dirty();
  ctx.SwitchToRenderEncoder(); CHECK(ctx.state_.InputAssembler.VertexBuffers.test_dirty(0));
  const auto before = ctx.emitted; vb(ctx, &resource); CHECK(ctx.emitted == before);
  ready(ctx); ctx.UpdateVertexBuffer(); ctx.flush(); CHECK(ctx.enc.uploads == 1);
  ctx.InvalidateCurrentPass(); ctx.flush(); ctx.SwitchToRenderEncoder();
  vb(ctx, &resource); ready(ctx); ctx.UpdateVertexBuffer(); ctx.flush(); CHECK(ctx.enc.uploads == 2);
  for (bool restore : {true, false}) {
    vb(ctx, &resource); ctx.flush(); ctx.execute_boundary(restore); ctx.flush();
    CHECK(bool(ctx.state_.InputAssembler.VertexBuffers.test_bound(0)) == restore);
    if (restore) { CHECK(ctx.enc.vbuf_[0].buffer.ptr() == &resource.storage); const auto n = ctx.emitted; vb(ctx, &resource); CHECK(ctx.emitted == n); }
    vb(ctx, &resource); ctx.flush(); ctx.finish_boundary(restore); ctx.flush();
    CHECK(bool(ctx.state_.InputAssembler.VertexBuffers.test_bound(0)) == restore);
    if (restore) CHECK(ctx.enc.vbuf_[0].buffer.ptr() == &resource.storage);
  }
  ctx.ClearState(); ctx.flush(); CHECK(!ctx.state_.InputAssembler.VertexBuffers.any_bound() && !ctx.enc.vbuf_[0].buffer);
  vb(ctx, &resource); ctx.flush(); CHECK(ctx.enc.vbuf_[0].buffer.ptr() == &resource.storage);
  const auto n = ctx.emitted; ctx.ClearState(); ctx.IASetInputLayout(nullptr);
  CHECK(ctx.emitted > n); ctx.flush(); CHECK(!ctx.enc.vbuf_[0].buffer);
}
static void discard() {
  ID3D11InputLayout layout; D3D11ResourceCommon resource; DynamicBuffer dynamic(&resource.storage); Context ctx;
  Rc<DynamicBuffer> handle(&dynamic);
  ctx.IASetInputLayout(&layout); vb(ctx, &resource); ctx.flush();
  ready(ctx); ctx.MapDiscard(handle); const auto before = ctx.emitted;
  vb(ctx, &resource); CHECK(ctx.emitted == before && ctx.state_.InputAssembler.VertexBuffers.test_dirty(0));
  ctx.UpdateVertexBuffer(); ctx.flush(); auto first = ctx.enc.read_entry(0);
  CHECK(first.address == dynamic.immediate->gpuAddress() + 32 && first.stride == 16 && first.length == 992);
  ctx.MapDiscard(handle); vb(ctx, &resource); ctx.UpdateVertexBuffer(); ctx.flush();
  auto second = ctx.enc.read_entry(0); CHECK(second.address != first.address && second.address == dynamic.immediate->gpuAddress() + 32);
  dynamic.next_sub = 2; ctx.MapDiscard(handle); vb(ctx, &resource); ctx.UpdateVertexBuffer(); ctx.flush();
  CHECK(ctx.enc.read_entry(0).address == second.address + 128 && ctx.enc.uploads == 3 && ctx.enc.tracks == 3);
  resource.storage.allocation = nullptr;
}
static void controls() {
  ID3D11InputLayout layout, other_layout;
  IMTLD3D11Shader shader, other_shader;
  D3D11ResourceCommon first, second, third;
  Context ctx;
  ctx.IASetInputLayout(&layout); ready(ctx); ctx.IASetInputLayout(&other_layout);
  CHECK(ctx.cmdbuf_state == Context::CommandBufferState::RenderEncoderActive);
  ready(ctx); ctx.IASetInputLayout(nullptr);
  CHECK(!ctx.state_.InputAssembler.InputLayout && ctx.cmdbuf_state == Context::CommandBufferState::RenderEncoderActive);
  ctx.IASetInputLayout(&layout); ready(ctx); other_layout.convertible = false; ctx.IASetInputLayout(&other_layout);
  CHECK(!ctx.state_.InputAssembler.InputLayout && ctx.cmdbuf_state == Context::CommandBufferState::RenderEncoderActive);
  ctx.SetShader<PipelineStage::Vertex>(&shader, nullptr, 0); ready(ctx);
  ctx.SetShader<PipelineStage::Vertex>(&shader, nullptr, 0);
  CHECK(ctx.cmdbuf_state == Context::CommandBufferState::RenderPipelineReady);
  ctx.SetShader<PipelineStage::Vertex>(&other_shader, nullptr, 0);
  CHECK(ctx.cmdbuf_state == Context::CommandBufferState::RenderEncoderActive);
  ready(ctx); ctx.SetShader<PipelineStage::Vertex>(static_cast<IMTLD3D11Shader *>(nullptr), nullptr, 0);
  CHECK(ctx.cmdbuf_state == Context::CommandBufferState::RenderEncoderActive);
  ready(ctx); ctx.SetShader<PipelineStage::Vertex>(static_cast<IMTLD3D11Shader *>(nullptr), nullptr, 1);
  CHECK(ctx.errors == 1 && ctx.cmdbuf_state == Context::CommandBufferState::RenderEncoderActive);
  vb(ctx, &first); ctx.flush(); CHECK(ctx.enc.full_binds == 1);
  vb(ctx, &first, 20, 32); ctx.flush(); CHECK(ctx.enc.offset_binds == 1 && ctx.enc.vbuf_[0].stride == 20);
  vb(ctx, &first, 20, 40); ctx.flush(); CHECK(ctx.enc.offset_binds == 2 && ctx.enc.vbuf_[0].offset == 40);
  vb(ctx, &second, 20, 40); ctx.flush(); CHECK(ctx.enc.full_binds == 2 && ctx.enc.vbuf_[0].buffer.ptr() == &second.storage);
  vb(ctx, nullptr); ctx.flush(); CHECK(ctx.enc.full_binds == 3 && !ctx.enc.vbuf_[0].buffer);
  const auto unbound = ctx.emitted; vb(ctx, nullptr); CHECK(ctx.emitted == unbound);
  ID3D11Buffer *buffers[] = {&first, &second, &third};
  UINT strides[] = {16, 24, 32}, offsets[] = {0, 8, 16};
  ctx.IASetVertexBuffers(0, 3, buffers, strides, offsets); ctx.flush();
  ctx.state_.InputAssembler.VertexBuffers.clear_dirty();
  strides[1] = 28;
  const auto updates = ctx.enc.offset_binds;
  ctx.IASetVertexBuffers(0, 3, buffers, strides, offsets); ctx.flush();
  // Both candidates must retain exactly the one changed slot's semantic state.
  CHECK(ctx.enc.vbuf_[0].stride == 16 && ctx.enc.vbuf_[1].stride == 28 && ctx.enc.vbuf_[2].stride == 32);
  CHECK(!ctx.state_.InputAssembler.VertexBuffers.test_dirty(0) && ctx.state_.InputAssembler.VertexBuffers.test_dirty(1) && !ctx.state_.InputAssembler.VertexBuffers.test_dirty(2));
  std::cout << "COUNT mixed-three-slots emitted-offset-commands=" << ctx.enc.offset_binds - updates << '\n';
  first.allowed = false; vb(ctx, &first); ctx.flush();
  CHECK(!ctx.state_.InputAssembler.VertexBuffers.test_bound(0) && !ctx.enc.vbuf_[0].buffer);
}
static void actual_callers() {
  ID3D11InputLayout layout; D3D11ResourceCommon resource, other;
  Context ctx;
  for (bool restore : {true, false}) {
    ctx.IASetInputLayout(&layout); vb(ctx, &resource); ctx.flush();
    Com<ID3D11CommandList> list;
    ctx.device->CreateCommandList(&list);
    auto *commands = static_cast<MTLD3D11CommandList *>(list.ptr());
    commands->Emit([&other](ArgumentEncodingContext &enc) { auto buffer = other.buffer(); enc.bindVertexBuffer(0, 2, 4, std::move(buffer)); enc.clearState(); });
    ctx.ExecuteCommandList(list.ptr(), restore); ctx.flush();
    CHECK(bool(ctx.state_.InputAssembler.VertexBuffers.test_bound(0)) == restore);
    CHECK(bool(ctx.enc.vbuf_[0].buffer) == restore);
    if (restore) {
      CHECK(ctx.enc.vbuf_[0].buffer.ptr() == &resource.storage && ctx.enc.vbuf_[0].offset == 32 && ctx.enc.vbuf_[0].stride == 16);
      const auto before = ctx.emitted; vb(ctx, &resource); CHECK(ctx.emitted == before);
    }
    ctx.recording_deferred = true;
    vb(ctx, &resource);
    Com<ID3D11CommandList> finished;
    ctx.FinishCommandList(restore, &finished);
    CHECK(finished.ptr() != ctx.ctx_state.current_cmdlist.ptr());
    CHECK(bool(ctx.state_.InputAssembler.VertexBuffers.test_bound(0)) == restore);
    if (restore) {
      CHECK(ctx.ctx_state.current_cmdlist->commands.size() == 1);
      ctx.enc.clearState(); ctx.ctx_state.current_cmdlist->Execute(ctx.enc);
      CHECK(ctx.enc.vbuf_[0].buffer.ptr() == &resource.storage && ctx.enc.vbuf_[0].offset == 32 && ctx.enc.vbuf_[0].stride == 16);
      const auto before = ctx.emitted; vb(ctx, &resource); CHECK(ctx.emitted == before);
    } else CHECK(ctx.ctx_state.current_cmdlist->commands.empty());
    ctx.recording_deferred = false;
  }
}
int main(int argc, char **argv) {
  if (argc != 2) return 3;
  const std::string test = argv[1];
  try {
    if (test == "layout-repeat") layout_repeat();
    else if (test == "shader-repeat") shader_repeat();
    else if (test == "vb-repeat") vb_repeat();
    else if (test == "lifecycle") lifecycle();
    else if (test == "discard") discard();
    else if (test == "controls") controls();
    else if (test == "actual-callers") actual_callers();
    else return 3;
    std::cout << "PASS " << test << '\n'; return 0;
  } catch (const std::exception &error) { std::cerr << "FAIL " << test << ": " << error.what() << '\n'; return 2; }
}
