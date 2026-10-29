# DXMT patch provenance

## Sustained continuation, 29 October 2026

Current component `a422a9324e6c655e43cfeb53423497e3ca390a0e`. The [sustained continuation](../diagnostics/sustained-wave-20261029.md) adds independently checked general repairs and bounded experiments.

Previous component `61aca2a3621386f1c6777972327149e48675883e`. The [next-wave report](../diagnostics/next-wave-20261028.md)
and [patch map](../PATCHES.md) separate general repairs, opt-in compilers,
measurement tooling and held negative findings. Full builds and source controls
pass; runtime adoption/activation remains independently observed. Older dated
sections keep their original identities.


## Frontier checkpoint,28 October 2026

Current component source: `8d0cef8dbcc8d1efbd0db97d4a0aee9e86d592d0`. The [frontier report](../diagnostics/frontier-wave-20261028.md)
records local independently reproduced general repairs, opt-in diagnostics,
matched builds and explicit held experiments. The [patch map](../PATCHES.md)
separates origins and game profiles. This source checkpoint does not change the
installed runtime; older dated sections retain their original identities.


Latest general hot-path work and independently reproduced correctness repairs
are recorded in [the 26 October report](../diagnostics/hotpaths-20261026.md).
Those source changes follow `96940d73` and are separate from the installed
renderer below. No game performance or runtime promotion follows from this map.


## FPS source wave, 25 October 2026

Current development pin: DXMT `c7e4ab45`, adding attributed borrowed-error
ownership and failed-pipeline work cancellation. Both complete matched variants
build. The installed experiment remains the preceding `25eff5e1` checkpoint
(build tree-equivalent `cb5fa11d`). See the
[new neighbor wave](../integrations/neighbor-wave-20261025.md).
The [integration evidence](../diagnostics/fps-wave-20261025.md) separates
source, complete selected builds and covered GPU correctness from installation
and gameplay performance. The [exact-tree publication map](fps-wave-publication-20261025.json)
binds local build revisions to public source. The prior playable selection is
unchanged by this wave; the dated checkpoints below retain their original scope.

## Current follow-up, 24 October 2026

The matched build source is `356cc60d500f11d59dd2cb215250b2a08b07e8ba`;
public equivalent `14e3a6a88288b8b69ff84b0b8c69c8c2af663385` has the exact
same tree. The [publication map](focused-publication-20261024.json) records all
three new commit pairs. Later publication-only test-reference changes preserve
production bytes. The selected closure is installed with verified hash readback
and no-op convergence; real-game exit/relaunch and FPS acceptance remain open.
See [the focused evidence](../diagnostics/focused-followup-20261024.md).

The dated checkpoints below retain their original scope and acceptance limits.

## Integrated candidate checkpoint, 2026-10-23

New candidate source: `828429ca6b142230399d98272207af7714f12895`; matched PE/native bridge/compiler, helper and epoch-2 cache namespace. It was built and signed in a separate
quarantine and passed the documented native controls. Installation, actual loaded
game modules and gameplay/performance remain separate pending checks. See the
[integrated record](../diagnostics/integrated-wave-20261023.md). The historical
source bindings and installed baseline below remain unchanged.


This page inventories the **matched-build baseline**, including its nine logical
local changes. Later development-only upstream imports and cache epoch 2 are
recorded in [the fork review](../integrations/fork-review-20261023.md) and
[component backport notes](../../components/dxmt/docs/upstream-backports-20261023.md).
They are not covered by the original matched-build or gameplay evidence below.

Reviewed 2026-10-23 against audited local build-source component
`5cf1662beee80e428810a49f0d5564569884b1bb`. Public publication identities
are distinguished below. This is a source and evidence inventory; it does not enable profiles, install binaries or claim an FPS gain.

## Upstream and local ancestry

