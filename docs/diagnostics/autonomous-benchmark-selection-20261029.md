# Autonomous D3D11 scene benchmark: source selection

Reviewed 29 October 2026. Owner: VibeWine benchmark integration/root.
Status: **implemented and installed as VibeWine Bench; all six scenes passed the
normal ten-second run through the installed app**.
See `tools/benchmarks/README.md` for the implemented protocol and limits. This
selection record preserves the original research below. The separate benchmark
prefix reuses read-only pinned engine files; DS3 settings, saves and prefix are
not its measurement target.

## Requested scenario

One command must prepare recording before opening a small Windows D3D11 app,
run named repeatable workloads without menus or human input, stop normally,
and report per-scene frame distributions correlated with renderer traces.
Rendering must traverse Wine -> DXMT -> Metal. A native Mac or browser score
does not establish performance of this path.

## Selection

Use the existing `components/dxmt/tests/dx11/` Win32/D3D11 scene foundation and
our existing collector lifecycle. Start with triangle, cube and BPL; expand
with individually reviewed procedural workloads. Do not build another game,
Steam workflow or input agent for this benchmark.

The existing scene programs need finite CLI execution, exact drawable size,
controlled scene time, explicit presentation mode, failure reporting and
normal automatic exit. Their present interactive, vsynced loops are not
already a benchmark.

