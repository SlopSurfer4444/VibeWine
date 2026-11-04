# Wave7 checked graphics source checkpoint

Current source and portable fixtures are in `renderer/wave7`, `native/wave7`, and
`ntdll/wave7`. This is the checked graphics successor. Read [acceptance limits](WAVE7_ACCEPTANCE.md): private GPU fixtures passed; actual DS3 fullscreen, input, relaunch and FPS acceptance remain pending.

Use [BUILDING_WAVE7.md](BUILDING_WAVE7.md) for offline checks and explicit build
inputs. [WAVE7_PROVENANCE.json](WAVE7_PROVENANCE.json) identifies the frozen
combination; [WAVE7_ADAPTATIONS.json](WAVE7_ADAPTATIONS.json) maps original hashes
to public hashes. Production source bytes are preserved. Only build/fixture path
adapters and authored documentation differ. Two unused upstream PNG test assets
are omitted under the retained text-only export policy.

Historical Wave5/Wave6 trees, receipts and tests remain available. Their status
and identities do not describe current Wave7. Source notices and licenses remain
with their respective components. No game/runtime binary or owner data is shipped.

## Historical material retained below

# Wine macOS integration lab — Wave6 integrated audit snapshot

Source-only Wine macOS driver and DXMT renderer research snapshot, succeeding public audit 421e6b0. This is not a complete Wine checkout, upstream release, or bundled game runtime.

## Current generation: Wave6

Start with `renderer/wave6/source`, `renderer/wave6/README.md`, `WAVE6_AUDIT_DISPOSITIONS.md`, and `NEXT_WAVE_BRIEF.md`. Wave6 supersedes the historical renderer trees described below; native Wine source remains Wave5. The exact installed renderer SHA256 is `ec522fc64a14f57c026f879414caa15aa4687a5294194dba2c35629ed61b9e22`. User confirmed full-display borderless DS3 and controller operation. Normal exit of this final build, actual exclusive fullscreen, real-game CUA and matched FPS gain remain unverified. New user observations: closing background apps removed perceived lag; increasing resolution in the game appeared to have no effect. Neither observation is a measured causal result.

Run `python3 tests/run_wave6.py` for the combined disposable offline source suite. No Wine/game launch or live changes occur. Binary distribution and complete runtime reproduction remain excluded.

## Historical Wave5 snapshot (retained for comparison)

The following sections describe the earlier generation; references to installed be876 renderer or uninstalled synchronization probes are historical. Current Wave6 incorporates V2 trace/probe work and uses a new opt-in DS3 owner-thread borderless route, not the optional external helper. See current dispositions for remaining limits.

## What is included

- `source/dlls`: exact Wave5 Wine driver and oleacc source; `source.patch` reproduces the selected source against `baseline/dlls`.
- `fixtures`: startup lifetime, AX refresh/deadline/provider composition, display-link coalescing and monitor cleanup regressions. `tests` retains display, Retina and native AX fixtures.
- `renderer/guarded/source`: DXMT fullscreen reentry guard plus preceding shader work.
- `renderer/diagnostic/source`: guarded DXMT with opt-in bounded CPU/GPU trace instrumentation. Its PE hash matches the installed DS3 renderer at export capture. Normal launches keep tracing disabled.
- `renderer/probes`: native trace controller and offline tests; no executable files.
- `optional/xinput`: retained earlier experimental source, not installed or hardware-accepted.

Run `python3 tests/run_wave5.py` on macOS with Xcode Command Line Tools and Python 3. It runs in a disposable copy, compiles native source fixtures and synthetic trace producer tests, and never starts Wine or a game. Test doubles do not prove real COM, GPU timing, window discovery, controller hotplug or display ownership.

## Current evidence and open failures

Wave5 native and Windows drivers were built and installed; isolated CUA standard-control actions passed. The diagnostic DXMT PE was also installed for DS3. Installed byte identities are recorded in `PROVENANCE.json`; binaries, deployment state and private paths are excluded.

Real-game computer-use discovery remains unresolved. An isolated AX fixture is not acceptance of Steam, Ubisoft or DS3. Borderless handling timed out during a real-game attempt, and a crash was observed after return to the desktop. Exclusive-fullscreen exit is NOT declared fixed: earlier attempts could leave a black desktop requiring forced restart. The source reentry guard fixes its bounded reproduced case, not every cause of display loss.

No FPS boost is established. Frame API intervals are not displayed frame rate. GPU completion timestamps need explicit validity and clock-domain handling. A controlled same-scene/resolution/settings/cache before/after comparison remains required. Prince testing is deferred; authentication restrictions occurred on both compared Wine generations. No general compatibility claim is made for other games.

## Licensing and reproducibility

Wine-derived source/modifications retain LGPL-2.1-or-later notices, LICENSE and upstream attribution. DXMT source retains its own MIT license and embedded third-party notices. Two PNG test assets per DXMT tree are omitted to keep this text-only export; the exported PE build does not use those tests.

Wine base archive SHA is retained, but verified upstream Git commit/download URL was not captured. Do not invent one. The archive itself and complete Wine build environment are not included. DXMT build scripts require an external LLVM-MinGW toolchain and exact release dependencies through `LLVM_MINGW` and `DXMT_RELEASE`; native Metal bridge and helper library are not redistributed here. This export supports source audit and offline fixtures, not an end-to-end reproducible installed runtime claim.

Reviewed optional successors have separate uninstalled status; discovery source remains excluded. `PENDING_ACCEPTANCE.md` records these boundaries.

## Reviewed optional successors

`optional/borderless` adds a reviewed watchdog generation/phase helper candidate and its source fixture. `optional/sync-probes` adds reviewed wait/flush/commit-reason instrumentation plus matching consumer/analyzer tests. Both remain uninstalled; these are not evidence of an FPS improvement or fixed exclusive-fullscreen exit. Installed be876 renderer/source remains unchanged in `renderer/diagnostic`.

Discovery remains an open failure even though a real CreateProcess target/helper were inventoried as distinct apps: the fixture supervisor bounded total lifetime to 180.82 seconds, while the getApp tool returned a timeout only after 667.9 seconds, after the fixture was already stopped. Control was not verified. A possible `_purecall` exit path is still a hypothesis; proprietary disassembly is excluded.
