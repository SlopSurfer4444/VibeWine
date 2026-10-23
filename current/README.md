# Installed post-Wave7 source checkpoint (2026-10-18)

**Historical snapshot.** “Installed”, “current” and “latest” below describe this
dated checkpoint. Active development is in [`components/`](../README.md#get-the-source),
and the current runtime evidence is in [status](../docs/STATUS.md). Do not edit
this snapshot as a second copy of the active component source.

This directory supersedes older Wave7 source only for the listed components. It contains source and portable fixtures, not a runtime installer or a complete reproducible Wine distribution. Local installation readback recorded the current DXGI and winemac hashes in `PROVENANCE.json`; identifiers do not distribute binaries. The private lifecycle/admission implementation, private deployment/game configuration profiles and raw observations are absent.

## General Wine fixes

- `ntdll/sync.c`: Unix `NtQueryDirectoryObject` captures its BOOLEAN restart argument into a byte before testing it. Unspecified upper register bits previously restarted enumeration and trapped Steam in `GetLogicalDrives`. Replace only `unix/sync.c` in `../ntdll/wave7/source`. The poisoned-upper-bits negative control fails on the original function and passes on the exported function. This is an ABI correctness fix, independent of executable names. Its installed binary is unchanged by this follow-up.
- `winemac/window.c`: top-level client surfaces retain Wine's authoritative projected client rectangle. This avoids losing pixels when reconstructing an extent from rounded DPI; the retained regression is 3413 versus 3420 pixels. Child surfaces add the client-to-visible offset only when the native content view is their parent; a client-view parent already supplies that offset.
- `winemac/window.c`, `cocoa_window.m` and `d3dmetal.c`: allocation/native-view failures return without publishing an incomplete replacement, preserve the prior view for the tested failures, and release owned bridge/surface storage. Cocoa creation stops before registering an observer for a nil view. This does not establish atomic construction, concurrent-destroy safety or recovery from arbitrary process-wide memory exhaustion.

Merge the six `winemac` files into `../native/wave7/source/winemac.drv`; the two headers are unchanged fixture context. `wine-fixes.patch` contains the earlier ntdll/surface delta against those retained Wave7 sources; apply `cursor-reset.patch` as well for the current driver. `surface-followup.patch` separately records the latest three-file change against the preceding installed source. Wine copyright notices and LGPL-2.1-or-later terms remain intact; see repository LICENSE.

## General DXGI mode-list correctness

`dxgi-source/src/dxgi/dxgi_output.cpp` now checks both count/fill HRESULTs in `FindClosestMatchingMode1`, searches only the actual returned count, treats an empty result as NOT_FOUND, and retries genuine MORE_DATA with a fresh count query. Retries stop after three attempts. Errors preserve the caller's output; an impossible successful count beyond capacity returns MORE_DATA. The old implementation could search zero-initialized tail entries after a shrink, including a zero refresh denominator. `dxgi-closest-mode.patch` is the exact one-file delta against the preceding installed DXGI source.

This fix applies with the resolution cap both disabled and enabled. Filtering, refresh matching, enumeration grouping and the application profile are unchanged. The matching source has been built offline; this export retains the source and fixtures, without private build paths, toolchain/donor archives or an assertion that a full Wine build is reproducible from this directory alone.

## DS3-specific DXGI compatibility

The existing resolution enumeration cap in `dxgi-source` is a separate compatibility option, enabled by the `DarkSoulsIII.exe` profile. It limits distinct resolutions while retaining consistent count/fill and legacy enumeration behavior. Its motivation was a fixed-size game-side mode list, not a universal DXGI API limit. Other applications retain the default behavior. The generic closest-mode fix above does not widen that profile.

This DXGI source derives from the retained installed DXMT donor plus the earlier reviewed changes and latest generic closest-mode fix. It is separate from the current D3D11 component in `../renderer/wave7/source`. DXMT MIT and third-party licenses remain inside `dxgi-source`. The mode-list follow-up did not replace D3D11. The later upstream follow-up below does replace D3D11; winemetal and paired Windows ntdll DLLs remain unchanged. An earlier D3D11-only artifact that lacked separately linked DXGI changes remains withdrawn.

## Verification and evidence limits

The latest DXGI component compiled and linked offline; the native driver built, received an ad-hoc signature and passed strict signature verification before private packaging. Local installation readback then matched the latest DXGI and winemac hashes. These facts establish build and installed identity, separately from runtime behavior.

The portable tests extract production functions and exercise mocked surroundings. They cover ABI argument handling, extent geometry, allocation/cleanup and DXGI count/fill changes. The surface fixture uses offscreen AppKit views, not a visible application. No game was started for this follow-up. Game/menu/display-transition, gesture/focus, clean relaunch and FPS acceptance of these latest binaries remain unverified; see `tests/README.md` for exact offline coverage.

Earlier owner reports described successful resolution changes, fullscreen exit/relaunch and a playable 1440p DS3 session. Those observations predate this follow-up and do not verify the latest binaries. Warm-session degradation, intermittent server login and poor invasion latency without VPN were also reported; thermal throttling, routing, shader work and CPU/GPU contention remain hypotheses. No controlled FPS gain is claimed.

No new controller implementation is claimed. Existing controller code and optional XInput experiments elsewhere retain their status; earlier working-control reports do not establish broad hotplug, Bluetooth/USB, vibration or multi-game coverage. No raw telemetry, network/account identifiers or private installation mechanisms are published.

## Latest installed upstream follow-up (2026-10-18)

These are general corrections without executable-name checks. They do not change the DS3 resolution-list profile above.

- **Cursor reset:** `winemac/mouse.c` incorporates [Sikarugir Wine eefbbc07](https://github.com/Sikarugir-App/wine/commit/eefbbc07a838ffc9e71a963fa3aec14c9cb5a1a2). A reset now requests an infinite clipping rectangle from the macOS backend and returns its status, instead of returning success without releasing clipping. This is not a Mission Control detector: whether the gesture delivers this reset in a game still needs testing. Fifteen local fixture cases passed normally and under ASan/UBSan; the old reset implementation failed the negative control.
- **DXMT task advancement:** `d3d11/dxmt_command.metal` incorporates [DXMT aadf0fbb](https://github.com/3Shain/dxmt/commit/aadf0fbbaa28c4260dcc14e69d8870c88fb05e97), advancing the index in the GS and TS argument-marshalling loops. Overlay this file at `renderer/wave7/source/src/dxmt/dxmt_command.metal` (repository-relative), or apply `dxmt-task-advance.patch` to that retained source tree. All other text files of the installed D3D11 source matched that tree. Two upstream PNG test assets remain omitted as in the previous export.

### Actual graphics build path

The helper must be recompiled, not merely edited beside an old binary: Metal source → `metal -c` → AIR → `metallib` → generated byte-array header → `dxmt_command.cpp` / `InternalCommandLibrary` → linked **d3d11.dll**. The private build regenerated that resource using Xcode and the Metal toolchain, rebuilt its consumer, and verified the exact compiled helper occurs once in the resulting D3D11 DLL. Replacing DXGI alone does not install this fix. `PROVENANCE.json` records the installed DLL and helper identities. Toolchains, binaries and private installation machinery are not redistributed; this remains a source export, not a turnkey reproducible distribution.

The new Metal source retains its upstream **LGPL-2.1-or-later** notice and author credits, even though much of DXMT is MIT. The repository's LGPL text applies to this file; keep the per-file notice. Other DXMT license notices remain in the retained renderer tree.

Ten CPU cases with six bounded old-source negative controls passed. Twenty native offscreen GPU comparisons (ten old-helper, ten rebuilt-helper) verified task output and guards. The old compiled helper wrote only task zero in these fixtures; no GPU hang was observed, and no hang or FPS improvement in DS3 is asserted. The new helper processed the expected tasks. These were correctness fixtures, not game performance measurements.

Installed readback matched the new cursor driver and D3D11, and a repeated installation was a no-op. No game was launched for this follow-up. Mission Control cursor release, gameplay use of these shader paths, sustained performance and FPS gains remain **unverified**.

Portable CPU reproduction: `python3 current/tests/task-marshal/test_marshal.py`. It extracts the production bodies, substitutes CPU types for Metal types, and adds a bounded loop guard for the deliberately defective source; it is not a GPU benchmark.
