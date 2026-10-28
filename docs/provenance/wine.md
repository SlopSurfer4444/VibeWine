# Wine patch origins and installed-source gap

## Frontier checkpoint,28 October 2026

Current component source: `bb2c891987c9551329fc7dfbb7c7f41abfceb582`. The [frontier report](../diagnostics/frontier-wave-20261028.md)
records local independently reproduced general repairs, opt-in diagnostics,
matched builds and explicit held experiments. The [patch map](../PATCHES.md)
separates origins and game profiles. This source checkpoint does not change the
installed runtime; older dated sections retain their original identities.


## FPS source wave, 25 October 2026

Current development pin: Wine `5d4b43e4`, adding Paul Gofman's attributed
address-queue publication repair while preserving the earlier timeout drain.
The changed PE unit compiles for i386/x86_64; the installed paired Unix
NTDLL/server remains the preceding `85864be4` source checkpoint. See the
[new neighbor wave](../integrations/neighbor-wave-20261025.md).
The [integration evidence](../diagnostics/fps-wave-20261025.md) separates
source, complete selected builds and covered GPU correctness from installation
and gameplay performance. The [exact-tree publication map](fps-wave-publication-20261025.json)
binds local build revisions to public source. The prior playable selection is
unchanged by this wave; the dated checkpoints below retain their original scope.

## Integrated candidate checkpoint, 2026-10-23

New candidate source: `e20af58312ef059e54f8d16767ee7b74cb8358e5`; selected PE/native/client/server closure, FreeType headers 2.14.3 and retained library-search paths. It was built and signed in a separate
quarantine and passed the documented native controls. Installation, actual loaded
game modules and gameplay/performance remain separate pending checks. See the
[integrated record](../diagnostics/integrated-wave-20261023.md). The historical
source bindings and installed baseline below remain unchanged.


This inventory describes the **pre-backport pin** below and retained installed
modules. Later development-only Wine/Proton-GE/Highball imports are recorded in
[the fork review](../integrations/fork-review-20261023.md) and the component's
`vibewine/UPSTREAM_BACKPORTS.md`. They do not change the named bases or original
source hashes of the retained runtime patches.

Audited 2026-10-23 against the workspace Wine pin
`7469fcf53c4f537dfe305e961bffb0495d7b750d`. This page distinguishes the
published development baseline from retained source evidence for the later
experimental runtime. It does not claim a fresh whole-engine build, binary
reproducibility, gameplay acceptance, or a performance improvement.

## Source baseline

