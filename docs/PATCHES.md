# Patch origins and scope

Audited checkpoint: **2026-10-25**. This is a source map, not an FPS leaderboard
or a claim that every feature is enabled in the installed test runtime.

| Area | Origin | Scope / default | Source and evidence |
| --- | --- | --- | --- |
| DXMT renderer/compiler baseline | Upstream 3Shain/DXMT, identified Git ancestry | General renderer | [DXMT history and nine logical local changes](provenance/dxmt.md) |
| Staging, dynamic and ring allocation lifetime | Local VibeWine changes | General DXMT ownership/reclamation | [DXMT provenance](provenance/dxmt.md), [GPU/fixture results](diagnostics/full-bundle-20261023.md) |
| DXGI count/fill, resize/presentation and shader error paths | Local corrections on the upstream baseline | General correctness | [DXMT provenance](provenance/dxmt.md) |
| Matched WineMetal/AIRCONV and Metal guards | Upstream plus ported local bridge behavior/guards | General bridge/compiler; presentation gates remain separate | [Matched bundle](provenance/dxmt.md) |
| Compiler-output cache epoch | Local VibeWine change | Cache compatibility, not a demonstrated FPS boost | [DXMT provenance](provenance/dxmt.md) |
| Native windows, Retina, AX and ABI fixes | Vendor-derived Wine plus local patches | General integration; real-app input coverage remains incomplete | [Wine provenance](provenance/wine.md) |
| Cursor reset | Adapted Sikarugir Wine change | General reset behavior; Mission Control delivery is a separate check | [Original revision and attribution](provenance/wine.md) |
| Terminal Metal release | Local VibeWine correction using actual NTDLL shutdown state | General process-detach behavior; normal release/unload retained | [Focused repair, controls and install boundary](diagnostics/focused-followup-20261024.md) |
| Compiler/wait trace profile and scene labels | Local VibeWine instrumentation and analysis | Opt-in diagnostics; no automatic scene inference or FPS claim | [Focused checks and activation gap](diagnostics/focused-followup-20261024.md), [phase contract](../tools/telemetry/SESSION_PHASES.md) |
| Early scene submission | Local VibeWine experiment, informed by Apple guidance and independently reviewed queue behavior | General D3D11, build switch off by default; no claimed game FPS benefit | [Source and CPU controls](../components/dxmt/tests/vibewine/scene-submission/README.md), [native proof and live limits](diagnostics/focused-followup-20261024.md#implementation-and-verification-checkpoint-25-october) |
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