| Checked candidate | Decision and reason |
| --- | --- |
| [AIO Graphics Test](https://github.com/The412Banner/AIO-Graphics-Test/tree/0f717300eea29848e465c0b33193657bb4f1afd3) | Useful procedural D3D11 scene donor: instancing, drawstress, raymarch, tessellation, compute and atomics. Its released executable is not accepted as our unattended measurement runner. |
| [AIO D3D11 runner](https://github.com/The412Banner/AIO-Graphics-Test/blob/0f717300eea29848e465c0b33193657bb4f1afd3/src/cube_d3d11.c) | Standalone path fixes the outer window at 640x480; Present errors are ignored; initialization errors can show dialogs; wall-time animation is not a fixed replay. |
| [AIO build](https://github.com/The412Banner/AIO-Graphics-Test/blob/0f717300eea29848e465c0b33193657bb4f1afd3/.github/workflows/build-windows.yml) / [timing](https://github.com/The412Banner/AIO-Graphics-Test/blob/0f717300eea29848e465c0b33193657bb4f1afd3/src/bench.c) | Released app imports Vulkan even when selecting DX11. Its CSV is overwritten between runs; half-second warmup is included in duration and can hide a long first-frame stall. CPU frame intervals are not GPU timestamps or presentation feedback. Reuse scenes through the scene API, not these measurement semantics. |
| [AIO license](https://github.com/The412Banner/AIO-Graphics-Test/blob/0f717300eea29848e465c0b33193657bb4f1afd3/LICENSE) / [credits](https://github.com/The412Banner/AIO-Graphics-Test/blob/0f717300eea29848e465c0b33193657bb4f1afd3/CREDITS.md) | Main code is Apache-2.0. Do not import the bundled Microsoft Dolphin asset header/scene/registry entry without separately established asset rights. Any selected code must retain attribution/notices. |
| [UNIGINE Heaven](https://benchmark.unigine.com/heaven) / [manual](https://benchmark.unigine.com/docs/heaven/User_Manual_Advanced.pdf) | Useful optional real-engine camera sequence with DX11. Command-line automation belongs to paid extended editions. No purchase/download made; not the required free foundation. |
| [moreaki/dxmtbench](https://github.com/moreaki/dxmtbench) | Useful suite/automation ideas, but targets WebGL/ANGLE and VirtualBox; introduces different layers from our native Windows D3D11-on-Wine route. Not our primary performance score. |

## Initial workload matrix

These were the selection targets. Implemented scenes are `cube`, `drawstress`,
`instanced`, `dynamicupload`, `pixelheavy` and `textured`; compute/tessellation/
atomics remain future work. A scene completion is not pixel-golden validation.

| Segment | Purpose | Source route |
| --- | --- | --- |
| Baseline | Submission/presentation overhead | Existing triangle/cube |
| Per-object draw/lighting | CPU state changes and many individual draws | Existing BPL; reviewed drawstress extension |
| Instancing | Distinguish per-draw overhead from batched geometry | Reviewed procedural instancing |
| Upload/rename | Dynamic constants, textures and resource lifetime | Existing BPL plus dynamic-retention guest patterns; add real displayed workload |
| Pixel-heavy | Separate shader/fragment pressure from CPU submit pressure | Reviewed stateless raymarch with controlled time |
| Compute/tessellation/atomics | Additional shader stages and ordering | Later opt-in scenes, explicit feature checks and pixel controls; unsupported is not a fallback score |

Keep cold shader/device creation, warm rendering and resize/fullscreen/exit as
separate named phases. Do not average unlike workloads into a claimed universal
FPS. Physics, AI, audio, real asset streaming and server behavior still require
separate fixtures or representative game validation.

## Reuse and acceptance

1. Reuse `tools/telemetry/scene_capture.py`, `session_phases.py`,
   `trace_controller.cpp`, fixed-window and repeatability analysis. Do not call
   the DS3-specific dispatch/controller route for a standalone app.
2. Prepare a dedicated matched runtime/prefix and bounded private output/cache
   before launch. Start the observer and verify fresh pre-launch readiness.
   Do not clear a user's game cache to manufacture a cold run.
3. Guest creates its device/swapchain, then waits on a bounded runner barrier.
   Bind the actual PID/birth/modules, trace generation and first real decoded
   records before releasing measurement. A running collector or ARM header
   alone does not prove useful recording. Support startup tracing separately
   so the first shader compile is not silently excluded.
4. Exact render/drawable dimensions, seed, scene state/time sequence, API,
   vsync policy, source/runtime hashes and phase boundaries belong in results.
   Use fixed-camera segments initially. Existing fixed-window analysis rejects
   moving routes: extend attribution explicitly before accepting moving scenes.
5. Preserve per-frame distributions and raw counts, median/p95/p99 times,
   slow-frame counts and separately labeled CPU/GPU/presentation observations.
   Match native presentation feedback where available; never relabel CPU loop
   intervals as physical display FPS. Report cache, power, thermal and background
   load state. First A/A repeatability, then ordered A/B on the same workloads.
6. Unknown scene, failed Present, wrong module, stale readiness, empty/lost/
   overflowed trace, output failure, timeout or missing normal exit fails the
   run. No dialog dismissal, hidden scene fallback or success based on a frame
   counter alone. Stop owned collectors and verify process absence at closure.

A quick finite suite should take a few minutes; duration will be selected only
after measuring trace rate and observer cost. Longer detail must fit collector
capacity; instrumented/minimal-observer A/A controls must expose observer cost.

## Data and validation boundary

For a future runner: root owns only its dedicated benchmark-session directory;
data classes are synthetic benchmark frames/timings, renderer traces and host
performance observations. Planned raw TTL 7 days / cap 256 MiB per run and 1 GiB total;
curated summaries/provenance TTL 30 days / cap 32 MiB total. Active runs, unresolved
failures, pinned comparisons and recovery evidence are excluded from deletion.
Collection must enforce limits at source and stop before overflow. Cleanup is
manual exact-owner/reference/hold recheck until an existing owner mechanism is
integrated; these limits are **not an installed cleaner or an enforced quota**.

The research itself executed no external source or binary. Subsequent local
implementation built a D3D11-only finite guest from the existing DXMT foundation
and new procedural workloads; no AIO source, binary or assets were incorporated.
All six scenes completed both a short Wine/DXMT smoke run and the normal app
run with real decoded CPU/GPU observations. The app opened its report and exited
without human input; original failures from integration remain preserved.
This establishes an autonomous synthetic harness, not DS3 coverage
or a measured performance improvement. Native presentation feedback is partial
and its strict whole-capture acceptance remains separate from CPU/GPU metrics.
