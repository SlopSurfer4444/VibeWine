# Deep optimization round — 26 October 2026

Production baseline DXMT `a21579fb`; CI-only qualification repair `c55166bd`;
production source checkpoint `101d5ff70d2dbbd19d90a545c7263e311bec15bd`;
published component `66bae06c1cba97db49172ef555e4ea4f2cc60efc` adds only test/docs and retains identical production bytes.
Wine remains `5d4b43e4`. Nine changes below operate on general renderer/compiler
mechanisms. No title condition, installed DS3 selection, game save, VPN or user
cache was changed. This source round does not establish a gameplay FPS increase.

Three separate public-source Pro audits covered native transport/passes,
initializer/cache transactions, and causal telemetry. Independent source owners
and review checked suggestions, counterexamples, actual work and failure paths.
The [component report](../../components/dxmt/docs/deep-hotpaths-20261026.md) and
individual fixture READMEs define exact admission and limitations.

## Changes retained

| Mechanism | Reduction demonstrated | Qualification |
| --- | --- | --- |
| Exact IA/shader/VB setters | 100 exact repeats avoid 100 invalidations/offset commands | Hazard validation precedes skip; converted identity, class-instance diagnostic, dirty/reopen/restore and immediate DISCARD preserved |
| Tight staging texture rows | 12 row commands→1; three five-row images 15→3 | Same format/full rows/both pitches/distinct native buffers; partial, padding, BC, alias and image gaps retain conservative path |
| Requested texture-view prefix | Nine registered views, request index2: nine wrappers/eight native views→three/two | Contiguous prefix, rename/epoch/residency and failed append ownership tested; high requested index keeps required prefix |
| Observer clocks | Three scopes×1000: compile-only 6000→0 optional clock reads | Generation metadata, payloads and 3000 wrapped native operations retained; remaining observer overhead unmeasured |
| Nullable shader error | Safe fallback when initialization transport fails without producing an error | Actual caller/C thunk and ownership/retry controls; native parser/transport mocked, no observed DS3 occurrence |
| Semantic blend PSO hash | Collision corpus equality calls526336→4096, GetDesc1 calls1044480→0 | Default-off; active-prefix/equality-compatible hash, one cached word per owner; count-zero CPU proxy can regress |
| Native raster shadow | Two equal states: ten→five selector groups; only changed fields sent | Default-off; synchronous decoder-local lifetime, ICB invalidation; change-heavy CPU proxy regresses |
| GS/TS marshal packets | Per nonempty stage2N+4→N+1 bridge entries, unchanged2N+4 commands | Default-off; fixed stack packets, exact payload/order, same barrier/resources/queries, W4 batching bypass |
| Canonical empty color slots | One getter plus eleven setters avoided per admitted slot | Default-off; all handles/indices/actions/individual double bits checked; getter reference retains historical path |

The empty-slot host experiment uses real Metal descriptor APIs. ABBA medians
for descriptor creation/setters/release/autoreleasepool were1030.30→255.29ns
for eight empty slots,1034.40→362.40ns for seven,1027.57→1021.39ns for no hits
with overlapping no-hit ranges. This excludes actual encoder creation and GPU.
Marshal timings were unstable, including reversals; raster and blend proxies
also have explicit regressions. Lower call counts are not universal CPU/FPS wins.
No default experiment promotion follows from these observations.

Four new matched PE/native options default to false:
`vibewine_native_raster_shadow`, `vibewine_semantic_blend_hash`,
`vibewine_marshal_packets`, `vibewine_empty_color_slots`.
Together with the previous fourteen, the integration gate is an eighteen-boolean
off/on build pair. Full source contains all general changes in both variants.

## Most important correctness discovery: unfinished initializer transaction

The explicit [known-defect reproducer](../../components/dxmt/tests/vibewine/initializer-failure-proof/README.md)
executes production initializer/ring/tracker with a mocked native boundary.
It reproduced18454 accepted fill requests but18453 encoded fills at natural
arena exhaustion. Injected failures also expose published unfilled commands,
failed ring-node reuse and poisoned zero-buffer cache state. Six happy paths
retain two fills/four copies/two clears. All sixteen intended observations were
reproduced by root. This is CPU evidence of permitted defects, **not** proof of
native memory failure causing a user's freeze. It is not an implemented repair
or an ordinary green CI test; the runner requires explicit known-defect mode.

The next correctness package must include reserve→fill→publish plus checked
factory callers/HRESULT/null outputs. Reserve finite arena/staging/reference
capacity before linking; flush only before an unpublished transaction; exclude
failed ring nodes; preserve successful prefix and reference lifetime; make zero
cache growth transactional. Keep two D/S spans separately reusable. Infinite
retry, fake success, sentinel sequence and hidden flush during allocation are
rejected. Queue/device-error liveness remains a separate contract.

## Every remaining hypothesis has a disposition

