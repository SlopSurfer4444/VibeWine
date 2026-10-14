# Wine macOS native integration — audit snapshot

Source-only research snapshot of a Wine 11.0 based macOS driver and accessibility bridge. This is **not an upstream release, complete Wine checkout, or redistributable game runtime**. It is supplied for architectural review and finding concrete bugs before further experiments.

## Start here

- `source.patch`: exact retained development patch against the archived base; SHA-256 in `PROVENANCE.json`.
- `source/dlls/winemac.drv`: 46 files byte-matched to the integrated candidate4 driver.
- `source/dlls/oleacc`: patched accessibility provider sources.
- `baseline/dlls`: corresponding Wine archive directories, enough to reproduce this patch locally.
- `SOURCE_INDEX.md`: review entry points and known limitations.
- `tests`: bounded offline fixtures. `python3 tests/run.py` requires macOS, Xcode Command Line Tools, and Python 3. No Wine process or game is launched.
- `LICENSE`, `upstream/LICENSE`, `upstream/AUTHORS`: retained licensing and authorship material. Wine-derived source retains its notices. Wine modifications in this audit snapshot are supplied under LGPL-2.1-or-later. Optional DXMT material retains its separate MIT license.

## Evidence boundaries

The paired native and Windows driver builds and the isolated accessibility fixture were exercised locally before this export. A fixture client successfully changed a checkbox and radio control; one separate action was rejected after a readiness timeout. Queue acceptance must not be confused with delivered UI action.

A real DS3 session was reported to have working controller, full-screen-sized borderless presentation, and a normal visible return to the desktop. That does **not** establish that exclusive fullscreen exit hangs are fixed. Native exclusive fullscreen previously required a forced machine restart; do not reproduce that casually on a working desktop. Direct native capture/release tests need an isolated machine and an independent recovery channel.

No FPS improvement is established. Existing frame measurements are observational and confounded by scene, resolution, background workload, and cache state. No before/after claim, shader-cause attribution, or per-frame percentile is supported by this export. The graphics translator and XInput optional candidates are outside this installed driver snapshot.

Ubisoft authentication was restricted on both compared runtime generations. Prince gameplay acceptance was deferred. CS2 and other games have not accepted this wave. The platform bridge does not turn arbitrary pixels rendered by a game into a semantic accessibility tree.

## Reproducibility and provenance gaps

The base archive identifies itself as Wine 11.0; its exact SHA-256 is retained, but no verified upstream Git commit or original download URL was retained in package provenance. Do not invent a commit match or assume pristine tag equivalence. The archive itself is intentionally omitted. `baseline/` is only the selected directories extracted from it.

The publication does not include the complete build system, compiler receipts, generated headers, engine, or deployment tooling. Therefore it is suitable for source review, patch reproduction within the included directories, and bounded fixtures, **not a claim of end-to-end reproducible runtime building**. Runtime deployment and the separate machine-management harness are explicitly outside this repository.

Tests were adapted only to address exported relative paths; production source and source.patch remain exact. Offline Foundation/Win32 doubles do not prove real COM, display hardware, Wine process lifecycle, games, or controller hotplug behavior.
