// SPDX-License-Identifier: MIT
// CPU seams: real CommandList and extracted production scheduling code.
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>
#include "dxmt_command_list.hpp"
using dxmt::CommandWithContext;

using UINT = uint32_t;
using INT = int32_t;
using WMTPrimitiveType = uint32_t;
using WMTIndexType = uint32_t;
struct WMTSize { uint32_t width{}, height{}, depth{}; };
namespace WMT {
using RenderPipelineState = void *;
using ComputePipelineState = void *;
using Buffer = uintptr_t;
}
enum class PipelineStage { Vertex, Pixel, Hull, Domain, Geometry, Compute };
enum class PipelineKind { Ordinary, Tessellation, Geometry };
enum ResourceAccess { Read = 1, Write = 2 };
enum CommandType {
  WMTRenderCommandSetPSO, WMTRenderCommandDraw, WMTRenderCommandDrawIndexed,
  WMTRenderCommandDXMTTessellationMeshDraw, WMTRenderCommandDXMTTessellationMeshDrawIndexed,
  WMTRenderCommandDXMTGeometryDraw, WMTRenderCommandDXMTGeometryDrawIndexed,
  WMTRenderCommandDrawIndexedIndirect, WMTRenderCommandDrawIndirect,
  WMTRenderCommandDXMTGeometryDrawIndirect, WMTRenderCommandDXMTGeometryDrawIndexedIndirect,
  WMTRenderCommandDXMTTessellationMeshDrawIndirect, WMTRenderCommandDXMTTessellationMeshDrawIndexedIndirect,
  WMTComputeCommandSetPSO, WMTComputeCommandDispatch, WMTComputeCommandDispatchIndirect,
  WMTComputeCommandDispatchThreads
};
struct Command {
  CommandType type{};
  void *pso{};
  WMTSize size{}, threadgroup_size{};
  uint64_t primitive_type{}, vertex_start{}, vertex_count{}, base_instance{}, instance_count{},
      index_count{}, index_type{}, index_buffer{}, index_buffer_offset{}, draw_arguments_offset{},
      threads_per_patch{}, patch_per_group{}, patch_per_mesh_instance{}, warp_count{}, vertex_per_warp{},
      indirect_args_buffer{}, indirect_args_offset{}, dispatch_args_buffer{}, dispatch_args_offset{},
      imm_draw_arguments{};
  int64_t base_vertex{};
  std::array<int, 4> uploaded{};
};
using wmtcmd_render_setpso = Command;
using wmtcmd_compute_setpso = Command;
using wmtcmd_render_draw = Command;
using wmtcmd_render_draw_indexed = Command;
using wmtcmd_render_dxmt_tessellation_mesh_draw = Command;
using wmtcmd_render_dxmt_tessellation_mesh_draw_indexed = Command;
using wmtcmd_render_dxmt_geometry_draw = Command;
using wmtcmd_render_dxmt_geometry_draw_indexed = Command;
using wmtcmd_render_draw_indexed_indirect = Command;
using wmtcmd_render_draw_indirect = Command;
using wmtcmd_render_dxmt_geometry_draw_indirect = Command;
using wmtcmd_render_dxmt_geometry_draw_indexed_indirect = Command;
using wmtcmd_render_dxmt_tessellation_mesh_draw_indirect = Command;
using wmtcmd_render_dxmt_tessellation_mesh_draw_indexed_indirect = Command;
using wmtcmd_compute_dispatch = Command;
using wmtcmd_compute_dispatch_indirect = Command;
struct DXMT_DRAW_ARGUMENTS { uint32_t StartVertex, VertexCount, InstanceCount, StartInstance; };
struct DXMT_DRAW_INDEXED_ARGUMENTS { int32_t BaseVertex; uint32_t IndexCount, StartIndex, InstanceCount, StartInstance; };
struct DXMT_DISPATCH_ARGUMENTS { uint32_t x, y, z; };
struct RenderEncoderData {
#include "render-flag.hpp"
  bool use_tessellation{}, use_geometry{}, use_emulated_so{};
  void *last_pso{};
};
struct ComputeEncoderData {
#include "compute-flag.hpp"
};
struct Buffer {
  uintptr_t buffer() const { return 80; }
  uint64_t gpuAddress() const { return 1024; }
};
struct Bindable {
  std::shared_ptr<Buffer> buffer() const { return std::make_shared<Buffer>(); }
};
struct Reflection { std::array<uint32_t, 3> ThreadgroupSize{8, 4, 1}; };
struct Shader {
  const Reflection &reflection() const { static Reflection value; return value; }
  int constant_buffers_info() const { return 10; }
  int arguments_info() const { return 11; }
};
struct CompiledPipeline { void *PipelineState{}; uint32_t NumControlPointOutputElement{}, ThreadsPerPatch{}; };
using MTL_COMPILED_GRAPHICS_PIPELINE = CompiledPipeline;
using MTL_COMPILED_TESSELLATION_MESH_PIPELINE = CompiledPipeline;
using MTL_COMPILED_COMPUTE_PIPELINE = CompiledPipeline;
struct Pipeline {
  void *value;
  uint32_t threads = 8;
  int resolutions = 0;
  void GetPipeline(CompiledPipeline *out) {
    ++resolutions;
    *out = {value, value ? 4u : 0u, value ? threads : 0u};
  }
};
std::pair<uint32_t, uint32_t> get_gs_vertex_count(uint32_t) { return {3, 3}; }
#define WARN(...) ((void)0)

