# Fork and upstream review, 2026-10-23

This is a source integration checkpoint. Selected fixes are ported onto the
identified VibeWine component bases; an unrelated Wine vendor history is never
merged wholesale. The installed DS3 runtime and its caches remain unchanged.
No FPS gain, network repair or cure of previous game freezes is established.

The subsequent [independent performance investigation](independent-performance-review-20261023.md)
records new source findings and bounded reproductions; its candidates are not
part of the accepted source imports or installed runtime.

## Initial selected changes

| Source | Change | Why it fits | Verification scope |
| --- | --- | --- | --- |
| [Wine `53a91cdc`](https://github.com/wine-mirror/wine/commit/53a91cdcbe21125913b24a48cf0f429597017d6a), also present in Sikarugir | Release copied pasteboard data after importing formats | Our `get_format_entries()` still retained every copied `CFData` reference. Adapted with a NULL guard on release; no claim to repair all existing import failure paths. | Actual extracted function with real CoreFoundation ownership and allocation-failure controls. |
| [Proton-GE `6b27fb1e`](https://github.com/GloriousEggroll/proton-ge-custom/commit/6b27fb1e663ffcd9ea6ad71ec256476320438031) | Skip a shared-SRW-release wake when no exclusive waiter exists | Ordinary portable PE synchronization code; avoids entering the wake path unnecessarily. Linux benchmark numbers from the donor are not Mac results. | Actual production body, no-waiter and real-waiter contract controls. |
| [Proton-GE `a1a8366f`](https://github.com/GloriousEggroll/proton-ge-custom/commit/a1a8366fd448417004d6a312448d8a9527d4eed9) | Preserve object/process lifetime across a handle-close callback | A kill-on-close job callback can tear down its own process's table. Recheck table ownership after that callback. | Reentrant teardown and unaffected-path source fixtures under sanitizers. |
| [Highball engine `84ff7c76`](https://github.com/gauthierpiarrette/highball-engine/commit/84ff7c76141bad87d5025c801fe92a0771e31e2a), also found in [ossover](https://github.com/hemkum/ossover/blob/5c11bd7be8da97c4dd03b05502be960faf75169e/engine/patches/0014-msync-unregister-early-return.patch) | Pair MSync early return with server unregistration and fix signaled-index-one cleanup | Both missing paths exist in our CrossOver-derived MSync. Client and server must be reviewed together. | Actual client/server bodies with deterministic transport/wake seams; not a real Mach-kernel or game test. |
| [DXMT `ce001dfb`](https://github.com/3Shain/dxmt/commit/ce001dfbedd7116aa9beb2e816c6ba44e599c15d) | Decode hull relative `OUTPUT_CONTROL_POINT_ID` and lower invocation plus offset | Existing parser/converter architecture already supports the underlying invocation ID. No D3D12 feature migration needed. | Actual parser/converter translation units and LLVM IR; baseline reaches missing-return UB. |
| [DXMT `a71b60ec`](https://github.com/3Shain/dxmt/commit/a71b60ecc506484b964cba9ebd3f9fb2b2537e04) | Acquire the existing device lock at four context entries | Uses the current recursive protection contract; deferred no-op mutex remains intact. | Full extracted entry/mutex bodies, deterministic contention and native/translated sanitizer checks. |

Original authorship and per-file licenses are retained. Wine-derived imports
remain LGPL-2.1-or-later. DXMT imports retain their component notices and original
author/cherry-pick provenance. Source records identify imported AI co-authorship
where present; VibeWine does not reattribute those patches to its own authors.

The DXMT compiler-output epoch advances from 1 to 2; its public AIRCONV API
version remains 26. This separates future output from earlier cache generations
without deleting or rewriting an owner's installed caches. The staging, dynamic
and ring lifetime fixes remain in the source.

## Deeper follow-up selection

The initially deferred candidates were checked against the actual component
sources, including baseline failures and unaffected-path controls. Five further
source changes are accepted; these are general mechanisms, with no new DS3 gate.

| Source | Accepted behavior | Evidence and remaining boundary |
| --- | --- | --- |
| Jason Eggleston via [Proton-GE `98efe1cd`](https://github.com/GloriousEggroll/proton-ge-custom/commit/98efe1cdefd7fc2f7ffb34044c194e046d739d6e) | When an address wait times out after its waker has dequeued it, consume that promised alert before returning. Decide ownership under the queue lock; preserve drain errors. | Six old-fail/new-pass races per backend, including actual Darwin `kqueue`/`kevent` alert retention/drain, normal-wake/error controls and sanitizers. Integrated Wine and independent raw-alert races remain outside the fixture. |
| kageroumado / Claude Fable 5.1, [Dormison `3e77a112`](https://github.com/kageroumado/dormison/commit/3e77a1125ef897378f3f869ff5b2eaf0c44a58aa) | Inactive topmost windows drop below the Dock; active promotion is preserved. | Full production methods with native AppKit panels and CoreGraphics order/level observations: 35 candidate assertions; baseline fails exactly eight. OS lifecycle state is injected; this does not prove real Dock-click or Mission Control behavior. |
| Gabe Zimbric, [Metal-view lifetime `0dad72df`](https://github.com/gzimbric/wine-arm64-macos/commit/0dad72df204142a2f276495af5baaf4d932ea91f) | Retire delegate/producer lease synchronously and defer hierarchy teardown through earlier main-GCD users. Preserve our exclusive lease and reject acquisition after terminal disposal. | Real native/translated AppKit/QuartzCore ownership checks and sanitizers; exactly three baseline hierarchy failures, plus terminal-guard mutation. Protection covers preceding FIFO blocks, not arbitrary later/private Metal-HUD work. |
| Feifan He / CodeWeavers, [DXMT `39625fa9`](https://github.com/3Shain/dxmt/commit/39625fa98c9830ffb035d51a37e3590e2b8d4d9e) | RAII for error objects and partial shaders; optional error output is safe. Narrow ownership subset, without newer IOHelper/VertexPuller refactors. | Actual complete converter TU, 22 CPU API cases under UBSan and repeated-allocation checks; exact baseline leak/null-output failures. Specialized pipeline success, GPU behavior and a fresh complete bundle remain untested. |
| [Gcenx portability recipe at `850f5330`](https://github.com/Gcenx/macports-wine/blob/850f5330ed500a87bdcf8441763ad6cf4109aedc/emulators/wine-stable/files/0001-win32u-Enable-host-Vulkan-portability-enumeration.diff) | Enable host portability enumeration extension and flag when supported. Original author not specified in the recipe; existing Wine notices retained. | Actual conversion function/real Wine headers, eight sanitizer cases, six exact baseline failures. Host-contract model only; real MoltenVK instance/device and Vulkan-enabled Wine remain untested. No effect established on the current DXMT / `--without-vulkan` DS3 route. |

The timeout donor reports original `jegglest/wine@dee180fa836108dcb61c520708721b8bed85ca42`;
that original was not independently retrievable. The checked GE patch is the
auditable source. Metal lifetime is an adaptation, not the donor's shared-client
ownership design. Existing exclusive generation identity and allocation fixes
remain intact. SM50 ownership does not alter successful shader lowering, so API
26 and compiler epoch 2 remain unchanged in this follow-up.

## Other sources inspected

| Project | Finding / decision |
| --- | --- |
| [Whisky](https://github.com/Whisky-App/Whisky) and surviving WhiskyWine-derived sources | Inspect engine sources separately from the Swift launcher. Original WhiskyWine source URL is unavailable; source-bearing alternatives were inspected rather than extracting installed binaries. |
| [CrossOver public source](https://www.codeweavers.com/crossover/source) | Our import already describes CrossOver 26.3 / Wine 11.0. Today's published archive URL does not prove the origin or byte identity of the historical local archive. Preserve that unknown ancestry. |
| [Sikarugir Wine](https://github.com/Sikarugir-App/wine), [WineAndAqua](https://github.com/WineAndAqua/wine) | Cursor-reset fix already imported. Style-mask initialization and several Retina changes already present. Newer swapchain autorelease wrappers do not match our driver ABI. |
| [Wine Staging](https://github.com/wine-staging/wine-staging) / [Valve Wine](https://github.com/ValveSoftware/wine) | Version-coupled patch queues and Linux synchronization/backend work require separate dependency review. Do not apply every patch or transplant Linux kernel synchronization into macOS. |
| [Gcenx macports-wine](https://github.com/Gcenx/macports-wine) | Portability enumeration accepted in the follow-up for optional Vulkan builds, with the current DXMT runtime boundary kept explicit. |
| [Dormison `3e77a112`](https://github.com/kageroumado/dormison/commit/3e77a1125ef897378f3f869ff5b2eaf0c44a58aa) | Native topmost ordering accepted after the follow-up reproduction. Private SkyLight cursor patches still have broader dependencies and remain excluded. |
| [gzimbric Metal-view lifetime](https://github.com/gzimbric/wine-arm64-macos/commit/0dad72df204142a2f276495af5baaf4d932ea91f) | Adapted FIFO hierarchy lifetime accepted after native reproduction; shared-client ownership was not copied. |
| [Cross-process IOSurface prototype](https://github.com/Coderx64/wine-macos-crossprocess-iosurface) | A larger alternate presentation subsystem, not a narrow repair; extra copies, rendezvous and stale-frame behavior need their own architecture review. Not imported. |
| Proton-GE WaitOnAddress timeout change | Accepted in the follow-up after reproducing the current wake-buffer race and native Darwin backend behavior. |

This is a bounded review, not an inventory of every Wine fork. Source inspection
supports selection/rejection; it does not establish the behavior of every donor
build or launcher.

## Build and runtime boundary

The initial selection's component pins were Wine
`d2237a9a82d9960360ce8400242ddeae6646c347` (four attributed backports) and
DXMT `284f84ccbc3ec61fb5ea25744a951d3f1eabdeaa` (two upstream fixes plus
regressions and cache compatibility). Their predecessors remain accessible in
Git; the public component branches advance without rewriting existing history.
The follow-up pins are Wine `bee453b09460e0c3eb49b7c491a6d04e65cc38a5` and DXMT
`fab325e0adcd639d822dbfac87a1b840ccb2d4bf`.

Local acceptance checked all 11,112 Wine source hashes, specific old-fail/new-pass
fixtures for all four Wine changes, native production syntax for four complete
translation units and PE syntax for the synchronization unit. The MSync fixture
passes sixteen assertions; the same baseline fails exactly six expected checks.
The lock fixture detects all four missing baseline locks. The compiler fixture
compiles the actual parser/converter, verifies invocation-plus-offset LLVM IR and
rejects the baseline's specific missing-return behavior. Existing compiler-cache
regressions also pass. The integration owner independently reran these focused
Wine, compiler and native lock checks; translated lock checks and both complete
D3D11 context syntax checks were performed by the component integrator.

The follow-up verifies all 11,124 Wine source files against the unchanged vendor
manifest and ordered overlay hashes. The integration owner independently reran
every new focused candidate and known baseline check. A separate source review
found no blocker in synchronization,
native lifetime or SM50 ownership. Complete modified Wine production units pass
native/PE syntax checks using the retained configured headers; the actual changed
SM50 converter TU is compiled by its fixture. This is not a freshly configured,
linked full Wine/renderer build.

CI runs Wine source verification, the portable Wine regressions, native DXMT
lock checks and the existing compiler/source checks. The actual LLVM converter
fixture requires the separately provisioned LLVM14/support build and remains a
documented local check. Declared workflow steps are not CI pass results by
themselves.
The follow-up adds both current/baseline address-wait (including Darwin backend)
and Vulkan-contract checks. Nonactivating native AppKit checks remain local
WindowServer-dependent checks; no headless success is inferred when unavailable.

The component notes contain exact bases, imported revisions and focused checks.
The previous matched graphics bundle remains the installed checkpoint described
in [status](../STATUS.md). New source backports need a newly matched compiler,
PE libraries, native bridge and Metal helper before runtime promotion. Source
fixtures and syntax checks do not substitute for that build, GPU acceptance or
a controlled gameplay comparison.

No new capture producer, cache cleaner, game profile, background service or
installed tool is introduced by this source review. Test-generated files belong
to temporary scratch and are removed by their runners; source/provenance records
are retained in Git.
