# Deep hot-path round — 26 October 2026

Base: `c55166bd` (production base `a21579fb`). These are general DXMT mechanisms,
not new DS3 conditions. Source experiments are distinct from installed runtime,
measured workload incidence and FPS. The hub report records root build/GPU gates.

## Implemented candidates

| Change | Admission / scope | Production-bound evidence |
| --- | --- | --- |
| Exact setters | Converted input-layout identity; null shader with zero class instances; unchanged VB offset/stride only after hazard/binding validation | 100 repeats remove 100 invalidations/offset commands; restore, ClearState, reopen and immediate DISCARD controls; deferred DISCARD source-inspected |
| Staging texture rows | Same format, tightly packed full rows, distinct native buffers; one command per depth image | 12→1 and 15→3 commands; 18 copy scenarios, partial/padded/alias/BC fallbacks; native execution is a separate gate |
| Texture views | Materialize only contiguous prefix through requested key; publish each successful append | Nine descriptors, request index 2: nine wrappers/eight native views → three/two; high/low requests, epochs, rename, failed wrapper/vector growth and residency controls |
| Optional observer clocks | Immutable profile gates before scope clocks; generation metadata and wrapped operations unchanged | Three scopes × 1000: full 6000 clocks, compile-only zero, presentation 2000; all 3000 wrapped operations retained |
| Shader error ownership | Initialize nullable error; retrieve/free only an owned present error | Actual caller and C thunk, transport/non-message/owned-error, retry and success controls; native parser/transport mocked |
| Semantic blend PSO hash | Default-off `vibewine_semantic_blend_hash`; comparator-matched fields and selected prefix; one cached word/owner | Collision corpus equality calls 526336→4096; GetDesc1 1044480→0. Count-zero timing regresses in CPU proxy; private ABI requires matched build |
| Native raster shadow | Default-off `vibewine_native_raster_shadow`; one synchronous decoder call only; ICB invalidates | Two equal raster commands: ten→five selector groups; all-change retains ten. Change-heavy proxy regresses; not a universal timing improvement |
| GS/TS marshal packets | Default-off `vibewine_marshal_packets`; synchronous fixed packets, no ABI/heap/allocator change | N nonempty tasks: 2N+4→N+1 transport entries, identical commands/payload/order; native CPU receiver, not GPU GS/TS acceptance |
| Canonical empty colors | Default-off `vibewine_empty_color_slots`; all handles/actions/indices and individual clear double bits match fresh Metal defaults | One indexed getter + eleven writes removed per admitted slot; real descriptor snapshots and mutation controls. Active MRT/encoder validation is a separate gate |

Each focused runner/README is under `tests/vibewine/`. Four new flags default to
false and share the existing PE/native Meson definition map. Getter and batching
references must retain their targeted historical work. Operation counts and host
CPU microbenchmarks do not establish game FPS. No new shader/cache epoch is needed:
shader output and serialized keys remain unchanged. Blend retention is documented
in [the existing owner contract](fps-wave-20261025.md).

## Initializer correctness finding — deliberately not presented as repaired

`initializer-failure-proof` executes the actual initializer/ring/tracker with a
mock native allocation/command boundary. Explicit `--expect-known-defects` is
required; default invocation refuses to turn known defects into a green test.
Natural arena exhaustion reproduced 18454 fill requests but only 18453 commands.
Failure injection also found published unfilled payloads, invalid ring-block
reuse and poisoned zero-buffer state. This is not evidence of a native OOM in DS3.

A safe repair must span reserve/fill/publish and checked creation callers: finite
preflight/flush before unpublished reservations; all arena, staging and reference
reservations before publication; exclude failed ring nodes; transactional zero
cache; checked status/sequence through buffer/texture factories to HRESULT/null
output. An infinite retry, sentinel sequence or flush inside allocation is not a
complete repair. Two separate D/S spans may reuse two retained blocks where one
combined span would require another allocation. Async submission/device-error
liveness is an additional contract, not silently solved by local OOM handling.

## Dispositions after independent source and Pro audits

- Fence batching: scalar semantics passed, but short-list bookkeeping and noisy
  timing reject a blanket import. Specialized update suffix needs its own cost proof.
- Adjacent resource grouping: dedup can leave only singletons; no established
  incremental benefit. Do not widen resource usage or reorder declarations.
- Clear folding: existing render folding already removes some clears. Removing
  standalone clear without moving signal provenance can strand later waits.
- Cache reader pool: one SQLite NOMUTEX connection/shared statement requires the
  current mutex. New read/creation stages exist; real critical contention unknown.
- Duplicate shader parse: possible before second lookup, but incidence unknown.
  Single-flight needs per-key RAII terminal success/failure, wakeup and recursion
  policy; a global parser lock is rejected.
- Depth/stencil packing: row packing could reduce CPU bridge work, but depends on
  safe initializer publication and bounded scratch; only two existing GPU blits.
- MSAA MetalFX: legacy reachability confirmed; native incompatibility unproven.
  Pre-resolving sRGB can change quantization (counterexample 137 vs 138); not a
  universally exact performance replacement.
- Direct drawable: gamma/EDR/format, ownership, retirement and fences prevent
  treating one fewer intermediate as a safe universal optimization.
- Acquire-and-describe: four fewer bridge queries/frame is source-plausible;
  preserve one acquisition, nil/occlusion/autorelease and old-bridge fallback.
- Full causal metrics: task elapsed stages are not consumer critical paths.
  Native layer/drawable binding, typed finite presentation outcomes, execution
  context/dependency edges, boundary-spanning gaps and uncertainty/overhead need
  explicit acceptance. No ten-minute all-events promise is made.

## Ownership and retention

Root owns index, matching builds, sandbox GPU lab, publication and acceptance.
Fixtures own bounded temporary files removed on normal/error exit; each README
states limits. Known-defect proof is not part of ordinary CI. Raw saved receipts
remain under the existing wave's seven-day/64 MiB shared cap; curated summaries
30 days/4 MiB, builds existing 40 GB cap and 20 GiB free reserve. Active source,
selected candidates and recovery are holds. Manual cleanup rechecks ownership;
there is no new cleaner, permanent collector, privilege or game-data producer.