struct ArgumentEncodingContext {
  RenderEncoderData render;
  ComputeEncoderData compute;
  std::vector<Command> render_commands, compute_commands;
  alignas(32) std::array<std::byte, 4096> mapped{};
  uint32_t tess_num_output_control_point_element{}, tess_threads_per_patch{};
  int visibility{}, render_barriers{}, compute_barriers{}, accesses{}, temp_allocations{}, gs_tasks{}, ts_tasks{};
  int resource_generation = 0;
  std::array<int, 4> uploaded{};
  RenderEncoderData *currentRenderEncoder() { return &render; }
  ComputeEncoderData *currentComputeEncoder() { return &compute; }
  void startRenderPass() { render = {}; }
  void startComputePass(uint64_t = 0) { compute = {}; }
  template <typename T> T &encodeRenderCommand() {
    render_commands.emplace_back();
    render_commands.back().uploaded = uploaded;
    return render_commands.back();
  }
  template <typename T> T &encodeComputeCommand() {
    compute_commands.emplace_back();
    compute_commands.back().uploaded = uploaded;
    return compute_commands.back();
  }
  void bumpVisibilityResultOffset() { ++visibility; }
  void resolveRenderPassBarrier() { ++render_barriers; }
  void resolveComputePassBarrier() { ++compute_barriers; }
  std::pair<uintptr_t, uint64_t> currentIndexBuffer() { ++accesses; return {90, 0}; }
  template <PipelineStage stage = PipelineStage::Compute, typename... Args>
  std::pair<Buffer *, uint64_t> access(const std::shared_ptr<Buffer> &buffer, Args...) {
    ++accesses; return {buffer.get(), 0};
  }
  struct TempBuffer { uintptr_t gpu_buffer; uint64_t gpu_address, offset; };
  TempBuffer allocateTempBuffer1(size_t, size_t) { ++temp_allocations; return {100, 2000, 0}; }
  template <typename T> T *getMappedArgumentBuffer(uint64_t offset) { return reinterpret_cast<T *>(mapped.data() + offset); }
  uint64_t getFinalArgumentBufferOffset(uint64_t offset) { return offset; }
  uintptr_t getFinalArgumentBuffer() { return 70; }
  template <typename... Args> void encodeGSDispatchArgumentsMarshal(Args...) { ++gs_tasks; }
  template <typename... Args> void encodeTSDispatchArgumentsMarshal(Args...) { ++ts_tasks; }
  template <PipelineStage, PipelineKind> void encodeConstantBuffers(const Reflection *, int, uint64_t) { uploaded[0] = resource_generation; }
  template <PipelineStage, PipelineKind> void encodeShaderResources(const Reflection *, int, uint64_t) { uploaded[1] = resource_generation; }
  template <PipelineKind> void encodeVertexBuffers(uint32_t, uint64_t) { uploaded[2] = resource_generation; }
  template <PipelineKind> void encodeStreamOutputBuffers(uint64_t) { uploaded[3] = resource_generation; render.use_emulated_so = true; }
};

// Each copied Emit call runs in its original lexical variable names. These are
// inputs to the production lambdas, not substitutes for their bodies.
#define route_locals \
  [[maybe_unused]] UINT Primitive = 3, StartVertexLocation = 1, VertexCount = 12, IndexType = 0, \
    IndexBufferOffset = 0, IndexCount = 12, VertexCountPerInstance = 12, InstanceCount = 2, \
    StartInstanceLocation = 0, IndexCountPerInstance = 12, NumControlPoint = 3, \
    StartIndexLocation = 0, BaseInstance = 0, AlignedByteOffsetForArgs = 0, \
    ThreadGroupCountX = 2, ThreadGroupCountY = 3, ThreadGroupCountZ = 4; \
  [[maybe_unused]] INT BaseVertexLocation = 0; \
  [[maybe_unused]] uint64_t draw_arguments_offset = 0, max_object_threadgroups = 100000; \
  [[maybe_unused]] Bindable binding; \
  [[maybe_unused]] Bindable *bindable = &binding

