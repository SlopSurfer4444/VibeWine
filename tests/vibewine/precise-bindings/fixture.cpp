// Production code replaces the marked lines at test time. Platform/Metal/COM
// dependencies below are CPU adapters; all allocations and recorded references
// use the production intrusive Rc and all binding sets use production bitsets.
#include <array>
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "rc/util_rc_ptr.hpp"
#include "dxmt_binding_set.hpp"
#include "airconv_public.h"

#define ERR(...) ((void)0)
#define DXMT_UNREACHABLE throw std::runtime_error("unexpected shader binding");
static unsigned checks = 0, scenarios = 0;
static void require(bool condition, const char *message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}

namespace dxmt {
using UINT = unsigned;
using HRESULT = int;
constexpr HRESULT S_OK = 0, E_INVALIDARG = -1;
enum D3D11_MAP { D3D11_MAP_READ = 1, D3D11_MAP_WRITE, D3D11_MAP_READ_WRITE,
                 D3D11_MAP_WRITE_DISCARD, D3D11_MAP_WRITE_NO_OVERWRITE };
constexpr unsigned D3D11_BIND_VERTEX_BUFFER = 1, D3D11_BIND_CONSTANT_BUFFER = 4,
                   D3D11_BIND_SHADER_RESOURCE = 8;
struct D3D11_MAPPED_SUBRESOURCE { void *pData = nullptr; UINT RowPitch = 0, DepthPitch = 0; };
struct D3D11_BUFFER_DESC { UINT ByteWidth = 256; };
using mutex = std::mutex;
enum class BufferAllocationFlag {};
template <class T> struct Flags {};
struct RefCounted {
  std::atomic<unsigned> refs = 0;
  virtual ~RefCounted() = default;
  void incRef() { ++refs; }
  void decRef() { if (--refs == 0) delete this; }
};
struct BufferAllocation : RefCounted {
  static inline uint64_t next_address = 0x10000;
  uint64_t address = std::exchange(next_address, next_address + 0x10000);
  std::array<uint8_t, 768> bytes{};
  unsigned suballocation = 0;
  bool hasSuballocatoin(unsigned sub) { return sub < 3; }
  void *mappedMemory(unsigned sub) { return bytes.data() + sub * 256; }
  void useSuballocation(unsigned sub) { require(sub < 3, "suballocation range"); suballocation = sub; }
  uint64_t gpuAddress() { return address; }
  uint64_t currentSuballocationOffset() { return suballocation * 256; }
};
class Buffer : public RefCounted {
  Rc<BufferAllocation> name_ = new BufferAllocation;
public:
  Rc<BufferAllocation> current() { return name_; }
  Rc<BufferAllocation> allocate(Flags<BufferAllocationFlag>) { return new BufferAllocation; }
  Rc<BufferAllocation> rename(Rc<BufferAllocation> next) {
    auto old = std::move(name_); name_ = std::move(next); return old;
  }
  unsigned length() { return 256; }
};
} // namespace dxmt
// HELPER
namespace dxmt {
// DYNAMIC_CLASS
// DYNAMIC_METHODS
struct D3D11ResourceCommon : RefCounted {
  Rc<Buffer> gpu;
  Rc<DynamicBuffer> dynamic;
  unsigned flags;
  explicit D3D11ResourceCommon(unsigned bind = D3D11_BIND_CONSTANT_BUFFER)
      : gpu(new Buffer), dynamic(new DynamicBuffer(gpu.ptr(), {})), flags(bind) {}
  D3D11ResourceCommon(Rc<Buffer> buffer, Rc<DynamicBuffer> dyn)
      : gpu(std::move(buffer)), dynamic(std::move(dyn)), flags(D3D11_BIND_CONSTANT_BUFFER) {}
  const Rc<Buffer> &buffer() const { return gpu; }
  void GetDesc(D3D11_BUFFER_DESC *desc) { *desc = {}; }
};
using IUnknown = RefCounted;
using ID3D11Buffer = D3D11ResourceCommon;
using ID3D11Resource = D3D11ResourceCommon;
template <class T, bool = true> using Com = Rc<T>;
inline D3D11ResourceCommon *GetResourceCommon(ID3D11Resource *resource) { return resource; }
inline Rc<DynamicBuffer> GetDynamicBuffer(ID3D11Resource *resource, UINT *length, UINT *flags) {
  *length = 256; *flags = resource->flags; return resource->dynamic;
}
struct D3D11ShaderResourceView : RefCounted {
  Rc<Buffer> gpu;
  explicit D3D11ShaderResourceView(Rc<Buffer> buffer = {}) : gpu(std::move(buffer)) {}
  Rc<Buffer> buffer() const { return gpu; }
};
// OTHER_BINDINGS
// BUFFER_SLICE
// CB_BINDING
struct OtherBinding { unsigned identity = 0; };
template <> struct redundant_binding_trait<OtherBinding> {
  static bool is_redundant(const OtherBinding &a, const OtherBinding &b) { return a.identity == b.identity; }
};
enum class PipelineStage { Vertex, Pixel, Hull, Domain, Geometry, Compute };
enum class PipelineKind { Ordinary, Tessellation, Geometry };
struct ManagedShader {
  MTL_SHADER_REFLECTION ref{};
  std::vector<MTL_SM50_SHADER_ARGUMENT> cb, resources;
  explicit ManagedShader(std::initializer_list<unsigned> slots) {
    for (unsigned slot : slots) {
      cb.push_back({SM50BindingType::ConstantBuffer, slot, {}, unsigned(cb.size())});
      ref.ConstantBufferSlotMask |= 1u << slot;
    }
    ref.NumConstantBuffers = cb.size();
  }
  const MTL_SHADER_REFLECTION &reflection() { return ref; }
  const MTL_SM50_SHADER_ARGUMENT *constant_buffers_info() { return cb.data(); }
  const MTL_SM50_SHADER_ARGUMENT *arguments_info() { return resources.data(); }
  void srv(std::initializer_list<unsigned> slots, bool typed) {
    for (unsigned slot : slots) {
      MTL_SM50_SHADER_ARGUMENT arg{};
      arg.Type = SM50BindingType::SRV; arg.SM50BindingSlot = slot;
      arg.Flags = MTL_SM50_SHADER_ARGUMENT_FLAG(typed ? MTL_SM50_SHADER_ARGUMENT_TEXTURE | MTL_SM50_SHADER_ARGUMENT_TBUFFER_OFFSET : MTL_SM50_SHADER_ARGUMENT_BUFFER);
      arg.StructurePtrOffset = resources.size() * 2;
      resources.push_back(arg);
      if (slot < 64) ref.SRVSlotMaskLo |= 1ull << slot;
      else ref.SRVSlotMaskHi |= 1ull << (slot - 64);
    }
    ref.NumArguments = resources.size(); ref.ArgumentTableQwords = resources.size() * 2;
  }
};
struct Shader : RefCounted {
  ManagedShader managed;
  explicit Shader(std::initializer_list<unsigned> slots) : managed(slots) {}
  ManagedShader *GetManagedShader() { return &managed; }
};
struct StageState {
  BindingSet<CONSTANT_BUFFER_B, 14> ConstantBuffers;
  BindingSet<SRV_B, 128> SRVs;
  BindingSet<OtherBinding, 16> Samplers;
  Rc<Shader> Shader;
};
template <typename T> struct Stages : std::array<T, 6> {
  T &operator[](PipelineStage stage) { return this->data()[unsigned(stage)]; }
};
struct ContextState {
  Stages<StageState> ShaderStages;
  struct { BindingSet<VERTEX_BUFFER_B, 32> VertexBuffers; } InputAssembler;
  struct { BindingSet<OtherBinding, 64> UAVs; } ComputeStageUAV, OutputMerger;
};
// ALIGN
struct OffsetCommand { unsigned type = 0, index = 0; uint64_t offset = 0; PipelineStage stage; };
using wmtcmd_compute_setbufferoffset = OffsetCommand;
using wmtcmd_render_setbufferoffset = OffsetCommand;
enum { WMTComputeCommandSetBufferOffset, WMTRenderCommandSetObjectBufferOffset,
       WMTRenderCommandSetMeshBufferOffset, WMTRenderCommandSetVertexBufferOffset,
       WMTRenderCommandSetFragmentBufferOffset };
namespace ResourceAccess { constexpr int Read = 1; }
template <PipelineKind> unsigned GetResidencyMask(PipelineStage, bool, bool) { return 1; }
struct Texture : RefCounted {
  unsigned checkViewUseArray(uint64_t view, bool) { return view; }
  unsigned arrayLength(uint64_t) { return 1; }
};
inline uint64_t TextureMetadata(uint32_t length, float) { return uint64_t(length) << 32; }
struct ArgumentEncodingContext {
  static constexpr unsigned kSRVBindings = 128;
  struct Resource { Rc<Buffer> buffer; Rc<Texture> texture; uint64_t viewId = 0; BufferSlice slice; };
  struct Vertex { Rc<Buffer> buffer; unsigned offset = 0, stride = 0; };
  std::array<Resource, 768> resview_{};
  std::array<Vertex, 32> vbuf_{};
  // BIND_SRV
  // BIND_VB
  struct CBuffer { Rc<Buffer> buffer; unsigned offset = 0; };
  std::array<CBuffer, 84> cbuf_{};
  std::array<uint64_t, 32768> arguments{};
  std::vector<OffsetCommand> commands;
  std::vector<Rc<BufferAllocation>> retained;
  PipelineStage current_stage = PipelineStage::Vertex;
  struct { uint64_t gpu_address = 0; } dummy_cbuffer_info_;
  Buffer *dummy_cbuffer_ = nullptr;
  unsigned resource_accesses = 0, residency_checks = 0;
  std::array<unsigned, 6> access_by_stage{}, residency_by_stage{};
  // BIND_CB
  // BIND_CB_OFFSET
  template <typename T, bool = false> T *getMappedArgumentBuffer(size_t offset) {
    require(offset + 112 <= sizeof(arguments), "fixture argument buffer capacity");
    return reinterpret_cast<T *>(reinterpret_cast<uint8_t *>(arguments.data()) + offset);
  }
  template <bool = false> uint64_t getFinalArgumentBufferOffset(uint64_t offset) { return offset; }
  template <PipelineStage stage> std::pair<BufferAllocation *, uint64_t>
  access(const Rc<Buffer> &buffer, unsigned, unsigned, int flags) {
    require(flags == ResourceAccess::Read, "CB access flags");
    current_stage = stage; ++resource_accesses; ++access_by_stage[unsigned(stage)];
    retained.push_back(buffer->current());
    return {retained.back().ptr(), retained.back()->currentSuballocationOffset()};
  }
  template <PipelineStage stage, PipelineKind> void makeResident(Buffer *, unsigned = 0) {
    current_stage = stage; ++residency_checks; ++residency_by_stage[unsigned(stage)];
  }
  template <class T> T &encodeRenderCommand() {
    commands.push_back({}); commands.back().stage = current_stage; return commands.back();
  }
  template <class T> T &encodeComputeCommand() { return encodeRenderCommand<T>(); }
  template <PipelineStage stage, PipelineKind kind>
  void encodeConstantBuffers(const MTL_SHADER_REFLECTION *, const MTL_SM50_SHADER_ARGUMENT *, uint64_t);
  struct View { uint64_t gpu_resource_id; };
  template <PipelineStage stage> std::pair<View, unsigned> access(const Rc<Buffer> &buffer, uint64_t, int flags) {
    auto [allocation, sub] = access<stage>(buffer, 0, 0, flags);
    return {{allocation->gpuAddress()}, unsigned(sub / 4)};
  }
  struct TextureView { uint64_t gpuResourceID; };
  template <PipelineStage> TextureView access(const Rc<Texture> &, uint64_t, int) { throw std::runtime_error("unexpected texture access"); }
  template <PipelineStage, PipelineKind> void makeResident(Texture *, unsigned) { throw std::runtime_error("unexpected texture residency"); }
  template <PipelineStage stage, PipelineKind kind> void encodeShaderResources(const MTL_SHADER_REFLECTION *reflection,
                                                                            const MTL_SM50_SHADER_ARGUMENT *arguments, uint64_t offset) {
    auto *encoded_buffer = getMappedArgumentBuffer<uint64_t, stage == PipelineStage::Compute>(offset);
    for (unsigned i = 0; i < reflection->NumArguments; ++i) {
      auto &arg = arguments[i];
      switch (arg.Type) {
        // SRV_CASE
        default: throw std::runtime_error("unexpected argument kind");
      }
    }
    if constexpr (stage == PipelineStage::Compute)
    // SRV_TAIL
  }
  template <PipelineKind kind> void encodeVertexBuffers(uint32_t, uint64_t);
  uint64_t read(uint64_t address) {
    if (!address) return 0; // Observed dummy-CB address in this CPU adapter.
    for (const auto &allocation : retained) {
      if (address >= allocation->gpuAddress() && address + sizeof(uint64_t) <= allocation->gpuAddress() + 768) {
        uint64_t value;
        std::memcpy(&value, allocation->bytes.data() + address - allocation->gpuAddress(), sizeof(value));
        return value;
      }
    }
    throw std::runtime_error("CB address not retained");
  }
};
// ENCODE_CB
// ENCODE_VB
// Interface substitute only: native queue allocation, wake and cancellation
// ownership are checked by the queue family's production tests.
struct Queue {
  static constexpr HRESULT out_of_memory = int32_t(0x8007000e);
  std::atomic<HRESULT> failure_{0};
  HRESULT FailureReason() const { return failure_.load(std::memory_order_acquire); }
  void Fail(HRESULT reason) { HRESULT expected = 0; failure_.compare_exchange_strong(expected, reason, std::memory_order_acq_rel); }
  uint64_t CurrentSeqId() { return 9; }
  uint64_t CoherentSeqId() { return 7; }
};
// USED_DYNAMIC
struct CommandList { std::vector<used_dynamic_buffer> used_dynamic_buffers; };
struct DynamicBufferAllocation { BufferAllocation *allocation; uint32_t allocation_id, suballocation; };
template <bool replayable> struct CommonContext {
  using mutex_t = std::mutex;
  mutex_t mutex;
  ContextState state_;
  Queue cmd_queue;
  CommandList list;
  struct Internal {
    Queue &cmd_queue;
    CommandList *current_cmdlist;
    std::unordered_map<DynamicBuffer *, DynamicBufferAllocation> current_dynamic_buffer_allocations;
  } ctx_state{cmd_queue, &list, {}};
  uint64_t argument_size = 0, *allocated_encoder_argbuf_size_ = &argument_size;
  ArgumentEncodingContext enc;
  std::vector<std::function<void(ArgumentEncodingContext &)>> recorded;
  size_t executed = 0;
  bool fail_next_emit = false;
  template <class F> void EmitST(F &&fn) {
    if (std::exchange(fail_next_emit, false)) { cmd_queue.Fail(Queue::out_of_memory); return; }
    recorded.emplace_back(std::forward<F>(fn));
  }
  template <class T> static Rc<T> forward_rc(Rc<T> &value) {
    if constexpr (replayable) return value;
    else return std::move(value);
  }
  // QUEUE_FAILED
  // ALLOCATE_ARGUMENTS
  // UPLOAD
  // SET_CB
  template <PipelineStage stage> void bind(Rc<D3D11ResourceCommon> resource, unsigned slot, unsigned first = 0) {
    auto raw = resource.ptr();
    SetConstantBuffer<stage>(slot, 1, &raw, &first, nullptr);
  }
  template <PipelineStage stage> void draw(std::initializer_list<uint64_t> values, unsigned count) {
    UploadShaderStageResourceBinding<stage, PipelineKind::Ordinary>();
    EmitST([expected = std::vector<uint64_t>(values), count](ArgumentEncodingContext &encoder) {
      unsigned found = 0;
      unsigned accesses = 0, residencies = 0;
      uint64_t last_offset = 0;
      for (const auto &command : encoder.commands) {
        if (command.stage == stage) {
          ++found; last_offset = command.offset;
          for (unsigned i = 0; i < expected.size(); ++i) {
            ++residencies;
            accesses += encoder.arguments[last_offset / 8 + i] != 0;
          }
        }
      }
      require(found == count, "unnecessary or missing stage-table materialization");
      require(encoder.access_by_stage[unsigned(stage)] == accesses, "CB table access count changed");
      require(encoder.residency_by_stage[unsigned(stage)] == residencies, "CB table residency count changed");
      for (unsigned i = 0; i < expected.size(); ++i)
        require(encoder.read(encoder.arguments[last_offset / 8 + i]) == expected[i], "stale CB data/address/offset");
    });
  }
  void replay() {
    if constexpr (replayable) {
      auto cmdlist = &list;
      auto seq_id = cmd_queue.CurrentSeqId();
      // EXECUTE_DYNAMIC
      enc = {};
      for (auto &command : recorded) command(enc);
    } else {
      while (executed < recorded.size()) recorded[executed++](enc);
    }
  }
};
struct Immediate : CommonContext<false> {
  // IMM_DYNAMIC_MAP
  HRESULT map(ID3D11Resource *pResource, D3D11_MAP MapType, D3D11_MAPPED_SUBRESOURCE *pMappedResource) {
    std::lock_guard<mutex_t> lock(mutex);
    if (!pResource) return E_INVALIDARG;
    UINT buffer_length = 0, bind_flag = 0;
    auto current_seq_id = cmd_queue.CurrentSeqId(), coherent_seq_id = cmd_queue.CoherentSeqId();
    // IMM_MAP_BRANCH
    return E_INVALIDARG;
  }
};
struct Deferred : CommonContext<true> {
  // DEF_DYNAMIC_MAP
  HRESULT map(ID3D11Resource *pResource, D3D11_MAP MapType, D3D11_MAPPED_SUBRESOURCE *pMappedResource) {
    if (!pResource) return E_INVALIDARG;
    UINT buffer_length = 0, bind_flag = 0;
    // DEF_MAP_BRANCH
    return E_INVALIDARG;
  }
};
template <class Context> void fill(Context &context, const Rc<D3D11ResourceCommon> &resource, uint64_t token) {
  D3D11_MAPPED_SUBRESOURCE mapped{};
  require(context.map(resource.ptr(), D3D11_MAP_WRITE_DISCARD, &mapped) == S_OK, "DISCARD result");
  require(mapped.pData && mapped.RowPitch == 256 && mapped.DepthPitch == 256, "mapped extent");
  for (unsigned i = 0; i < 16; ++i) {
    uint64_t value = token + i;
    std::memcpy(static_cast<uint8_t *>(mapped.pData) + i * 16, &value, sizeof(value));
  }
  D3D11_MAPPED_SUBRESOURCE unchanged{};
  require(context.map(resource.ptr(), D3D11_MAP_WRITE_NO_OVERWRITE, &unchanged) == S_OK &&
          unchanged.pData == mapped.pData, "NO_OVERWRITE preserves mapped name/suballocation");
}
template <class Context> void dirty(Context &context, Buffer *mapped) {
  for (auto &stage : context.state_.ShaderStages) {
    for (unsigned slot = 0; slot < 14; ++slot) {
      bool expected = !EXPECT_PRECISE;
      if (EXPECT_PRECISE && stage.ConstantBuffers.test_bound(slot))
        expected = stage.ConstantBuffers.at(slot).Buffer->buffer().ptr() == mapped;
      require(stage.ConstantBuffers.test_dirty(slot) == expected, "resource-exact dirty stage/slot mask");
    }
  }
}
template <PipelineStage stage, class Context> void scenario(bool cross_stage_alias) {
  ++scenarios;
  constexpr PipelineStage other = stage == PipelineStage::Pixel ? PipelineStage::Vertex : PipelineStage::Pixel;
  Context context;
  Rc<D3D11ResourceCommon> a = new D3D11ResourceCommon;
  Rc<D3D11ResourceCommon> alias = new D3D11ResourceCommon(a->gpu, a->dynamic);
  Rc<D3D11ResourceCommon> b = new D3D11ResourceCommon;
  Rc<D3D11ResourceCommon> c = new D3D11ResourceCommon;
  fill(context, a, 10);
  fill(context, b, 100);
  // Establish a clean initial state independently of prior unbound maps.
  for (auto &s : context.state_.ShaderStages) s.ConstantBuffers.clear_dirty();
  context.state_.ShaderStages[stage].Shader = new Shader({0, 1, 2});
  context.state_.ShaderStages[other].Shader = cross_stage_alias ? new Shader({0, 2}) : new Shader({0});
  context.template bind<stage>(a, 0);
  context.template bind<stage>(alias, 1, 1);
  context.template bind<stage>(b, 2, 3);
  context.template bind<other>(b, 0, 4);
  if (cross_stage_alias) context.template bind<other>(alias, 2, 2);
  context.template draw<stage>({10, 11, 103}, 1);
  if (cross_stage_alias) context.template draw<other>({104, 12}, 1);
  else context.template draw<other>({104}, 1);
  context.replay();
  unsigned main_count = 1, other_count = 1;
  for (unsigned rename = 0; rename < 6; ++rename) {
    uint64_t value = 20 + rename * 10;
    fill(context, a, value);
    dirty(context, a->gpu.ptr());
    context.template draw<stage>({value, value + 1, 103}, ++main_count);
    if (cross_stage_alias) context.template draw<other>({104, value + 2}, ++other_count);
    else {
      if (!EXPECT_PRECISE) ++other_count;
      context.template draw<other>({104}, other_count);
    }
    context.replay();
  }
  fill(context, c, 700);
  dirty(context, c->gpu.ptr());
  if (!EXPECT_PRECISE) { ++main_count; ++other_count; }
  context.template draw<stage>({70, 71, 103}, main_count);
  if (cross_stage_alias) context.template draw<other>({104, 72}, other_count);
  else context.template draw<other>({104}, other_count);
  context.replay();
  // Same view/resource, different constant offset must still invalidate.
  context.template bind<stage>(alias, 1, 4);
  context.template draw<stage>({70, 74, 103}, ++main_count);
  context.replay();
  // Unbinding removes the alias from dependency scans and encodes the dummy CB.
  context.template bind<stage>({}, 1);
  context.template draw<stage>({70, 0, 103}, ++main_count);
  context.replay();
  fill(context, a, 80);
  dirty(context, a->gpu.ptr());
  context.template draw<stage>({80, 0, 103}, ++main_count);
  if (cross_stage_alias) context.template draw<other>({104, 82}, ++other_count);
  else {
    if (!EXPECT_PRECISE) ++other_count;
    context.template draw<other>({104}, other_count);
  }
  context.replay();
  context.template bind<stage>(alias, 1, 5);
  context.template draw<stage>({80, 85, 103}, ++main_count);
  context.replay();
  // The complete recorded deferred list must remain reusable after captures run.
  context.replay();
  require(context.enc.resource_accesses > 0 && context.enc.residency_checks > 0, "CB access/residency path exercised");
  // Map errors must not invalidate or rename anything.
  for (auto &s : context.state_.ShaderStages) s.ConstantBuffers.clear_dirty();
  D3D11_MAPPED_SUBRESOURCE mapped{};
  require(context.map(a.ptr(), D3D11_MAP_READ, &mapped) == E_INVALIDARG, "invalid read map");
  require(context.map(a.ptr(), D3D11_MAP_WRITE_DISCARD, nullptr) == E_INVALIDARG, "null map destination");
  require(context.map(nullptr, D3D11_MAP_WRITE_DISCARD, &mapped) == E_INVALIDARG, "null resource");
  for (auto &s : context.state_.ShaderStages) require(!s.ConstantBuffers.any_dirty(), "failed map changed CB state");
  // Existing SRV/VB invalidation is exercised independently of the CB gate.
  Rc<D3D11ResourceCommon> srv = new D3D11ResourceCommon(D3D11_BIND_SHADER_RESOURCE);
  fill(context, srv, 900);
  for (auto &s : context.state_.ShaderStages) {
    require(s.SRVs.any_dirty() == !EXPECT_PRECISE, "unbound SRV invalidation");
    require(!s.ConstantBuffers.any_dirty(), "SRV map dirtied constant buffers");
  }
  Rc<D3D11ResourceCommon> vb = new D3D11ResourceCommon(D3D11_BIND_VERTEX_BUFFER);
  fill(context, vb, 1000);
  require(context.state_.InputAssembler.VertexBuffers.any_dirty() == !EXPECT_PRECISE, "unbound VB invalidation");
}
template <class Context> void all_stages() {
  for (bool cross_stage_alias : {false, true}) {
    scenario<PipelineStage::Vertex, Context>(cross_stage_alias);
    scenario<PipelineStage::Pixel, Context>(cross_stage_alias);
    scenario<PipelineStage::Hull, Context>(cross_stage_alias);
    scenario<PipelineStage::Domain, Context>(cross_stage_alias);
    scenario<PipelineStage::Geometry, Context>(cross_stage_alias);
    scenario<PipelineStage::Compute, Context>(cross_stage_alias);
  }
}
template <class Context> void unused_slot_and_existing_dirty() {
  ++scenarios;
  Context context;
  Rc<D3D11ResourceCommon> a = new D3D11ResourceCommon, b = new D3D11ResourceCommon;
  fill(context, a, 11);
  fill(context, b, 101);
  for (auto &s : context.state_.ShaderStages) s.ConstantBuffers.clear_dirty();
  context.state_.ShaderStages[PipelineStage::Vertex].Shader = new Shader({0});
  context.template bind<PipelineStage::Vertex>(b, 0);
  context.template bind<PipelineStage::Vertex>(a, 13);
  context.template draw<PipelineStage::Vertex>({101}, 1);
  context.replay();
  fill(context, a, 21);
  dirty(context, a->gpu.ptr());
  context.template draw<PipelineStage::Vertex>({101}, EXPECT_PRECISE ? 1 : 2);
  context.replay();
  auto &bindings = context.state_.ShaderStages[PipelineStage::Vertex].ConstantBuffers;
  bindings.set_dirty(9);
  fill(context, a, 31);
  require(bindings.test_dirty(9) && bindings.test_dirty(13), "preexisting unrelated dirty bit lost");
  context.template draw<PipelineStage::Vertex>({101}, EXPECT_PRECISE ? 1 : 3);
  context.replay();
}
void interleaved_contexts() {
  ++scenarios;
  Rc<D3D11ResourceCommon> a = new D3D11ResourceCommon;
  Immediate immediate;
  Deferred deferred;
  immediate.state_.ShaderStages[PipelineStage::Vertex].Shader = new Shader({0});
  deferred.state_.ShaderStages[PipelineStage::Pixel].Shader = new Shader({0});
  fill(immediate, a, 41);
  immediate.template bind<PipelineStage::Vertex>(a, 0);
  immediate.template draw<PipelineStage::Vertex>({41}, 1);
  immediate.replay();
  fill(deferred, a, 11);
  deferred.template bind<PipelineStage::Pixel>(a, 0);
  deferred.template draw<PipelineStage::Pixel>({11}, 1);
  // Record another immediate update before publishing/replaying deferred names.
  fill(immediate, a, 51);
  immediate.template draw<PipelineStage::Vertex>({51}, 2);
  immediate.replay();
  deferred.replay();
  fill(immediate, a, 61);
  immediate.template draw<PipelineStage::Vertex>({61}, 3);
  immediate.replay();
  // Deferred captures must still name their original data, after immediate use.
  deferred.replay();
}
template <PipelineStage Stage, class Context> void resource_discard(bool typed) {
  ++scenarios;
  Context ctx;
  Rc<D3D11ResourceCommon> a = new D3D11ResourceCommon(D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_SHADER_RESOURCE);
  Rc<D3D11ResourceCommon> alias = new D3D11ResourceCommon(a->gpu, a->dynamic);
  Rc<D3D11ResourceCommon> b = new D3D11ResourceCommon(D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_SHADER_RESOURCE);
  Rc<D3D11ResourceCommon> c = new D3D11ResourceCommon(D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_SHADER_RESOURCE);
  fill(ctx, a, 10); fill(ctx, b, 100);
  for (auto &stage : ctx.state_.ShaderStages) { stage.ConstantBuffers.clear_dirty(); stage.SRVs.clear_dirty(); }
  ctx.state_.InputAssembler.VertexBuffers.clear_dirty();
  auto &srvs = ctx.state_.ShaderStages[Stage].SRVs;
  auto &vbs = ctx.state_.InputAssembler.VertexBuffers;
  auto bind_srv = [&](unsigned slot, Rc<Buffer> buffer) {
    Rc<D3D11ShaderResourceView> view = new D3D11ShaderResourceView(buffer);
    bool replaced = false;
    auto &entry = srvs.bind(slot, {view.ptr()}, replaced); entry.SRV = view;
    ctx.EmitST([slot, buffer, typed](ArgumentEncodingContext &enc) mutable {
      enc.bindBuffer<Stage>(slot, Context::forward_rc(buffer), typed ? 1 : 0, {16, 32, 4, 8});
    });
  };
  bind_srv(0, a->gpu); bind_srv(64, alias->gpu); bind_srv(1, b->gpu);
  bind_srv(127, a->gpu); // Bound but not reflected must still be invalidated.
  bind_srv(126, {}); // Texture-view identity must never match a buffer DISCARD.
  for (unsigned slot : {0u, 1u, 3u}) {
    auto resource = slot == 1 ? b : (slot == 3 ? alias : a);
    bool replaced = false;
    auto &entry = vbs.bind(slot, {resource.ptr()}, replaced); entry.Buffer = resource;
    entry.Offset = slot == 3 ? 16 : 0; entry.Stride = 16;
    ctx.EmitST([slot, buffer = resource->gpu, offset = entry.Offset](ArgumentEncodingContext &enc) mutable {
      enc.bindVertexBuffer(slot, offset, 16, Context::forward_rc(buffer));
    });
  }
  ctx.state_.ShaderStages[Stage].Shader = new Shader({});
  ctx.state_.ShaderStages[Stage].Shader->managed.srv({0, 1, 64}, typed);
  unsigned table_count = 0, vb_count = 0;
  auto draw = [&](uint64_t token, bool changed) {
    const auto old_size = ctx.recorded.size();
    ctx.template UploadShaderStageResourceBinding<Stage, PipelineKind::Ordinary>();
    require((ctx.recorded.size() != old_size) == changed, "SRV table rebuild precision");
    if (changed) ++table_count;
    bool vb_dirty = vbs.any_dirty_masked(uint64_t(0xb));
    require(vb_dirty == changed, "VB table rebuild precision");
    if (vb_dirty) {
      ++vb_count;
      auto offset = ctx.PreAllocateArgumentBuffer(48, 32);
      ctx.EmitST([offset](ArgumentEncodingContext &enc) { enc.encodeVertexBuffers<PipelineKind::Ordinary>(0xb, offset); });
      vbs.clear_dirty();
    }
    ctx.EmitST([=](ArgumentEncodingContext &enc) {
      unsigned tables = 0, vertices = 0; uint64_t table_offset = 0, vertex_offset = 0;
      for (const auto &command : enc.commands) {
        if (command.index == SM50_BINDING_INDEX_ARGUMENT_TABLE && command.stage == Stage) { ++tables; table_offset = command.offset; }
        if (command.index == SM50_BINDING_INDEX_VERTEX_BUFFER) { ++vertices; vertex_offset = command.offset; }
      }
      require(tables == table_count && vertices == vb_count, "unrelated SRV/VB native table emissions");
      auto *words = enc.arguments.data() + table_offset / 8;
      if (typed) {
        require(words[0] == a->gpu->current()->gpuAddress() && words[4] == words[0], "typed SRV allocation refresh");
        require(words[1] == (uint64_t(8) << 32 | (4 + a->gpu->current()->currentSuballocationOffset() / 4)), "typed SRV suballocation metadata");
      } else {
        require(enc.read(words[0]) == token + 1 && enc.read(words[4]) == token + 1, "raw SRV rename/suballocation data");
      }
      auto *vertex = enc.arguments.data() + vertex_offset / 8;
      require(enc.read(vertex[0]) == token && enc.read(vertex[4]) == token + 1, "VB rename/suballocation data");
      require(enc.access_by_stage[unsigned(Stage)] >= table_count * 3, "SRV access tracking removed");
      require(enc.residency_by_stage[unsigned(Stage)] >= table_count * 3, "SRV residency removed");
    });
    ctx.replay();
  };
  draw(10, true);
  auto check_dirty = [&](Buffer *discarded) {
    for (auto &stage : ctx.state_.ShaderStages) for (unsigned slot = 0; slot < 128; ++slot) {
      bool dependent = stage.SRVs.test_bound(slot) && stage.SRVs.at(slot).SRV->buffer().ptr() == discarded;
      require(stage.SRVs.test_dirty(slot) == (!EXPECT_PRECISE || dependent), "resource-exact SRV dirty slots");
    }
    for (unsigned slot = 0; slot < 32; ++slot) {
      bool dependent = vbs.test_bound(slot) && vbs.at(slot).Buffer->buffer().ptr() == discarded;
      require(vbs.test_dirty(slot) == (!EXPECT_PRECISE || dependent), "resource-exact VB dirty slots");
    }
  };
  for (unsigned rename = 0; rename < 6; ++rename) {
    uint64_t token = 20 + rename * 10;
    fill(ctx, a, token); check_dirty(a->gpu.ptr()); draw(token, true);
  }
  fill(ctx, c, 1000); check_dirty(c->gpu.ptr()); draw(70, !EXPECT_PRECISE);
  srvs.set_dirty(120); vbs.set_dirty(20);
  fill(ctx, a, 80);
  require(srvs.test_dirty(120) && vbs.test_dirty(20), "preexisting resource dirty bits lost");
  draw(80, true);
  // Unbind aliases using the actual owner: later DISCARD must not dirty them.
  srvs.unbind(64); srvs.unbind(127); vbs.unbind(3);
  srvs.clear_dirty(); vbs.clear_dirty();
  fill(ctx, a, 90);
  require(srvs.test_dirty(0) && vbs.test_dirty(0), "remaining alias invalidation lost");
  require(srvs.test_dirty(64) == !EXPECT_PRECISE && vbs.test_dirty(3) == !EXPECT_PRECISE, "unbound alias invalidation");
  // Preserve and replay the last observation before retiring the shader owner.
  ctx.replay();
}
template <class Context> void resource_all_stages() {
  for (bool typed : {false, true}) {
    resource_discard<PipelineStage::Vertex, Context>(typed);
    resource_discard<PipelineStage::Pixel, Context>(typed);
    resource_discard<PipelineStage::Hull, Context>(typed);
    resource_discard<PipelineStage::Domain, Context>(typed);
    resource_discard<PipelineStage::Geometry, Context>(typed);
    resource_discard<PipelineStage::Compute, Context>(typed);
  }
}

// These controls invoke unchanged extracted guards, rather than replacing
// them with successful fixture stubs. Old source failures occur in the original
// contract scenarios before these current-interface controls are reached.
void queue_failure_controls() {
  ++scenarios;
  {
    Immediate ctx;
    require(ctx.cmd_queue.FailureReason() == S_OK, "healthy queue status");
    require(ctx.PreAllocateArgumentBuffer(64, 32) == 0 && ctx.argument_size == 64, "healthy argument reservation");
    ctx.cmd_queue.Fail(Queue::out_of_memory);
    ctx.cmd_queue.Fail(E_INVALIDARG);
    require(ctx.cmd_queue.FailureReason() == Queue::out_of_memory, "first queue failure retained");
    auto before = ctx.argument_size;
    require(ctx.PreAllocateArgumentBuffer(32, 16) == 0 && ctx.argument_size == before, "failed queue reserved argument bytes");
  }
  {
    Immediate ctx;
    Rc<D3D11ResourceCommon> resource = new D3D11ResourceCommon;
    auto before = resource->gpu->current();
    auto sentinel = reinterpret_cast<void *>(uintptr_t(0xBAD));
    D3D11_MAPPED_SUBRESOURCE mapped{sentinel, 77, 88};
    ctx.fail_next_emit = true;
    require(ctx.map(resource.ptr(), D3D11_MAP_WRITE_DISCARD, &mapped) == Queue::out_of_memory, "failed recording Map status");
    require(mapped.pData == sentinel && mapped.RowPitch == 77 && mapped.DepthPitch == 88, "failed recording published Map output");
    require(ctx.recorded.empty() && resource->gpu->current().ptr() == before.ptr(), "failed recording queued rename");
  }
}

} // namespace dxmt
int main() {
  try {
    dxmt::all_stages<dxmt::Immediate>();
    dxmt::all_stages<dxmt::Deferred>();
    dxmt::unused_slot_and_existing_dirty<dxmt::Immediate>();
    dxmt::unused_slot_and_existing_dirty<dxmt::Deferred>();
    dxmt::interleaved_contexts();
    dxmt::resource_all_stages<dxmt::Immediate>();
    dxmt::resource_all_stages<dxmt::Deferred>();
    dxmt::queue_failure_controls();
    std::cout << "PASS scenarios=" << scenarios << " checks=" << checks
              << " precise=" << EXPECT_PRECISE << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "CONTRACT: " << error.what() << " after " << checks << " checks\n";
    return 1;
  }
}