The component retains the original [DXMT history](https://github.com/3Shain/dxmt).
Its upstream base is
[`50102f994b406f58f56d5d09f2ad20f9b90390a2`](https://github.com/3Shain/dxmt/commit/50102f994b406f58f56d5d09f2ad20f9b90390a2)
(`fix(nvngx): allow resources stored as void * to be read as D3D resources`,
authored by windlandneko, committed by Feifan He). Local Git inspection confirms
that this exact object is the merge base with the reviewed component, followed
by nine local commits. This identifies the base of this candidate; it is not a
claim that it is the latest upstream commit today.

VibeWine's `component/dxmt` branch carries the public component lineage. These
are VibeWine changes, not upstream-accepted DXMT changes. The six previously
unpublished local commits after `80f1d010` have separate public publication
identities that replace machine-local author/committer email metadata and use
the public account as publication committer. Each
public equivalent preserves its audited source tree, author display name,
author date and original message, with an `Original-Local-Commit` trailer.
The [publication map](../../components/dxmt/docs/PUBLICATION.json) records both
identities and exact tree equality. Public equivalent tip:
`e063c4a8e6053d0aab0320c58ce6c0bd98aa4002`; subsequent publication-only
documentation commits do not change production code. Earlier
published ancestry is retained without rewriting it. Public commit IDs therefore
identify equivalent source trees; they are not the IDs used in the earlier build
receipts. The underlying upstream authors and file notices remain in place.
The older snapshot publications used the SlopSurfer4444 author identity.

| Public commit, in application order | Origin and current purpose | Scope |
| --- | --- | --- |
| [`dc1e9fe0`](https://github.com/SlopSurfer4444/VibeWine/commit/dc1e9fe09094ff39872e76ac05d497545ce17502) | Port of earlier VibeWine renderer/bridge work onto the identified upstream base. Transactional swapchain changes, immutable queued presentation state, drawable geometry, shader/error cleanup, lock/query/task ownership, bounded mode enumeration and successful-only clear-pipeline caching. Keeps the newer upstream compiler, clear commands, stencil support, constants and keyed mutex architecture. Also carries the separately scoped profiles and opt-in tools below. | Mixed port; classify each behavior rather than enabling the whole set as a profile. |
| [`4eb67bdb`](https://github.com/SlopSurfer4444/VibeWine/commit/4eb67bdb4e328c7fc3470e99aacf82f2d3da0052) | Clarifies preservation of component license notices in `AGENTS.md`. | Documentation. |
| [`80f1d010`](https://github.com/SlopSurfer4444/VibeWine/commit/80f1d010a229f1c62e91e59113a67d21cb5c16e2) | Declares target-private WineMetal headers for WSI in the standard Meson graph. | Build integration. |
| [`44029f84`](https://github.com/SlopSurfer4444/VibeWine/commit/44029f84b230283c79ae51265f907d7695a8713c) | Local staging allocation retention fix: reclaim aged completed backing allocations while preserving numeric-name lookup, pending/current names, in-flight work and external `Rc` ownership. | General allocator correctness/retention; lazy cleanup after subsequent use. |
| [`39027b5d`](https://github.com/SlopSurfer4444/VibeWine/commit/39027b5d750e9a93958406149481142360e2ea61) | Preserves a headless GPU copy regression for staging lifetime. | Test source, not another runtime fix. |
| [`a5ad261e`](https://github.com/SlopSurfer4444/VibeWine/commit/a5ad261e80902319c0115a58e6d73e263809f535) | Local dynamic buffer/linear-texture retention fix: release excess completed FIFO references after the 300 completed-sequence reuse window, preserving current, in-flight and externally owned allocations. | General retention; sequences are not frames or seconds. |
| [`f9ab5e3a`](https://github.com/SlopSurfer4444/VibeWine/commit/f9ab5e3a765ad7dee93c8aa4d0fe2269727a5ede) | Local ring allocator fix: age completed blocks together, retire excess after the existing idle grace, retain the newest completed standard spare, and preserve in-flight blocks/references. Tail suballocation resets grace. | General retention; no demonstrated DS3 stall cure. |
| [`ce8921f3`](https://github.com/SlopSurfer4444/VibeWine/commit/ce8921f3efe50ade9c6e3977530d94d6c067151e) | Ports retained native bridge behavior into the matched current component and guards current GS/TS Metal marshal arithmetic against zero divisors/counts and wide-product overflow. Carries the explicitly gated DS3 aspect profile, optional acquisition diagnostics and experimental occlusion policy. | General marshal fix plus separately gated behavior. |
| [`e063c4a8`](https://github.com/SlopSurfer4444/VibeWine/commit/e063c4a8e6053d0aab0320c58ce6c0bd98aa4002) | Local persisted compiler-output namespace: shared 64-bit key combines upstream AIRCONV API version 26 with local output epoch 1 (`111669149697`) for shader tables and optional PSO archives. | General cache compatibility. API/ABI version stays 26; old tables and archives are preserved. A cache collision was a plausible hazard, not a proven freeze cause. |

The staging, dynamic and ring changes address separately reproduced retention
policies. They introduce no general proof of reduced game memory or better frame
times. Reclamation is lazy; unused resources can retain storage until destruction,
and a later burst may allocate again. See the
[staging](../diagnostics/staging-retention-20261023.md),
[dynamic](../diagnostics/dynamic-retention-20261023.md) and
[ring](../diagnostics/ring-retention-20261023.md) verification notes.

## Earlier sources and provenance confidence

The first local port is an integration commit, not one commit per original
experiment. Public historical inputs include:

- [`caf553d0`](https://github.com/SlopSurfer4444/VibeWine/commit/caf553d0747f699432b729b3ead8ee114fca7763): Wave6 integrated renderer source, patch, provenance and bounded borderless observations.
- [`d33a4f3c`](https://github.com/SlopSurfer4444/VibeWine/commit/d33a4f3ca32cd86b3a80c5b39867f4475a6973f6): published installed-era source snapshot, including the DS3 display-mode limiter.
- [`d5be4c53`](https://github.com/SlopSurfer4444/VibeWine/commit/d5be4c5332b62e53869239a5bb281a06c4e842c2): published general closest-mode count/fill correction and its regression fixture.

Those snapshot commits identify comparison material and publication history;
they are not parents inserted into the DXMT upstream lineage. The component
integration note also names reconstructed older overlay `9f13ff1e...`; that
object is unavailable in the reviewed object store, so its exact individual
patch authorship/ancestry is not independently established here.

For the later bridge port, the private audit binds the behavioral reference to
source commit `1eb6b43f21a96b62045700f94831522cab6600bc`. That reference is a
source binding, not a promised public commit link. The audit compares the actual
reference and current component. It retains the current checked main-thread
execution, display snapshots, layer application and feedback/listener lifetime;
ports acquisition/visibility/profile behavior; and brings guarded arithmetic
into the current Metal helper. The old hash-selected helper replacement and
original-AIRCONV donor trampoline are deliberately superseded by a matched build.

Confidence is high for the exact retained Git ancestry, nine local diffs and
current gates. The historical snapshots and private source bindings support
behavioral provenance. They do not establish a one-to-one original author or
external upstream PR for every line in the integration port; no such origin is
invented here. In particular, the local allocator and cache fixes are not
attributed to an unrelated DXMT upstream issue or release.

## General behavior, application profiles and experiments

| Behavior | Current admission | Classification |
| --- | --- | --- |
| Transactional resize/fullscreen state, queued presentation state, lock/query/task lifetime, mode count/fill, clear-pipeline retry | Ordinary component behavior. Clear creation retries at most once per second per failed key; only success is cached. | General correctness. |
| Staging/dynamic/ring reclamation and GS/TS arithmetic guards | Ordinary component behavior; no DS3 environment variable is needed for marshal guards. | General correctness/retention. |
| Display-mode resolution limiting | `dxgi.limitDisplayModeResolutions` defaults false. The case-insensitive `DarkSoulsIII.exe` application profile enables it and retains at most 32 distinct sizes, including all refresh variants within selected sizes. | DS3 compatibility profile; unsupported resolutions are not invented. |
| Owner-thread borderless fallback | Requires exact `DXMT_DS3_BORDERLESS=1` and the case-insensitive executable path suffix `\Game\DarkSoulsIII.exe`, a windowed swapchain, and its ownership/window safety checks. | Explicit DS3 profile, separate from generic fullscreen policy. |
| Native aspect fit/black bars | Requires both exact `DXMT_DS3_ASPECT_FIT=1` and `DXMT_DS3_MARSHAL_FIX=1`, and case-insensitive DS3 basename in argument zero or one; applied through both layer-property paths. | Explicit DS3 profile. `MARSHAL_FIX` remains a profile admission name; arithmetic guards themselves are general. |
| Skip occluded drawable acquisition | Exact `DXMT_SKIP_OCCLUDED_DRAWABLES=1`; default off. First-drawable allowance, unknown/stale recovery, 250 ms refresh and 1 s expiry remain. | Experiment; background screenshots can become stale. Not an FPS or freeze fix. |
| Native acquisition records | `DXMT_ACQUIRE_DIAGNOSTICS_DIR` must select a pre-existing owner-controlled, symlink-free absolute directory with mode 0700; absent by default. | Bounded diagnostic producer; no per-frame filesystem calls. |
| Trace | Nonempty `DXMT_CS2_TRACE_DIR`; no executable filter. Actual recording additionally requires the controller's `Open` state. | Measurement. |
| Compile diagnostics | `DXMT_CS2_COMPILE_DIAGNOSTICS=1` with no configured trace directory; logs bounded waits/compiles of at least 5 ms. | Diagnostic. |
| Early compile | `DXMT_CS2_EARLY_COMPILE=1`; no executable filter. | Scheduling experiment. |
| Recipe output | Nonempty `DXMT_CS2_RECIPE_DIR`; no executable filter; at most 4,096 save attempts. | Opt-in producer. |
| Graphics PSO archive | Nonempty absolute Unix `DXMT_CS2_PIPELINE_CACHE` path and executable basename `cs2`, `compile-probe` or `prepare-pipelines`. | Opt-in archive, not a generic DS3 setting. |

The upstream `d3d11.maxTessFactor` option is a quality tradeoff, disabled by
default; it is not a local VibeWine performance patch.

The acquisition sink records numeric identities, clocks and counters in one
exclusive mode-0600, 82,176-byte file per process, at most 1,024 records. It does
not capture window text, screenshots, credentials or input content. Its manual
7-day TTL/16 MiB session budget and active/unresolved/pinned holds remain in the
[component contract](../../components/dxmt/docs/VIBEWINE.md); no automatic cleaner
or aggregate quota is installed. Cache ownership, unsupported shader-cache TTL
and byte cap, optional PSO admission limits and manual cold-cache controls are
in [compiler cache compatibility](../../components/dxmt/docs/compiler-cache-compatibility.md).
This document adds no producer or retention mutation.

## The five matched graphics artifacts

All five artifacts come from the same component pin above, built with matching
headers, Wine import libraries/native dependencies, AIRCONV and Metal helper.
`winemetal.so` contains the statically linked matched AIRCONV; D3D11 embeds the
newly compiled helper exactly once. AIRCONV is not an independently replaced
sixth installed graphics artifact in this bundle.

| Runtime-relative artifact | Role | Recorded installed/ordinary-launch evidence |
| --- | --- | --- |
| `engine/lib/wine/x86_64-windows/d3d11.dll` | D3D11 renderer and embedded Metal helper | Installation readback and actual mapping in the ordinary DS3 process. |
| `engine/lib/wine/x86_64-windows/dxgi.dll` | DXGI adapter/output/swapchain-facing interfaces | Installation readback and actual mapping. |
| `engine/lib/wine/x86_64-windows/d3d10core.dll` | D3D10Core implementation | Installed bytes verified on disk; the observed DS3 process did not load it. |
| `engine/lib/wine/x86_64-windows/winemetal.dll` | PE side of WineMetal thunks/call interface | Installation readback and actual mapping. |
| `engine/lib/wine/x86_64-unix/winemetal.so` | Native Metal bridge, command decoding and matched AIRCONV | Installation readback and actual mapping. |

New PE code cannot safely use the older bridge merely because some Unix-call
IDs remain append-only: the newer table tail, payloads, packed swizzles and
command decoder differ. Keep the PE libraries, native bridge/compiler, headers
and helper together. Wine driver/NTDLL, controller and network candidates were
retained by the private installation transaction; those are separate component
provenance, not code rebuilt by these five graphics artifacts.

The source receipt records archive SHA-256
`6541eeed726251cdd39b3779d3781dd6efb52882f30086b4a9dce424fe2cc38d`
and the matching DirectX header gitlink
[`9df86f23`](https://github.com/misyltoad/mingw-directx-headers/tree/9df86f2341616ef1888ae59919feaa6d4fad693d).
The private receipts additionally bind the complete build, signed installed files
and installer/control checkpoint
`6a2a510cba7db8e13c29d440134983ddff3e1114`. That checkpoint is not the graphics
source commit. Paths, profiles, session IDs and raw observations are omitted
from this public inventory. Build-output and signed-install hashes are different
evidence stages; do not assume a pre-sign native hash identifies installed bytes.

## Verification and remaining limits

The retained integration evidence records a complete 161-step matched build,
AIRCONV CLI smoke, native dependency inspection, source-extracted regression
suites, 3,826 hardware copy checks with 512 byte-exact snapshots, and a shader
fixture with 27 golden pixels plus a deliberately failing corrupted control.
These are previous checked receipts, not checks rerun during this documentation
audit. See the [matched-bundle acceptance note](../diagnostics/full-bundle-20261023.md).

Reproduce focused checks from `components/dxmt`. C++ source fixtures require
Python and `clang++` with ASan/UBSan. Native bridge/profile fixtures additionally
require macOS, an Apple SDK and the documented frameworks:

```sh
python3 tests/vibewine/compile_regressions.py
python3 tests/vibewine/clear_retry.py
python3 tests/vibewine/closest-mode/run.py
python3 tests/vibewine/mode-limit/test_modes.py
python3 tests/vibewine/staging-retention/run.py --sanitize
python3 tests/vibewine/dynamic-retention/run.py --sanitize
python3 tests/vibewine/ring-retention/run.py --sanitize
python3 tests/vibewine/native/run.py
python3 tests/vibewine/bridge-port/run.py
python3 tests/vibewine/bridge-port/run_occlusion.py
python3 tests/vibewine/bridge-port/run_profile.py
python3 tests/vibewine/bridge-port/test_marshal.py
```

The allocator fixtures compile production bodies against counted mock buffers;
native/profile fixtures mock Foundation/Metal boundaries; the marshal fixture
runs source-extracted Metal arithmetic through a CPU C++ shim with defective
controls. The C++ source fixtures and macOS-specific native fixtures establish
only their documented source behavior, not game/GPU benchmark results. `--arch x86_64` variants are available for
the retention suites on a suitable macOS host. Baseline commands in the retention READMEs use public
predecessors `39027b5d` (dynamic baseline) and `a5ad261e` (ring baseline),
whose trees equal local audited `68ef6024` and `6f21900c`, respectively.
Fresh public clones do not require the unpublished original revision objects.
GPU guests and a complete matched build are separate, heavier acceptance stages; see [building](../BUILDING.md)
and each fixture README rather than replacing those stages with a mock pass.

A user reported good gameplay in the matching private trial. The permanent
ordinary route subsequently passed installation readback, initial title-screen
launch, same-process mappings and an already-running response on a repeated
launch request. Sustained gameplay on that permanent route, reliable native
keyboard control, and normal menu exit followed by relaunch with Steam open
remain unverified. The title session also showed a server-disconnection message.
No network repair, universal compatibility, eliminated long stalls or measured
FPS improvement is established. A pre-existing source limitation also remains:
failed missing-drawable fallback blit-encoder allocation needs end-to-end
command-buffer failure propagation; the component integration notes track it.

## Attribution and licenses

The upstream Git history and original authorship are retained, including DXMT
work by Feifan He/3Shain and CodeWeavers and contributions by other upstream
authors. This inventory does not reattribute imported files to VibeWine. The
current [LICENSE](../../components/dxmt/LICENSE) declares LGPL-2.1-or-later with
copyright 2023–2026 Feifan He for CodeWeavers; preserve
[COPYING.LIB](../../components/dxmt/COPYING.LIB). The upstream license transition
is commit
[`ec357fd5`](https://github.com/3Shain/dxmt/commit/ec357fd582f0b7e0fee3803f6102ab786f1ba616).
[LICENSE.OLD](../../components/dxmt/LICENSE.OLD) records the MIT license through
v0.80; it does not make the current whole component MIT. Preserve separate file
notices and bundled licenses such as
[src/util/dxvk.LICENSE](../../components/dxmt/src/util/dxvk.LICENSE), plus the
licenses of external dependencies. VibeWine's AI-contribution policy applies to
its own derivative branch and does not replace upstream contribution policy.
