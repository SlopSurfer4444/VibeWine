# Current source, installation and checks

Reconciled **25 October 2026**. This is the authoritative current checkpoint;
dated snapshot documents retain their original historical claims.

| Layer | Current state | What it does not establish |
| --- | --- | --- |
| Development source | Wine `85864be4`; DXMT `25eff5e1`: seven gated renderer experiments, MSync WaitAll rollback/attribution, and prior early scene submission | A component branch tip is not an installed runtime |
| Matched build | Both complete 161-target DXMT variants, paired Unix NTDLL/server; PE/native/helper and LLVM 15.0.7; compiler epoch 2 off / 3 cleanup on | Full reproducible Wine distribution or every optional feature |
| Installation | Selected 22-file closure installed in the owner's ordinary DS3 runtime; hash readback matched and repeated installation made no changes; prior recovery retained | This follow-up's real-game exit/relaunch or FPS acceptance |
| New experiments | Seven new default-off switches plus independent scene submission; focused CPU controls, five generic GPU stages and four dedicated pass cases per build, six native shader images per helper and connected pixel negatives pass | Dynamic DISCARD/residency admission, real MetalFX, production Present/capture, displayed FPS, latency or memory improvement |
| Latest correctness checks | Terminal release old-fail/new-pass controls on arm64 and Rosetta, real NTDLL lifecycle controls, production MinGW-header compilation, five offscreen GPU stages and three rejected broken controls | A cure for every game hang |
| Latest diagnostics | 178 offline telemetry checks; process-bound observed phase labels and opt-in compiler/wait coverage; collection adapter has automatic sampling off by default | Automatic scene detection, ten-minute detailed trace coverage or measured observer overhead |
| Game observations | Earlier owner reports described usable play under background load; a previous unlabeled 1080p stream was retained | Controlled before/after FPS, clean gameplay-only timing or a causal network repair |

The **installed** graphics still correspond to local DXMT
`356cc60d500f11d59dd2cb215250b2a08b07e8ba` and its tree-identical
[public equivalent](provenance/focused-publication-20261024.json).
The latest experimental pair uses local build source `36385509`, public
tree-equivalent `cb5fa11d`; later pin changes are test/documentation only. Eight
booleans differ: seven new switches and scene submission. Both use the paired
new Wine MSync source. The previous scene-only control package remains a separate
checkpoint. No variant from the new wave was installed or promoted. See
[the integration evidence](diagnostics/fps-wave-20261025.md) and
[exact hashes and accepted checks](diagnostics/fps-wave-20261025.json).
The 25 October baseline attempt launched the unchanged installed game, observed
menu responses and repeated connection-loss dialogs, then ended it with macOS
Quit. No stable offline world or controlled gameplay timing was accepted.
The task-owned prefix and observer were stopped. The private install/admission
tooling and licensed retained engine dependencies are not distributed here.

See [the focused changes and checks](diagnostics/focused-followup-20261024.md)
and the [earlier integrated build record](diagnostics/integrated-wave-20261023.md).

## Open acceptance

- Per-switch and combined performance/latency/memory comparison, actual dynamic
  DISCARD and immutable residency GPU admission, idle memory effects, contested
  WaitAll runtime, actual MetalFX scaler/presenter and readable split-frame capture.

- Real-game normal exit and a fresh launch with Steam left open on the latest
  installed follow-up; investigate any surviving CPU-heavy process.
- Actual menu/loading/gameplay phase labels in a new capture; comparable cold
  and warm frame times at fixed scene, settings, resolution and power state.
- Compiler/wait profile activation through the supported launch route. The
  producer/decoder are implemented; the installed launcher does not yet expose
  this profile option. Filtering does not enlarge the fixed trace capacity.
- Resolution/Retina transitions, Mission Control cursor release, and reliable
  input on another Space remain separate live checks.
- Intermittent server access and multiplayer latency remain unlocalized; no
  networking or FPS cure is inferred from an earlier successful session.

## Earlier checkpoint: before the 24 October promotion

The table below describes the retained epoch-1 baseline and its evidence. It is
historical, not the current installation inventory.

## Previous installed baseline

| Layer | Observation | Limit |
| --- | --- | --- |
| Source | Pinned complete Wine/DXMT trees; per-patch origins and exact source deltas | Wine vendor import has no verified exact WineHQ Git ancestor |
| Graphics build | Matched D3D11, DXGI, D3D10Core, WineMetal PE and native WineMetal with static AIRCONV | Not a complete production-feature Wine rebuild |
| GPU correctness | Copy guest: 3,826 checks, 1,309 queries, 512 byte-exact snapshots; shader guest: 27 golden pixels; negative control rejected | Correctness fixtures, not FPS measurements |
| Permanent install | Ten selected runtime files, four graphics prefix pairs and lifecycle matched; repeat apply was a no-op | Private installer/licensed donor dependencies are not distributed |
| Ordinary launch | Application launched DS3 with native confirmation; new graphics/XInput/WS2 mappings and title screen verified | In-world gameplay on this permanent route not measured in this follow-up |
| Repeated request | Launcher recognized the live game without another session | Menu exit followed by launch with Steam left open remains unverified here |
| Gameplay report | Owner reported smooth DS3 play, subsequently also with other applications active and on battery | Uncontrolled observation; no attributed patch-by-patch or numerical FPS gain |
| Computer use | Window discovery and screenshots worked | Reliable native keyboard input/full autonomous gameplay remain incomplete |

The matching private graphics trial preceded permanent installation. Keep its
positive gameplay report separate from later installation/title checks. See the
[full-bundle report](diagnostics/full-bundle-20261023.md).

The five graphics artifacts correspond to the DXMT build revision and its
[public source equivalent](provenance/dxmt.md). Retained Wine modules are a
mixture of identified source revisions. NTDLL was partly relinked against retained
objects and subsequently signed; publishing source/recipes does not establish a
full reproducible rebuild. See [Wine provenance](provenance/wine.md) and the
[exact patches](../patches/runtime-2026-10-23/README.md).

The development Wine component pin is distinct from those experimental branches and
native snapshots. Cloning, updating Git branches and workspace checks do not
install or switch an owner's runtime.

## Open checks

- Controlled before/after frame times at identical scene, resolution, settings
  and cache state; sustained play and warm-session behavior.
- Normal fullscreen exit and a fresh target launch while Steam remains open,
  on the permanent bundle.
- Resolution/Retina transitions and Mission Control cursor release on additional
  runs/applications.
- Reliable focus/input and separate-workspace control. AX discovery or a reported
  key event alone is not an observed game response.
- Intermittent server failures and poor multiplayer latency. A disconnection
  message appeared during the ordinary title test; its cause is not established.

Earlier captures recorded long Metal submission/completion waits. Recent memory
reclamation fixes reproduce and correct allocator behavior in tests; that alone
does not prove the cause of those old stalls.

Use the [build guide](BUILDING.md), [patch map](PATCHES.md) and component fixtures.
Report source checks, compilation/linking, installation, observed behavior and
measured performance separately. CI coverage is declared by the workflow; this
page does not convert local results into CI passes.