struct Harness {
  // Storage outlives list destruction; real CommandList invokes/destructs lambdas.
  std::vector<std::unique_ptr<std::byte[]>> storage;
  dxmt::CommandList<ArgumentEncodingContext> list;
  struct { struct { uint32_t Topology = 3; } InputAssembler; } state_;
  template <dxmt::CommandWithContext<ArgumentEncodingContext> Fn> void EmitOP(Fn &&fn) {
    auto size = list.template calculateCommandSize<Fn>();
    auto allocation = std::make_unique<std::byte[]>(size);
    list.emit(std::forward<Fn>(fn), allocation.get());
    storage.push_back(std::move(allocation));
  }
  template <dxmt::CommandWithContext<ArgumentEncodingContext> Fn> void EmitST(Fn &&fn) { EmitOP(std::forward<Fn>(fn)); }
#include "scheduling-under-test.hpp"
  void execute(ArgumentEncodingContext &enc) { list.execute(enc); }
  void route(int index) {
    using Method = void (Harness::*)();
    static constexpr Method methods[] = {&Harness::route_0, &Harness::route_1, &Harness::route_2, &Harness::route_3,
      &Harness::route_4, &Harness::route_5, &Harness::route_6, &Harness::route_7, &Harness::route_8, &Harness::route_9,
      &Harness::route_10, &Harness::route_11, &Harness::route_12, &Harness::route_13, &Harness::route_14, &Harness::route_15};
    (this->*methods[index])();
  }
  void render_pso(int kind, Pipeline *pipeline) {
    if (kind == 0) pso_0(pipeline);
    else if (kind == 1) pso_1(pipeline);
    else pso_2(pipeline);
  }
};

struct InternalHelper {
  ArgumentEncodingContext &ctx;
#include "internal-under-test.hpp"
};

int failures = 0;
void check(bool condition, const std::string &name) { if (!condition) { ++failures; std::cerr << "FAIL " << name << '\n'; } }
size_t render_draws(const ArgumentEncodingContext &enc) {
  size_t count = 0;
  for (const auto &cmd : enc.render_commands) count += cmd.type != WMTRenderCommandSetPSO;
  return count;
}
size_t compute_dispatches(const ArgumentEncodingContext &enc) {
  size_t count = 0;
  for (const auto &cmd : enc.compute_commands) count += cmd.type != WMTComputeCommandSetPSO;
  return count;
}
int kind_for(int route) { return route == 4 || route == 5 || route >= 12 ? 2 : route == 6 || route == 7 || route == 10 || route == 11 ? 1 : 0; }

