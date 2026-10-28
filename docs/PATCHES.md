# Patch origins and scope

Audited checkpoint: **2026-10-28**. This is a source map, not an FPS leaderboard
or a claim that every feature is enabled in the installed test runtime.

| Area | Origin | Scope / default | Source and evidence |
| --- | --- | --- | --- |
| Lazy buffer/alternate texture views, compute urgency, variant retry and ring splice | Local VibeWine continuation on existing owners | General mechanisms, default paths preserved; fixture work reduction, not measured FPS | [Eight-direction wave](diagnostics/next-wave-20261028.md), [component contract](../components/dxmt/docs/next-wave-20261028.md) |
| Metal4 compiler and persistent ARM converter | Local integration reusing public Apple APIs and the existing LGPL converter; standalone pilots retained as earlier evidence | Explicit opt-in, complete supported recipe/lifetime/error gates; separate worker build/deployment | [Eight-direction wave](diagnostics/next-wave-20261028.md) |
| Monotonic MSync relative waits, minimum free hint and native cursor ownership | Local independently reproduced Wine repairs | General timing/reuse/focus classes; full Mach/liveness and actual Mission Control delivery remain open | [Eight-direction wave](diagnostics/next-wave-20261028.md) |
| Staging payloads, whole-CB reset, query polling, shader-key normalization and per-key parse ownership | Local fixes and refinements after independent Pro-audit reproduction; existing DXMT owners reused | General D3D11, no DS3 detection; VB/SRV dependency refinement retains existing default-off precise-bindings switch | [Frontier checkpoint](diagnostics/frontier-wave-20261028.md), [component source contract](../components/dxmt/docs/frontier-wave-20261028.md) |
| Bulk depth/stencil row writes and tile-helper failure handling | Local DXMT implementation on existing initializer/ring/barrier owners | General; row payload/readback controls, no unsynchronized dependent draw on helper creation failure | [Frontier checkpoint](diagnostics/frontier-wave-20261028.md) |
| MSync alert descriptor ownership and stable shared-page directory | Local Wine repair of reproduced fd-close and moving-table reader defects | General Apple MSync; real-fd cleanup retained; broader Mach RPC repair held | [Frontier checkpoint](diagnostics/frontier-wave-20261028.md), [table tests](../components/wine/vibewine/tests/msync-memory-ownership/TABLE_README.md) |
| Combined bounded compiler/presentation/helper trace | Local instrumentation and conservative consumer joins | Explicit new profile; old coverage/report shapes preserved; no automatic scene or causal inference | [Telemetry contract](../tools/telemetry/README.md) |
| Late-stall duration filtering and finish-thread memory scopes | Local extension of existing diagnostic producer/decoder | Explicit stall-waits opt-in; no game gate, retention-policy change or FPS claim | [Capture repair and memory hypotheses](diagnostics/stall-waits-20261026.md) |
| Exact setters, staging-row copies, requested view prefix, optional clocks and shader-null error ownership | Local general source refinements; independently checked Pro audit leads | General default behavior, no DS3 gate | [Deep round](diagnostics/deep-hotpaths-20261026.md) |
| Native empty color slots/raster shadow, semantic blend PSO hash and GS/TS packets | Local matched renderer/native experiments; Apple descriptor defaults and existing DXMT owners reused | Four default-off options, operation-count/proxy evidence and explicit regression/gap accounting | [Deep round](diagnostics/deep-hotpaths-20261026.md), [component contract](../components/dxmt/docs/deep-hotpaths-20261026.md) |
| Initializer transaction and creation-failure ownership | Local general repair of reproduced CPU publication/reservation defects; existing ring/tracker/native owners reused | Default behavior, no DS3 gate; checked failure/retry and covered GPU success paths; historical known-defect proof remains separate | [Repair, provenance and acceptance](diagnostics/initializer-repair-20261026.md), [historical reproducer](../components/dxmt/tests/vibewine/initializer-failure-proof/README.md) |
| Hot-path transport, variant hashing, pool/IR cost | Local implementation on existing DXMT owners; Pro source audits independently checked | General; render/compute fusion and growth default-off, no new game gate | [26 October report](diagnostics/hotpaths-20261026.md) |
| Resource hazard membership, SO-layout PSO identity and resolve-order preservation | Local correctness repairs from independently reproduced audit leads | General D3D11; baseline-failing controls, no FPS claim | [26 October report](diagnostics/hotpaths-20261026.md), [hazard fixture](../components/dxmt/tests/vibewine/binding-hazard/README.md), [SO fixture](../components/dxmt/tests/vibewine/pso-solayout/README.md) |
| Compiler stages/read-lock and long-stall parser | Local bounded producer/decoder/consumer changes | Opt-in attribution; elapsed time and explicit coverage, not automatic causality | [Compiler contract](../components/dxmt/docs/compiler-attribution-20261026.md), [session parser](../tools/telemetry/SESSION_PHASES.md) |
| DXMT renderer/compiler baseline | Upstream 3Shain/DXMT, identified Git ancestry | General renderer | [DXMT history and nine logical local changes](provenance/dxmt.md) |
| Native borrowed NSError ownership | Adapted g-cqd `cbf3c18c`; extended local API and same-object replacement handling | General correctness; successful shader/cache output unchanged | [Neighbor wave](integrations/neighbor-wave-20261025.md), [native production-bound controls](../components/dxmt/tests/vibewine/error-ownership/README.md) |
| Failed-PSO dependent-work cancellation | Adapted g-cqd render/compute guards, with preserved uploads and per-encoder reset | General D3D11; no title gate or compiler-success claim | [Production paths and controls](../components/dxmt/docs/failed-pipeline-20261025.md) |
| Demand-aware shader admission | Independent DXVK urgency design adapted to DXMT lifetimes | General D3D11, default-off; same worker capacity, no omitted draws | [Wave and limitations](diagnostics/performance-wave-next-20261025.md), [scheduler controls](../components/dxmt/docs/demand-scheduler-20261025.md) |
| Explicit color discard intent | DXVK API design; independent Metal adaptation | General D3D11, default-off, narrow last-encoder Store removal; no future load rewrite | [Admission and controls](../components/dxmt/docs/discard-hints-20261025.md) |
| Presentation wait identity/coverage | Local VibeWine instrumentation and analysis | Opt-in profile; bounded, no automatic scene or FPS claim | [Protocol and recipe](../tools/telemetry/PRESENTATION_WAITS.md) |
| Address queue publication ordering | Adapted Paul Gofman/Wine `81e52628`; previous timeout drain retained | General synchronization; PE production compilation and native/translated controls | [Neighbor wave](integrations/neighbor-wave-20261025.md), [Wine provenance](../components/wine/vibewine/UPSTREAM_BACKPORTS.md) |
| Staging, dynamic and ring allocation lifetime | Local VibeWine changes | General DXMT ownership/reclamation | [DXMT provenance](provenance/dxmt.md), [GPU/fixture results](diagnostics/full-bundle-20261023.md) |
| DXGI count/fill, resize/presentation and shader error paths | Local corrections on the upstream baseline | General correctness | [DXMT provenance](provenance/dxmt.md) |
| Matched WineMetal/AIRCONV and Metal guards | Upstream plus ported local bridge behavior/guards | General bridge/compiler; presentation gates remain separate | [Matched bundle](provenance/dxmt.md) |
| Compiler-output cache epoch | Local VibeWine change | Cache compatibility, not a demonstrated FPS boost | [DXMT provenance](provenance/dxmt.md) |
| Native windows, Retina, AX and ABI fixes | Vendor-derived Wine plus local patches | General integration; real-app input coverage remains incomplete | [Wine provenance](provenance/wine.md) |
| Cursor reset | Adapted Sikarugir Wine change | General reset behavior; Mission Control delivery is a separate check | [Original revision and attribution](provenance/wine.md) |
| Terminal Metal release | Local VibeWine correction using actual NTDLL shutdown state | General process-detach behavior; normal release/unload retained | [Focused repair, controls and install boundary](diagnostics/focused-followup-20261024.md) |
| Compiler/wait trace profile and scene labels | Local VibeWine instrumentation and analysis | Opt-in diagnostics; no automatic scene inference or FPS claim | [Focused checks and activation gap](diagnostics/focused-followup-20261024.md), [phase contract](../tools/telemetry/SESSION_PHASES.md) |
| Early scene submission | Local VibeWine experiment, informed by Apple guidance and independently reviewed queue behavior | General D3D11, build switch off by default; no claimed game FPS benefit | [Source and CPU controls](../components/dxmt/tests/vibewine/scene-submission/README.md), [native proof and live limits](diagnostics/focused-followup-20261024.md#implementation-and-verification-checkpoint-25-october) |
| Precise CB dependencies, proven dead stores and physical residency dedup | Local VibeWine source experiments | General D3D11; independent default-off switches, covered GPU checks | [Integrated wave and limits](diagnostics/fps-wave-20261025.md), [switch contract](../components/dxmt/docs/fps-wave-20261025.md) |
| Bounded immutable residency and completed-idle pool sweep | Local extension of existing DXMT owners/bridge | General, default-off; bounded references/backing/work; no measured memory/FPS gain | [Wave evidence](diagnostics/fps-wave-20261025.md), [idle policy](diagnostics/idle-retirement-20261025.md) |
| Fixed-output MetalFX and pure integer shader cleanup | Local opt-in experiments on existing scaler/compiler | Explicit scaling quality mode and exact integer CSE; shader cache epoch split; actual scaler/game acceptance pending | [Integrated wave](diagnostics/fps-wave-20261025.md) |
| WaitAll rollback and send/ACK attribution | Local MSync repair/instrumentation | Paired Unix NTDLL/server; ownership/wake controls and bounded summaries; not atomic WaitAll | [Wine source pin](provenance/wine.md), [integration limits](diagnostics/fps-wave-20261025.md) |
| Capture ownership/results and offset/MSAA present geometry | Local correctness repairs | General, common to reference/candidate; actual capture/presenter acceptance pending | [Native shader controls and limits](diagnostics/fps-wave-20261025.md) |
| Server-loss MSync guard | Local patch, retained composite NTDLL build | Failure handling, not a connectivity or FPS repair | [Exact patch and bases](../patches/runtime-2026-10-23/README.md) |
| Process-local virtual XInput | Local experimental Wine branch | Explicitly armed test input | [Wine source revisions](provenance/wine.md), [protocol](../tools/gameplay/VIRTUAL_CONTROLLER.md) |
| Socket/readiness metadata | Local experimental Wine branch | Opt-in diagnostics, not an established server fix | [Wine provenance](provenance/wine.md) |
| DS3 modes, aspect and borderless policy | Local title-specific compatibility | Exact title/profile gates | [DXMT gates](provenance/dxmt.md), [profiles](../profiles/README.md) |
| Metrics and presentation probes | Local tooling / experiments | Explicit test/capture use | [Telemetry](../tools/telemetry/README.md), [experiments](../experiments/README.md) |
| Clipboard ownership, SRW wake avoidance, reentrant handle close and MSync registration cleanup | Selected Wine, Proton-GE and Highball patches | General source backports; selected-module installation coverage in status | [Fork audit and exact upstream revisions](integrations/fork-review-20261023.md) |
| Hull relative indexing and four context lock entries | DXMT upstream backports | General compiler/context correctness; development cache epoch 2 | [Source audit](integrations/fork-review-20261023.md), [component checks](../components/dxmt/docs/upstream-backports-20261023.md) |
| Address-wait timeout race, inactive topmost ordering and Metal-view teardown | Adapted Proton-GE, Dormison and gzimbric patches | General source fixes; exclusive Metal producer lease preserved; selected-module coverage in status | [Follow-up audit](integrations/fork-review-20261023.md), [Wine checks](../components/wine/vibewine/UPSTREAM_BACKPORTS.md) |
| SM50 error/partial-shader ownership | Adapted DXMT upstream `39625fa9` subset | General compiler CPU API correctness; API 26 / epoch 2 unchanged | [Component source and 22-case regression](../components/dxmt/docs/upstream-backports-20261023.md) |
| Vulkan portability enumeration | Gcenx macports-wine recipe | Conditional host capability for optional Vulkan/MoltenVK builds; no current DS3/DXMT effect | [Follow-up audit](integrations/fork-review-20261023.md) |

## Which source represents the installed test?

The [current status](STATUS.md) binds the selected Wine/graphics closure and
latest DXMT source equivalent. This is a selected rebuild over retained engine
and dependency assets, not a full new Wine distribution. The earlier composite
baseline and its native-driver/XInput/network source deltas remain dated history;
do not apply that old patch queue on top of the current component blindly.
Private installation/admission helpers, game files, saves, accounts and raw
capture material are outside this source repository.

Public revision equivalents preserve the locally audited code while omitting
machine-local email metadata from unpublished commits. The DXMT provenance map
records both identities and exact tree equality; prior build identities remain
historical facts. Publication does not rerun or upgrade their evidence.

For installed/build/runtime limits, read [status](STATUS.md). New PRs should name
their source revision and choose core, game-profile, experiment or tooling/docs
in the [PR template](../.github/PULL_REQUEST_TEMPLATE.md).