The [Wine component](https://github.com/SlopSurfer4444/VibeWine/tree/7469fcf53c4f537dfe305e961bffb0495d7b750d)
starts at vendor import `35b5f78f2d4e4e22303270af0e983785244b67e9`.
Its corresponding-source archive SHA-256 is
`7be5819017b34f09670293f2be7ed9f4476734b8f42dab121a8b74e6619c92a8`.
The packaged description identifies CodeWeavers CrossOver 26.3 / Wine 11.0,
including earlier vendor changes for Steam CEF, exceptions and generated-code
caching. This is the archive's description, not a verified upstream Git identity.
The archive's original download URL and exact Wine ancestor remain **unknown**.

The import is an independent Git root. [Wine upstream](https://gitlab.winehq.org/wine/wine)
and the [Wine mirror](https://github.com/wine-mirror/wine) identify the upstream
project; they do not establish ancestry for this archive. Preserve this distinction
when reviewing backports. The component's
[README](https://github.com/SlopSurfer4444/VibeWine/blob/7469fcf53c4f537dfe305e961bffb0495d7b750d/vibewine/README.md),
[ordered overlays](https://github.com/SlopSurfer4444/VibeWine/blob/7469fcf53c4f537dfe305e961bffb0495d7b750d/vibewine/source-overlay.json)
and vendor manifest retain the file-level source map.

## Which patch came from where

| Change | Origin and exact source | Classification / public baseline |
| --- | --- | --- |
| Cursor clipping reset | Imported from [Sikarugir Wine `eefbbc07a838ffc9e71a963fa3aec14c9cb5a1a2`](https://github.com/Sikarugir-App/wine/commit/eefbbc07a838ffc9e71a963fa3aec14c9cb5a1a2); `dlls/winemac.drv/mouse.c`, retained as `current/cursor-reset.patch` | General correction; included in the pin. It asks the backend to release clipping and returns the backend status. Mission Control delivery/game behavior is a separate check. |
| Native accessibility and application lifecycle | Local VibeWine work, integrated by [`5783983b8f6d261c86c8b548e667e0bc600eb92c`](https://github.com/SlopSurfer4444/VibeWine/commit/5783983b8f6d261c86c8b548e667e0bc600eb92c); `dlls/oleacc/`, `dlls/winemac.drv/ax_*`, `cocoa_app.*`, `cocoa_main.m`, `cocoa_window.*`; native application routing in `dlls/ntdll/unix/loader.c` and `native_app_route.h` | Reusable native/AX behavior; included. The AX bridge remains opt-in. Source does not invent accessible controls for game-rendered UI. |
| Retina/client-surface geometry, failure cleanup, display policy and main-thread executor | Local VibeWine corrections in the same integration and ordered `current/winemac/` overlays; `window.c`, `cocoa_window.m`, `d3dmetal.c`, `display.c`, `wave7_display_edr.inc`, `wave7_main_executor.inc` | General native changes; included. See [historical source/check limits](../../current/README.md). |
| `NtQueryDirectoryObject` BOOLEAN argument | Local ABI fix, integrated from `current/ntdll/sync.c` into `dlls/ntdll/unix/sync.c` | General correctness fix; included. It masks unspecified upper argument-register bits before testing restart. |
| Explicit AX Raise activates its native application | Local component commit `fb8f4515858c65798a2873fe2f6402f6df955167`; `dlls/winemac.drv/ax_window_identity.inc` | General action handling; absent from the pin, present in retained installed-driver source. Passive AX reads remain passive; accepted submission does not prove OS focus or game input. |
| Process-local synthetic XInput and expiry diagnostics | Local experiment `fbe0e3de434aa611b352aae661eacf87c4ee2f85`; `dlls/xinput1_3/main.c`, `test_backend.h`, `tests/process-local/` | Opt-in input test backend; absent from the pin. It is separate from the older [optional XInput candidate](../../optional/README.md), and supplies neither a system device nor a background-game simulation fix. |
| Socket metadata, lifetimes and select/WSAPoll readiness | Local experiments beginning at `503ae94acd7b16c849b4e2febc34af0ac32cc0e9`, retained at `0b4261775ea2016db16d7a77aa1e3586c808073f`; `dlls/ws2_32/`, `dlls/ntdll/unix/{socket.c,unix_private.h,virtual.c}`, `include/wine/netdiag*.h` | Opt-in bounded diagnostics; absent from the pin. These observe networking and do not fix login, routing, TLS or game-server behavior. |
| DS3 startup visibility snapshot | Local g20 driver source baseline `407e2d1c0a21e3aacf0259424e3636246ec149ed`; `visibility_guard.h`, `visibility_snapshot.inc`, `visibility_startup.inc` and `d3dmetal{,_objc}.{c,m}` integration | DS3-specific diagnostic, exact image guard plus explicit opt-in; absent from the pin. Keep it out of universal default policy. |
| Drawable/event-queue phase diagnostics | Local `4e6b235421bf3ded37b08ca12928e1998285c390`, atop that visibility baseline; `cocoa_event.m`, `d3dmetal_objc.m`, `drawable_phase_diag.h` | Off by default; exact `VIBEWINE_DRAWABLE_PHASE_DIAG=1` enables two sites, each capped at 128 attempted records for intervals of at least 100 ms. Absent from the pin; records may contain process/thread IDs and scalar addresses. |
| Msync server-disconnect retry guard | Local `c5f9649ba8d4eeb35d7d13c716468c740fdd9b6f`, retained by source checkpoint `611fe80cdfb09377f94c505d09dad962c6bee2e3`; `ntdll/wave7/source/unix/msync.c` | General failure-path candidate; absent from the pin. Undoes this waiter's references and terminates the thread after failed registration instead of retry-spinning. It does not explain or repair the original Wine-server exit. |

The XInput and network revisions are published as separate experimental branches
linked below. Native-driver/MSync private checkpoint hashes identify retained
evidence; their source is supplied by the exact patches, not by publishing the
private task histories.

The DS3 resolution-list cap belongs to the separate DXGI/DXMT profile described
in [current source notes](../../current/README.md); it is not a Wine-wide display
limit. Keep its game policy separate from the general DXGI closest-mode fix.
Graphics patches and their upstream attribution belong in the renderer provenance.

## Retained installed modules: five files, several source lines

The 2026-10-23 read-only provenance inspection bound the five retained non-graphics
artifacts in the g24 preparation to the earlier g23 bytes and their actual sources.
It did not rebuild them. The installed bundle is **not** a full rebuild of the
workspace Wine pin or of any single later source commit.

| Artifact | SHA-256 | Actual source/build relationship |
| --- | --- | --- |
| `xinput1_3.dll` | `01836e174719a8704413afe38928c4cba009c84dd8985ea36878e920df6c6eda` | `fbe0e3de434aa611b352aae661eacf87c4ee2f85`, `dlls/xinput1_3`; retained process-local build recipe and compiler/linker/import receipt. |
| `winemac.so` | `c73bfc3cba57b00b7df902db10faa7c19231414cec31c96b889a0799a79736a2` | `4e6b235421bf3ded37b08ca12928e1998285c390`, `source/winemac.drv`; two replacement Objective-C objects linked with exact donor objects, then ad-hoc signed. |
| `ws2_32.so` | `997c4ae12397a92a2aa43390a72b72b563f3630039dc451916cfcbf8e5857481` | Network build-v4, frozen source/configured headers later matched to `0b4261775ea2016db16d7a77aa1e3586c808073f`. |
| `ws2_32.dll` | `1bc4236fbf8e0658e8fa5776181b2e9e63f298fab00a14314546f5b3158ff52a` | The same build-v4 group; its added Unix bridge ordinal requires the matched PE/Unix pair and instrumented Unix ntdll. |
| `ntdll.so` | `82f690172760ab61cb538702a41367b888beea96869ceded781e581fef103702` | Composite: network build-v4 objects/headers, replacement `msync.c` from the `611fe80c` checkpoint / `c5f9649b` fix, relink in the matched-g18 recipe, then g19 ad-hoc signing. No full `611fe80c` rebuild. |

For network build-v4, the original receipt observed dirty source at
`790a7da32a8aba7d782ed03c4df7eaf11ad1c2b1`. The later handoff matched all 1,205
source-input hashes to the frozen snapshot and clean committed `0b426177` tree.
That establishes source binding after the build; it must not be described as
clean-at-build. Network build scripts/receipts live outside that component Git
history and have their own hash bindings.

The replacement `msync.c` SHA-256 is
`2f29d50b13855701d87791d9af3cc49984e184bbdee4b0b789111486deb4cb28`.
The unsigned composite ntdll hash is
`5cf4e51c3c31e1d6b4b08e9789801c87a49b7b8e43deffe345e0e52c66207a65`;
signing explains the distinct final hash. A root component gitlink alone cannot
describe this composite binary.

## Exact source deltas missing from the pin

These maps compare the audited component pin with the retained module sources.
They are source-publication scope, not instructions to replace an installed engine.

- **XInput:** modified `dlls/xinput1_3/main.c`; added `test_backend.h` and
  `tests/process-local/{README.md,build.py,fixture.c,native-expiry-fixture.c,run_native_expiry.py}`.
- **Native driver:** modified `dlls/winemac.drv/{ax_window_identity.inc,cocoa_event.m,d3dmetal.c,d3dmetal_objc.m}`;
  added `{drawable_phase_diag.h,visibility_guard.h,visibility_snapshot.inc,visibility_startup.inc}`
  in that directory. Other retained driver source files match the pin.
- **Network:** modified `dlls/ntdll/unix/{socket.c,unix_private.h,virtual.c}` and
  `dlls/ws2_32/{async.c,protocol.c,socket.c,unixlib.c,ws2_32_private.h}`;
  added `dlls/ws2_32/netdiag_pe.h`, `include/wine/{netdiag.h,netdiag_unix.h}` and
  `dlls/ws2_32/tests/netdiag/{README.md,logger-fixture.c,native-hook-fixture.c,native-readiness-fixture.c,netdiag-tool.c,run.py,run_native_hooks.py,run_native_readiness.py,windows-fixture.c}`.
- **Msync:** `dlls/ntdll/unix/msync.c` differs by the 19-line registration-send
  failure guard. Its retained snapshot path is `ntdll/wave7/source/unix/msync.c`.
  Keep the associated `test_msync_disconnect.py` fixture and README with a port.

The XInput/network component histories also contain the explicit AX Raise change.
Taking either entire branch as an undifferentiated feature patch would hide that
dependency and pull unrelated experiment content into the public baseline.

## Published experimental source

Two complete component histories are exposed on separate experimental branches
without copying another Wine tree:

- [XInput expiry/diagnostics](https://github.com/SlopSurfer4444/VibeWine/tree/experiment/process-local-xinput-expiry-diagnostics-20261018): `fbe0e3de434aa611b352aae661eacf87c4ee2f85`,
  13 changed files against the public pin, including AX Raise/tests and component
  manifests/checks.
- [Network readiness](https://github.com/SlopSurfer4444/VibeWine/tree/experiment/network-metadata-20261018): `0b4261775ea2016db16d7a77aa1e3586c808073f`, 31 changed
  files, including AX Raise and the precursor XInput backend. This is a separate
  source line, not a claim that it includes the later XInput expiry patch.

These branch tips remain separate from `component/wine` and each other. The
delta and intermediate changed blobs were checked for host paths and obvious
private assignments; the new commits use the public contributor noreply address.
Branch reachability is distinct from build, installation and runtime acceptance.

The [runtime source patches](../../patches/runtime-2026-10-23/README.md) publish
all eight native-driver deltas against the Wine pin and the exact MSync delta
against the network source revision. Their machine-readable manifest binds every
changed file and all 54 resulting driver-source files. Patch application and
source-byte equality were checked in disposable scratch; the patch export includes
license texts and excludes the private task Git histories, receipts and local
metadata. It supplies source deltas, not the donor objects, configured headers,
full portable build environment or binary-reproduction evidence. The composite
ntdll assembly and signing limits remain explicit in that manifest.

This exposes the actual experimental sources while retaining the component pin
as a distinct development baseline. It does not turn an old partial tree into a
new full engine. Then:

1. Separately port reviewable changes onto `component/wine`: AX Raise and msync
   as general candidates; XInput/network/phase instrumentation as opt-in
   experiments with their fixtures; DS3 visibility as explicit title-specific
   diagnostic policy. Update overlay manifests and component checks for each
   accepted port, then update main's gitlink with new integration evidence.
2. Claim a fresh integrated runtime only after a build from the selected new
   sources, paired-module/dependency checks and the intended runtime acceptance.
   Existing source hashes and historical fixtures do not supply those results.

An existing private preservation archive contains 2,497 members / 57,949,167
source-payload bytes; compressed SHA-256
`455ba7b046932ab298cf6f34ab55c8c583d777c48ee2e781a3046c602d287e2d`.
It preserves the actual source subtrees, configured-header snapshot and receipts.
It is a preservation artifact, **not a public download**, a full engine's complete
corresponding source, or a reproducible-build certification. Its recipes still
identify prior donor objects/toolchains; source-subtree preservation alone cannot
recreate their exact bytes.

Before publishing a derived source snapshot, retain Wine LGPL notices, original
authors and bundled-library licenses; include the applicable root license texts
with every separated source collection. Keep Sikarugir attribution on the cursor
import. Review donor/source completeness and dependency redistribution scope
before distributing a compiled engine. Sanitize host paths, account identifiers,
raw profiles, executable bindings, session tokens, raw network captures and
diagnostic addresses from public receipts. Record redactions as a derived export
with its own hashes; do not relabel changed receipts with their private originals'
hashes or overwrite the private evidence.

Public source and per-file attribution are the deliverable here. Installation,
activation, game behavior and performance retain separate evidence requirements.
