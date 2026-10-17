# Installed post-Wave7 source checkpoint (2026-10-18)

This directory supersedes older Wave7 source only for the files/components listed below. It contains source, not a runtime installer. Installed identities were read back locally on the publication date; binary hashes in PROVENANCE.json are identifiers, not distributed binaries. The private lifecycle/admission implementation, game profiles and raw observations are deliberately absent.

## General Wine fixes

- `ntdll/sync.c`: the current Unix NtQueryDirectoryObject captures its BOOLEAN restart argument into a byte before testing it. The earlier optimized implementation interpreted unspecified upper register bits, repeatedly restarting directory enumeration and trapping Steam in GetLogicalDrives. The current source is based on `../ntdll/wave7/source`; replace only `unix/sync.c`. The original exact function fails a poisoned-upper-bits negative control, while the patched function and isolated runtime API probe passed. This is a general ABI correctness fix, not a Steam name check.
- `winemac/window.c`: top-level client surfaces use the authoritative client rectangle rather than querying a rounded DPI-converted rectangle. At the observed Retina scale the old path produced 3413 pixels for a 3420-pixel-wide surface; the patch removes the resulting uncovered edge. Child-window behavior is preserved. Merge these three files into `../native/wave7/source/winemac.drv`; only window.c changes, while the two headers are supplied for fixture context. This is a general top-level window coordinate fix. The exact-function fixture checks both the regression and unchanged cases.

Wine file copyright notices and LGPL-2.1-or-later terms remain intact; see repository LICENSE. These files do not constitute a full Wine distribution.

## DS3-specific DXGI compatibility

`dxgi-source` is the matching DXGI component source, based on the retained installed DXMT donor archive plus five reviewed source changes. It is separate from the current D3D11 component in `../renderer/wave7/source` and must not be conflated with that build. The resolution enumeration cap is enabled by the DarkSoulsIII.exe application profile. It limits distinct resolutions while preserving consistent count/fill and legacy enumeration behavior. The motivation was a fixed-size game-side mode list, not a universal DXGI API limit. Other applications retain the default behavior.

D3D11, winemetal and the paired Windows ntdll DLLs were not replaced by these fixes. An earlier D3D11-only attempted artifact was withdrawn because it did not contain the separately linked DXGI changes; it is not included or recommended. DXMT MIT and third-party licenses are retained inside dxgi-source.

## Controller and evidence limits

No new controller implementation is claimed in this checkpoint. Existing controller code and optional XInput experiments elsewhere in the repository retain their original status. The owner has reported working controls during DS3 sessions; that does not establish broad hotplug, Bluetooth/USB, vibration or multi-game acceptance.

The owner reported successful resolution changes, fullscreen exit and relaunch, and a playable 1440p session. These are bounded observations, not automated coverage of every display transition. The owner also reported degradation after warming up, intermittent server login and poor invasion latency without VPN. Thermal throttling, network routing, shader work and CPU/GPU contention remain hypotheses; there is no controlled before/after FPS gain claim. No raw game telemetry or network/account identifiers are published.

The private installed delivery revision is recorded for provenance only; its machine-management implementation is not part of this public repository. Review source correctness and portable fixtures here independently of those private installation mechanisms.
