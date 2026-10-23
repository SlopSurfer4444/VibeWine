# Current verified checkpoint

Last reconciled: **2026-10-23**. This describes a private macOS test installation
and source published for inspection. It is not a downloadable binary release.

The workspace development pins now include [selected source backports](integrations/fork-review-20261023.md)
from Wine, Proton-GE, Highball, Dormison, macOS forks and DXMT. The follow-up
adds address-wait timeout handling, inactive window ordering, deferred Metal-view
teardown, SM50 ownership and optional Vulkan portability enumeration. Their
focused regressions are separate from the earlier installed-bundle evidence
below. No newly matched runtime build
or activation of these backports has occurred. Compiler output uses epoch 2 in
development; the installed matched bundle retains epoch 1.

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
