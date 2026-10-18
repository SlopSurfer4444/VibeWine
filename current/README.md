# Installed post-Wave7 source checkpoint (2026-10-18)

This directory supersedes older Wave7 source only for the listed components. It contains source and portable fixtures, not a runtime installer or a complete reproducible Wine distribution. Local installation readback recorded the current DXGI and winemac hashes in `PROVENANCE.json`; identifiers do not distribute binaries. The private lifecycle/admission implementation, private deployment/game configuration profiles and raw observations are absent.

## General Wine fixes

- `ntdll/sync.c`: Unix `NtQueryDirectoryObject` captures its BOOLEAN restart argument into a byte before testing it. Unspecified upper register bits previously restarted enumeration and trapped Steam in `GetLogicalDrives`. Replace only `unix/sync.c` in `../ntdll/wave7/source`. The poisoned-upper-bits negative control fails on the original function and passes on the exported function. This is an ABI correctness fix, independent of executable names. Its installed binary is unchanged by this follow-up.
- `winemac/window.c`: top-level client surfaces retain Wine's authoritative projected client rectangle. This avoids losing pixels when reconstructing an extent from rounded DPI; the retained regression is 3413 versus 3420 pixels. Child surfaces add the client-to-visible offset only when the native content view is their parent; a client-view parent already supplies that offset.
- `winemac/window.c`, `cocoa_window.m` and `d3dmetal.c`: allocation/native-view failures return without publishing an incomplete replacement, preserve the prior view for the tested failures, and release owned bridge/surface storage. Cocoa creation stops before registering an observer for a nil view. This does not establish atomic construction, concurrent-destroy safety or recovery from arbitrary process-wide memory exhaustion.

Merge the five `winemac` files into `../native/wave7/source/winemac.drv`; the two headers are unchanged fixture context. `wine-fixes.patch` contains the complete ntdll/winemac delta against those retained Wave7 sources. `surface-followup.patch` separately records the latest three-file change against the preceding installed source. Wine copyright notices and LGPL-2.1-or-later terms remain intact; see repository LICENSE.

## General DXGI mode-list correctness

`dxgi-source/src/dxgi/dxgi_output.cpp` now checks both count/fill HRESULTs in `FindClosestMatchingMode1`, searches only the actual returned count, treats an empty result as NOT_FOUND, and retries genuine MORE_DATA with a fresh count query. Retries stop after three attempts. Errors preserve the caller's output; an impossible successful count beyond capacity returns MORE_DATA. The old implementation could search zero-initialized tail entries after a shrink, including a zero refresh denominator. `dxgi-closest-mode.patch` is the exact one-file delta against the preceding installed DXGI source.

This fix applies with the resolution cap both disabled and enabled. Filtering, refresh matching, enumeration grouping and the application profile are unchanged. The matching source has been built offline; this export retains the source and fixtures, without private build paths, toolchain/donor archives or an assertion that a full Wine build is reproducible from this directory alone.

## DS3-specific DXGI compatibility

The existing resolution enumeration cap in `dxgi-source` is a separate compatibility option, enabled by the `DarkSoulsIII.exe` profile. It limits distinct resolutions while retaining consistent count/fill and legacy enumeration behavior. Its motivation was a fixed-size game-side mode list, not a universal DXGI API limit. Other applications retain the default behavior. The generic closest-mode fix above does not widen that profile.

This DXGI source derives from the retained installed DXMT donor plus the earlier reviewed changes and latest generic closest-mode fix. It is separate from the current D3D11 component in `../renderer/wave7/source`. DXMT MIT and third-party licenses remain inside `dxgi-source`. D3D11, winemetal and paired Windows ntdll DLLs were not replaced by this follow-up. An earlier D3D11-only artifact that lacked separately linked DXGI changes remains withdrawn.

## Verification and evidence limits

The latest DXGI component compiled and linked offline; the native driver built, received an ad-hoc signature and passed strict signature verification before private packaging. Local installation readback then matched the latest DXGI and winemac hashes. These facts establish build and installed identity, separately from runtime behavior.

The portable tests extract production functions and exercise mocked surroundings. They cover ABI argument handling, extent geometry, allocation/cleanup and DXGI count/fill changes. The surface fixture uses offscreen AppKit views, not a visible application. No game was started for this follow-up. Game/menu/display-transition, gesture/focus, clean relaunch and FPS acceptance of these latest binaries remain unverified; see `tests/README.md` for exact offline coverage.

Earlier owner reports described successful resolution changes, fullscreen exit/relaunch and a playable 1440p DS3 session. Those observations predate this follow-up and do not verify the latest binaries. Warm-session degradation, intermittent server login and poor invasion latency without VPN were also reported; thermal throttling, routing, shader work and CPU/GPU contention remain hypotheses. No controlled FPS gain is claimed.

No new controller implementation is claimed. Existing controller code and optional XInput experiments elsewhere retain their status; earlier working-control reports do not establish broad hotplug, Bluetooth/USB, vibration or multi-game coverage. No raw telemetry, network/account identifiers or private installation mechanisms are published.