int main(int argc, char **) {
  Pipeline good{reinterpret_cast<void *>(1)}, bad{nullptr}, recovered{reinterpret_cast<void *>(2)};
  if (argc > 1) {
    { Harness h; ArgumentEncodingContext enc; h.pso_0(&good); h.route_0(); h.pso_0(&bad); h.route_0(); h.execute(enc);
      check(render_draws(enc) == 1, "baseline stale draw submitted"); }
    { Harness h; ArgumentEncodingContext enc; h.pso_3(&good); h.route_14(); h.pso_3(&bad); h.route_14(); h.execute(enc);
      check(compute_dispatches(enc) == 1, "baseline stale dispatch submitted"); }
    return failures ? 1 : 0;
  }

  // Every actual route runs successfully, then suppresses repeated cached failure,
  // then recovers without a new physical encoder. Failed TS metadata is zero.
  for (int route = 0; route < 16; ++route) {
    Harness h; ArgumentEncodingContext enc;
    auto pso = [&](Pipeline *pipeline) { if (route < 14) h.render_pso(kind_for(route), pipeline); else h.pso_3(pipeline); };
    pso(&good); h.route(route);
    pso(&bad);
    h.EmitST([](ArgumentEncodingContext &ctx) {
      ctx.accesses = ctx.temp_allocations = ctx.gs_tasks = ctx.ts_tasks = 0;
      ctx.visibility = ctx.render_barriers = ctx.compute_barriers = 0;
      ctx.tess_threads_per_patch = 0;
    });
    h.route(route); h.route(route);
    h.EmitST([route](ArgumentEncodingContext &ctx) {
      check(ctx.accesses == 0 && ctx.temp_allocations == 0 && ctx.gs_tasks == 0 && ctx.ts_tasks == 0,
            "failed route no access/allocation/marshal " + std::to_string(route));
      check(ctx.visibility == 0 && ctx.render_barriers == 0 && ctx.compute_barriers == 0,
            "failed route no visibility/barrier " + std::to_string(route));
      check(route < 14 ? render_draws(ctx) == 1 : compute_dispatches(ctx) == 1,
            "cached failure omits repeated work " + std::to_string(route));
    });
    pso(&recovered); h.route(route); h.execute(enc);
    check(route < 14 ? render_draws(enc) == 2 : compute_dispatches(enc) == 2,
          "success failure success route " + std::to_string(route));
    check(route < 14 ? !enc.render.pipeline_failed : !enc.compute.pipeline_failed,
          "successful PSO clears failure " + std::to_string(route));
  }

  // First PSO failure must omit all work even without any previous valid PSO.
  for (int route = 0; route < 16; ++route) {
    Harness h; ArgumentEncodingContext enc;
    if (route < 14) h.render_pso(kind_for(route), &bad); else h.pso_3(&bad);
    h.route(route); h.execute(enc);
    check(enc.render_commands.empty() && enc.compute_commands.empty() && enc.gs_tasks == 0 && enc.ts_tasks == 0,
          "first failure has no native commands " + std::to_string(route));
  }

  // Application PSO metadata becomes observable only on successful resolution.
  {
    Harness h; ArgumentEncodingContext enc;
    enc.tess_threads_per_patch = 7; enc.tess_num_output_control_point_element = 9;
    h.pso_2(&bad); h.pso_1(&bad); h.execute(enc);
    check(!enc.render.use_tessellation && !enc.render.use_geometry, "failed stage flags stay absent");
    check(enc.tess_threads_per_patch == 7 && enc.tess_num_output_control_point_element == 9,
          "failed tessellation preserves metadata");
  }
  {
    Harness h; ArgumentEncodingContext enc;
    h.pso_2(&good); h.pso_1(&good); h.pso_2(&bad); h.pso_1(&bad); h.execute(enc);
    check(enc.render.use_tessellation && enc.render.use_geometry, "failed resolution preserves successful stage union");
    check(enc.render.last_pso == good.value, "failure preserves successful last PSO for helpers");
  }

  // Real upload scheduling remains independent of cancellation. No shader change
  // or new pass is inserted at recovery, so the latest uploads must survive.
  {
    Harness h; ArgumentEncodingContext enc;
    h.pso_0(&good); h.route_0(); h.pso_0(&bad);
    h.EmitST([](ArgumentEncodingContext &ctx) { ctx.resource_generation = 29; });
    h.resource_uploads<PipelineStage::Vertex, PipelineKind::Ordinary>();
    h.vertex_uploads(); h.so_uploads(); h.route_0();
    h.pso_0(&recovered); h.route_0(); h.execute(enc);
    check(render_draws(enc) == 2, "binding recovery omits failed draw");
    check(enc.render_commands.back().uploaded == std::array<int, 4>{29, 29, 29, 29},
          "unchanged shader recovery preserves all uploads");
  }

  // The same real command list is replayed against fresh and reused encoders.
  {
    Harness h;
    h.EmitST([](ArgumentEncodingContext &ctx) { ctx.startRenderPass(); });
    h.pso_0(&bad); h.route_0();
    h.EmitST([](ArgumentEncodingContext &ctx) { ctx.startComputePass(); });
    h.pso_3(&good); h.route_14();
    h.EmitST([](ArgumentEncodingContext &ctx) { ctx.startRenderPass(); });
    h.pso_0(&good); h.route_0();
    ArgumentEncodingContext enc;
    enc.render.pipeline_failed = enc.compute.pipeline_failed = true;
    h.execute(enc); h.execute(enc);
    check(render_draws(enc) == 2 && compute_dispatches(enc) == 2, "replay and render compute render isolation");
    ArgumentEncodingContext fresh;
    h.execute(fresh);
    check(render_draws(fresh) == 1 && compute_dispatches(fresh) == 1, "replay on fresh execution context");
  }
  {
    Harness h; ArgumentEncodingContext enc;
    h.pso_3(&bad); h.route_14();
    h.EmitOP([](ArgumentEncodingContext &ctx) {
      InternalHelper helper{ctx};
      helper.startComputePass();
      helper.setComputePSO(reinterpret_cast<void *>(3), {8, 4, 1});
      helper.dispatch({16, 16, 1});
    });
    h.execute(enc);
    check(compute_dispatches(enc) == 1 && enc.compute_commands.back().type == WMTComputeCommandDispatchThreads,
          "independent internal compute helper executes after application failure");
  }
  if (!failures) std::cout << "PASS failed pipeline production scheduling scenarios\n";
  return failures ? 1 : 0;
}
