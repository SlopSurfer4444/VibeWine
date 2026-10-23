# Current source/build checkpoint and installed baseline

Reconciled **2026-10-23**. The integrated candidate binds Wine
`e20af58312ef059e54f8d16767ee7b74cb8358e5` and DXMT
`828429ca6b142230399d98272207af7714f12895`. All 22 selected Wine/graphics
artifacts were freshly built together; this includes both PE architectures,
matching client/server, native window/font modules, graphics bridge and compiler.
It remains a selected closure over the retained engine/dependencies, not a full
reproducible engine distribution. Compiler cache output uses epoch 2.

The final signed candidate passed isolated native GDI font rasterization and its
blank-image control, all five offscreen GPU stages and three deliberately broken
GPU controls with correct natural exit statuses. A separate 32-run native PE
shutdown test passed after a reproduced SIGQUIT reentry repair. Source/sanitizer
fixtures cover the scheduler/persistence, query publication/retirement, native
configuration and command batching changes. See the [integrated build record](diagnostics/integrated-wave-20261023.md)
and [patch review](integrations/independent-performance-review-20261023.md).

**Installation and gameplay acceptance of this candidate are pending.** The
existing installed epoch-1 bundle is unchanged at this checkpoint. The table and
retained provenance below describe that previous installed baseline, not the new
candidate. No numerical FPS gain, universal game compatibility or autonomous
gameplay follows from these build/fixture results.

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
