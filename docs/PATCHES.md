# Patch origins and scope

Audited checkpoint: **2026-10-23**. This is a source map, not an FPS leaderboard
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
| Server-loss MSync guard | Local patch, retained composite NTDLL build | Failure handling, not a connectivity or FPS repair | [Exact patch and bases](../patches/runtime-2026-10-23/README.md) |
| Process-local virtual XInput | Local experimental Wine branch | Explicitly armed test input | [Wine source revisions](provenance/wine.md), [protocol](../tools/gameplay/VIRTUAL_CONTROLLER.md) |
| Socket/readiness metadata | Local experimental Wine branch | Opt-in diagnostics, not an established server fix | [Wine provenance](provenance/wine.md) |
| DS3 modes, aspect and borderless policy | Local title-specific compatibility | Exact title/profile gates | [DXMT gates](provenance/dxmt.md), [profiles](../profiles/README.md) |
| Metrics and presentation probes | Local tooling / experiments | Explicit test/capture use | [Telemetry](../tools/telemetry/README.md), [experiments](../experiments/README.md) |

## Which source represents the installed test?

- Five graphics artifacts came from one matched DXMT/compiler/bridge build.
- Five retained Wine artifacts came from the separately recorded XInput,
  network, native-driver and composite NTDLL sources. Their exports are linked
  above; they are not presented as a rebuild of `component/wine`.
- Private installation/admission helpers, game files, saves, accounts and raw
  capture material are outside this source repository.

Public revision equivalents preserve the locally audited code while omitting
machine-local email metadata from unpublished commits. The DXMT provenance map
records both identities and exact tree equality; prior build identities remain
historical facts. Publication does not rerun or upgrade their evidence.

For installed/build/runtime limits, read [status](STATUS.md). New PRs should name
their source revision and choose core, game-profile, experiment or tooling/docs
in the [PR template](../.github/PULL_REQUEST_TEMPLATE.md).