| Direction | Finding and decision |
| --- | --- |
| Fence batching | Scalar-oracle3340 cases preserve semantics and reduce calls, but singleton/short-list bookkeeping and unstable timing do not support blanket production import. Specialized render-update suffix remains a separate measurable experiment. |
| Resource declaration grouping | In the checked duplicate pattern existing dedup leaves singletons:384→320 selectors already; grouping adds no reduction afterward. Other distributions are unmeasured; no usage widening or reordering. |
| Clear folding | Clear→render cases already fold. Standalone clear removal without transferring signal provenance can strand a later wait. No unsafe new fold retained. |
| Reader concurrency | One NOMUTEX SQLite connection and mutable statement require current mutex.104/93 separate acquisition/read; warm disk hit still creates native library/function. Critical gameplay contention remains unknown; do not remove lock or add a pool without evidence. |
| Duplicate parse | Cold same-key callers may both parse before second lookup; temporary loser is released. Per-key single-flight needs terminal success/failure, recursion, cancellation and lifetime ownership; global parse lock rejected. Incidence unknown. |
| Shader error output | Small independent nullable-error guard accepted; this does not solve duplicate parsing. |
| Cache column/constructor failures | No ordinary current-writer BLOB conversion failure reproduced. Foreign non-BLOB rows can require conversion; valid empty BLOB is not OOM. Raw shader handle lacks a local owner during wrapper allocation; no ordinary throwing member identified, continuing-process leak unproven. Unchecked argument malloc is separate. No speculative repair imported. |
| D/S upload row packing | Can reduce per-pixel CPU bridge work to per-row while GPU stays two blits; bounded scratch and initializer transaction are prerequisites. Held. |
| Upload-cap donor patch | A64MiB threshold is not an allocation/publication repair or strict resident cap. Flushing inside allocation after linking an unfilled command is unsafe. No blind donor import. |
| MSAA MetalFX | Legacy multi-sample scaler reachability confirmed; native rejection not established. An added pre-resolve can change sRGB quantization (137 vs138 counterexample), so no universal exact substitution. |
| Direct drawable | Gamma/EDR/format conversion, drawable ownership, fences and retirement must survive. Fewer intermediates alone is insufficient. Held. |
| Acquire-and-describe | Could remove four bridge queries/frame while preserving a single acquisition, nil/occlusion/current hook/autorelease/fallback; cost and lifecycle proof remain pending. |
| Broader setter suppression | Nonnull shader/SRV/sampler/CB/topology repeats already coalesce. Pointer-only VB skip would miss hazards/offsets; null raster/blend/depth/index expansion remains unaccepted. |
| Full causal metrics | Implemented stages do not establish the actual frame's compiler critical path. Reuse existing owners/context; next coordinated packages below. |

## Metrics: concrete next acceptance, not another perpetual profiler

1. **Actual presentation binding.** Native REGISTER currently trusts supplied
   revision/geometry without validating each drawable's actual responsible layer
   and applied owner. Extend existing layer state/native feedback with versioned
   binding and typed successful-registration receipts, then finite outcome closure.
   Keep v1 evidence separate; sampled visibility alone is not continuous binding.
   Reject same-size foreign layer, same-revision foreign owner, address reuse,
   partial apply, delayed/overflowed/stale callback and old-bridge fallback.
2. **Consumer→task→presentation chain.** Existing `ArgumentEncodingContext`
   already owns execution queue/chunk/frame. Add immutable captured generation
   there and bounded task-bearing consumer wait edges; reuse80–84 and104–107.
   Add actual presentation-carrier and initializer-event dependency edges, plus
   native PSO phases. Recording-thread proximity and readiness address are not
   actual execution attribution; GPU interval is not GPU saturation.
3. **Clock/coverage/overhead.** Keep clock domains and mapping uncertainty;
   represent boundary-spanning gaps separately from internal-window statistics.
   Compare disabled, configured-unarmed, binding-only and detailed costs; count
   attempted clocks/getters/records as well as output and producer/controller CPU.
   A preflight budget does not automatically stop source emission. Without an
   independent disabled-mode presentation observer the absolute FPS tax is unknown.

These are source-verified design gaps, not added ABI/events or live acceptance.
The existing131072×64-byte append-only lifetime ring and detail budget remain;
rearm does not replenish slots. New collection must have source admission,
finite deadlines, explicit terminal/unknown outcomes and shared byte caps.

## Verification checkpoint

Focused root runs pass all nine new suites and existing render/compute fusion,
221 offline telemetry tests and13 workspace tests. Source authors also ran the
native/x86 sanitizer and mutation controls listed in each fixture; boundaries
vary and remain stated (native descriptors, mocked transport, stand-in COM owner,
actual full native interpreter). Initializer's sixteen expected-defect observations
are counted separately. Independent integration review caught getter-reference
contamination; the corrected reference preserves8slot visits/88indexed getters
across21 scenarios and rejects the regression control.

Both fresh161-target builds passed, with eighteen boolean values matched in actual PE/native compile commands and seven artifact hashes per variant. Both variants passed five generic GPU stages, two dedicated resolve paths, four active4/8MRT/sparse/depth adjunct stages and six staging/view stages; pixel, resolve, depth, staging and view wrong-golden controls rejected. Same process birth and seven PE/native mappings were admitted per run. The lab returned to the exact baseline plan/files with no private IPC socket. Present/mip/array/3D, GS/TS GPU execution and native error validation remain open. This is a retained dependency/Wine scaffold, not a newly built complete Wine distribution.

Build/GPU details are recorded in the [bounded receipt](deep-hotpaths-20261026.json).
The targeted guests supplement generic coverage: seventeen mandatory positive
stages per variant and five connected negative controls across thirteen
process-bound runs. GS/TS GPU execution, mip/array/3D attachment surfaces,
presentation and native failure validation remain separate gaps. Hosted CI is
an additional source gate, not a GPU receipt. Installed DS3 remains on its prior
selected runtime; no FPS result is inferred from successful builds or pixels.

## Retention

This round is nested in the existing hot-path wave contract: root owns raw
receipts at seven days/64MiB aggregate, curated results30days/4MiB, build products
existing40GB cap with20GiB reserve. Active source/candidate/recovery holds remain.
Manual retirement validates ownership and holds; no cleaner or new permanent
collector is installed. Public fixed minimal reproducer sources remain versioned.

Hosted Actions logs/artifacts inherit the repository's existing GitHub retention
setting; this round does not enforce its local byte/TTL cap on vendor storage.
Repository owners can inspect/delete an exact completed run from Actions or the
GitHub CLI after required evidence/holds are preserved. No automatic vendor
purge or new artifact upload is configured by this change.
