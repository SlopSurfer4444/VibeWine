// SPDX-License-Identifier: MIT
#include "tile-methods-under-test.hpp"
int main() {
#if defined(__x86_64__) && defined(__APPLE__)
  int translated = 0;
  size_t translated_size = sizeof(translated);
  check(sysctlbyname("sysctl.proc_translated", &translated, &translated_size, nullptr, 0) == 0 && translated == 1,
        "x86_64 fixture executes through Rosetta");
#endif
  static constexpr CommandType expected_draw[] = {
    WMTRenderCommandDraw, WMTRenderCommandDrawIndexed, WMTRenderCommandDraw, WMTRenderCommandDrawIndexed,
    WMTRenderCommandDXMTTessellationMeshDraw, WMTRenderCommandDXMTTessellationMeshDrawIndexed,
    WMTRenderCommandDXMTGeometryDraw, WMTRenderCommandDXMTGeometryDrawIndexed,
    WMTRenderCommandDrawIndexedIndirect, WMTRenderCommandDrawIndirect,
    WMTRenderCommandDXMTGeometryDrawIndirect, WMTRenderCommandDXMTGeometryDrawIndexedIndirect,
    WMTRenderCommandDXMTTessellationMeshDrawIndirect, WMTRenderCommandDXMTTessellationMeshDrawIndexedIndirect
  };
  Pipeline good{reinterpret_cast<void *>(1)};
  for (int route = 0; route < 14; ++route) {
    native_attempts = diagnostics = 0;
    const int prior_refs = live_pso_references;
    {
      ArgumentEncodingContext enc;
      enc.render.tile_barrier_pso_key.raster_sample_count = 1;
      enc.render.tile_barrier_pso_key.color_formats[0] = 9;
      Harness h;
      h.render_pso(kind_for(route), &good);
      h.EmitST([](ArgumentEncodingContext &ctx) {
        ctx.render.barrier_state = {1, 1, 1, 2};
        ctx.intrapass_barrier_control_bits_ = 2;
        native_fail = true;
      });
      h.route(route); h.route(route);
      h.EmitST([route, prior_refs](ArgumentEncodingContext &ctx) {
        check(ctx.render_commands.size() == 1, "nil emits no helper or draw " + std::to_string(route));
        check(ctx.visibility == 0 && ctx.gs_tasks == 0 && ctx.ts_tasks == 0,
              "nil omits visibility and queued marshal " + std::to_string(route));
        check(ctx.render.barrier_state.barrierSet == 1 && ctx.render.barrier_state.barrierPreRasterSet == 1 &&
              ctx.render.barrier_state.barrierFragmentAfterPreRasterSet == 1 &&
              ctx.render.barrier_state.barrierPreRasterAfterFragmentSet == 2,
              "nil retains all pending flags " + std::to_string(route));
        check(ctx.render.last_pso == reinterpret_cast<void *>(1) && !ctx.render.pipeline_failed,
              "nil preserves application PSO and failure state " + std::to_string(route));
        check(native_attempts == 2 && diagnostics == 2 && live_pso_references == prior_refs,
              "nil is observable and not owned " + std::to_string(route));
        native_fail = false;
      });
      h.route(route);
      h.EmitST([route, prior_refs](ArgumentEncodingContext &ctx) {
        check(native_attempts == 3 && live_pso_references == prior_refs + 1,
              "retry creates owned cached PSO " + std::to_string(route));
        check(ctx.render_commands.size() == 7 && ctx.render_commands[1].type == WMTRenderCommandSetPSO &&
              ctx.render_commands[1].pso == reinterpret_cast<void *>(209) &&
              ctx.render_commands[2].type == WMTRenderCommandDispatchThreadsPerTile &&
              ctx.render_commands[2].width == 16 && ctx.render_commands[2].height == 16 &&
              ctx.render_commands[3].type == WMTRenderCommandSetPSO &&
              ctx.render_commands[3].pso == reinterpret_cast<void *>(1) &&
              ctx.render_commands[4].type == WMTRenderCommandMemoryBarrier &&
              ctx.render_commands[5].type == WMTRenderCommandMemoryBarrier &&
              ctx.render_commands[4].scope == (WMTBarrierScopeBuffers | WMTBarrierScopeTextures) &&
              ctx.render_commands[4].stages_before == WMTRenderStagePreRaster &&
              ctx.render_commands[4].stages_after == WMTRenderStagePreRaster &&
              ctx.render_commands[5].scope == (WMTBarrierScopeBuffers | WMTBarrierScopeTextures) &&
              ctx.render_commands[5].stages_before == WMTRenderStageFragment &&
              ctx.render_commands[5].stages_after == WMTRenderStagePreRaster &&
              ctx.render_commands[6].type == expected_draw[route],
              "successful exact tile restore barrier draw sequence " + std::to_string(route));
        check(ctx.render.barrier_state.barrierSet == 0 && ctx.render.barrier_state.barrierPreRasterSet == 0 &&
              ctx.render.barrier_state.barrierFragmentAfterPreRasterSet == 0 &&
              ctx.render.barrier_state.barrierPreRasterAfterFragmentSet == 2 && ctx.visibility == 1,
              "success consumes barriers once " + std::to_string(route));
        check(ctx.gs_tasks + ctx.ts_tasks == (route >= 10 ? 1 : 0),
              "only successful indirect marshal queued " + std::to_string(route));
        ctx.render.barrier_state.barrierSet = 1;
      });
      h.route(route);
      h.execute(enc);
      check(native_attempts == 3 && diagnostics == 2 && enc.visibility == 2,
            "success cache hit avoids native creation " + std::to_string(route));
      check(enc.render.last_pso == good.value, "tile never replaces last application PSO " + std::to_string(route));
    }
    check(live_pso_references == prior_refs, "encoder context releases tile cache " + std::to_string(route));
  }
  // No prior application PSO: successful helper emits no bogus restore.
  {
    ArgumentEncodingContext enc;
    enc.render.tile_barrier_pso_key.raster_sample_count = 4;
    enc.render.barrier_state.barrierSet = 1;
    native_fail = true;
    check(!enc.resolveRenderPassBarrier() && enc.render_commands.empty(), "first nil without last PSO");
    native_fail = false;
    check(enc.resolveRenderPassBarrier() && enc.render_commands.size() == 2 &&
          enc.render_commands[0].type == WMTRenderCommandSetPSO &&
          enc.render_commands[1].type == WMTRenderCommandDispatchThreadsPerTile && !enc.render.last_pso,
          "no restore without previous PSO");
    const int attempts = native_attempts;
    enc.render.barrier_state.barrierSet = 1;
    enc.intrapass_barrier_control_bits_ = 1;
    check(enc.resolveRenderPassBarrier() && native_attempts == attempts && enc.render.barrier_state.barrierSet == 1,
          "control mask preserves existing suppression policy");
  }
  // A second key must not reuse the first key's PSO; failure must not poison cache.
  {
    ArgumentEncodingContext enc;
    enc.render.tile_barrier_pso_key.raster_sample_count = 1;
    enc.render.barrier_state.barrierSet = 1;
    native_fail = false;
    check(enc.resolveRenderPassBarrier(), "first key success");
    enc.render.tile_barrier_pso_key.color_formats[0] = 13;
    enc.render.barrier_state.barrierSet = 1;
    native_fail = true;
    const auto old_size = enc.render_commands.size();
    check(!enc.resolveRenderPassBarrier() && enc.render_commands.size() == old_size, "new key nil retains stream");
    native_fail = false;
    check(enc.resolveRenderPassBarrier() && enc.render_commands.size() > old_size && enc.render_commands[old_size].pso == reinterpret_cast<void *>(213),
          "new key retry uses its own PSO");
  }
  check(live_pso_references == 0, "all PSO references released");
  if (!failures) std::cout << "PASS tile PSO failure and recovery on 14 production draw routes\n";
  return failures ? 1 : 0;
}
