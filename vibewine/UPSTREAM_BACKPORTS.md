# Selected Wine-family backports, 2026-10-23

Base: `7469fcf53c4f537dfe305e961bffb0495d7b750d`, the complete VibeWine
vendor-derived component. The exact WineHQ Git ancestor of that import remains
unknown. These are reviewed source backports, not a merge of unrelated histories
or an update of the installed Wine engine.

| Source / original authorship | Production change | Adaptation |
| --- | --- | --- |
| Brendan Shanks, [Wine `53a91cdc`](https://github.com/wine-mirror/wine/commit/53a91cdcbe21125913b24a48cf0f429597017d6a); also present in Sikarugir | `dlls/winemac.drv/clipboard.c`: release copied pasteboard data | Guard `CFRelease(data)` for NULL. The existing import callbacks' unrelated NULL/empty-data behavior is not repaired by this release. |
| Sebastian Matkovich, [Proton-GE `6b27fb1e`](https://github.com/GloriousEggroll/proton-ge-custom/commit/6b27fb1e663ffcd9ea6ad71ec256476320438031), carrying patch `f1a25ff823ae58d12aa44dbd37205181e8e2f6a2`; original patch credits Claude Opus 5.5 | `dlls/ntdll/sync.c`: do not wake an exclusive SRW waiter when none exists | Exact production hunk. No claim to reproduce the donor's Linux game CPU percentages on macOS. |
| giantorth, [Proton-GE `a1a8366f`](https://github.com/GloriousEggroll/proton-ge-custom/commit/a1a8366fd448417004d6a312448d8a9527d4eed9), carrying patch `4fcef5462e55bb3f0005e9762fb2e9592c85b095` | `server/handle.c`: hold process/object references over a close callback, then check whether its handle table survives | Exact production hunk. Covers reentrant process teardown, including kill-on-close jobs. |
| Gauthier Piarrette, [Highball `84ff7c76`](https://github.com/gauthierpiarrette/highball-engine/commit/84ff7c76141bad87d5025c801fe92a0771e31e2a), `patches/0014-msync-unregister-early-return.patch`; also found in ossover | `dlls/ntdll/unix/msync.c` and `server/msync.c`: unregister an early-returning multi-wait; clean preceding registration at signaled index one; emit exhausted-pool warning once | Exact production hunks applied together. Original patch SHA-256 `09c46f062f7e936bb331274192ce3436dbd18242a1b1618ca4944b6193158715`. |

Original Wine/MSync source notices remain intact, including Zebediah Figura and
Marc-Aurel Zent. These changes and the added Wine-derived fixtures retain
LGPL-2.1-or-later terms; see root `LICENSE` and `COPYING.LIB`. This record does
not reattribute imported patches or the vendor base to VibeWine.

## Focused verification

From the component root on macOS:

```sh
python3 vibewine/tests/backports.py
python3 vibewine/tests/msync_registration.py --source-dir .
python3 vibewine/verify_source.py
```

The fixtures in `vibewine/tests/` extract the actual production functions, rather
than maintaining a separate implementation. Their known baseline must fail for
the expected regression; a build error or unrelated crash is not a pass.

- Clipboard: actual CoreFoundation objects verify retained-data ownership on
  successful import and allocation failure. The importer seam isolates release
  behavior; it does not validate every real clipboard converter.
- SRW: count unnecessary wakes on uncontended final shared releases and use a
  real pthread waiter to verify that exclusive acquisition still wakes correctly.
  Wine address queues/PE ABI and real workload contention remain separate checks.
- Handle close: an instrumented callback tears down its process and table while
  the production function is running; sanitizer checks and unaffected rejection/
  normal paths check ownership. This is a server-function fixture, not a whole
  Wine job-object integration run.
- MSync: sixteen assertions exercise actual client/server bodies, including a
  realistic queued signal followed by a competing consumer, 1,000 repeated lost
  races, alert references, timeouts and unrelated waiters. The baseline fails
  exactly six expected checks. Mach transport, pool allocation, wake delivery and
  ulock results are deterministic seams. The warning-once hunk is not exercised;
  real kernel scheduling is outside the test. See
  [MSync commands and limits](tests/MSYNC_REGISTRATION.md).

The five changed complete production translation units also passed syntax-only
compilation: four native x86_64 files and the x86_64 PE synchronization file.
Those checks reused the baseline's retained configured headers and installed
compilers. They do not establish a fresh configure, linked Wine build, paired
runtime assembly, installation, gameplay acceptance or FPS improvement.

`python3 vibewine/verify_source.py` verifies the original vendor manifest and the
ordered overlay hashes, including these production deltas and their source
fixtures/documentation. The original archive identity is preserved. No compiled
binaries, configured headers, personal paths, raw traces or private receipts are
published. Temporary fixture outputs are removed by their runners.

## Follow-up: formerly deferred candidates

Follow-up base: `d2237a9a82d9960360ce8400242ddeae6646c347`. This extends the
source selection above without changing installed runtime selection.

| Source / original authorship | Change and adaptation | Evidence / limit |
| --- | --- | --- |
| Jason Eggleston, [Proton-GE `98efe1cd`](https://github.com/GloriousEggroll/proton-ge-custom/commit/98efe1cdefd7fc2f7ffb34044c194e046d739d6e) | Consume the promised in-flight address alert when timeout races a waker that already dequeued the waiter. Decide queue ownership under the lock on every timeout; preserve backend drain errors. | Six deterministic races per backend, real Darwin `kqueue` event retention/drain, unchanged-path controls, ASan/UBSan and exact baseline failures. Alert allocation/table lifecycle, integrated Wine and independent raw-alert races are outside this check. |
| kageroumado, [Dormison `3e77a112`](https://github.com/kageroumado/dormison/commit/3e77a1125ef897378f3f869ff5b2eaf0c44a58aa); original co-author Claude Fable 5.1 `<noreply@anthropic.com>` | Lower inactive topmost-window floor below the Dock; preserve active-window promotion. | Actual complete level/activation methods with native AppKit panels and CoreGraphics level/order checks: 35 assertions, exactly eight expected baseline failures. Lifecycle state is injected; no real focus/Spaces/fullscreen transition or Dock-click acceptance. |
| Gabe Zimbric, [Metal-view lifetime `0dad72df`](https://github.com/gzimbric/wine-arm64-macos/commit/0dad72df204142a2f276495af5baaf4d932ea91f) | Adapt deferred hierarchy teardown while preserving our exclusive producer lease: retire the lease and layer delegate synchronously, retain native hierarchy through preceding main-GCD work, reject acquisition after terminal disposal. | Actual production methods with real AppKit/QuartzCore objects, native and translated sanitizer checks, three exact baseline hierarchy failures and a terminal-guard mutation. Covers earlier FIFO work, not arbitrary later/private Metal-HUD callbacks. |
| [Gcenx/macports-wine recipe at `850f5330`](https://github.com/Gcenx/macports-wine/blob/850f5330ed500a87bdcf8441763ad6cf4109aedc/emulators/wine-stable/files/0001-win32u-Enable-host-Vulkan-portability-enumeration.diff); original author not specified | When the host supports portability enumeration, request both extension and flag while preserving existing flags. | Actual conversion function, real Wine Vulkan headers and eight host-contract cases under sanitizers; baseline fails exactly six checks. Optional Vulkan/MoltenVK route only: current tested Wine uses `--without-vulkan` and DXMT. No live loader/device acceptance or current DS3 benefit. |

The GE patch header reports `jegglest/wine@dee180fa836108dcb61c520708721b8bed85ca42`
as its original source; that original commit was not independently retrievable.
The source recipe and these adaptations remain individually attributed; no shared
Metal-client lease or private SkyLight subsystem was imported.

```sh
python3 vibewine/tests/address-wait/run.py --sanitize
python3 vibewine/tests/address-wait/run.py --revision d2237a9a82d9960360ce8400242ddeae6646c347 --expect-regression --sanitize
python3 vibewine/tests/vulkan-portability/run.py
python3 vibewine/tests/vulkan-portability/run.py --revision d2237a9a82d9960360ce8400242ddeae6646c347 --expect-regression
# Logged-in macOS WindowServer required; reviewed fixtures create only their own
# hidden/nonactivating native objects, without changing user focus or Spaces:
python3 vibewine/tests/native-window-levels/check.py
python3 vibewine/tests/metal-view-lifetime/run.py
python3 vibewine/tests/metal-view-lifetime/run.py --baseline
python3 vibewine/tests/metal-view-lifetime/run.py --mutation missing-terminal-guard
```

Detailed extraction boundaries, timeout/error controls and remaining limitations
live beside each fixture. The complete modified synchronization and native-driver
translation units also pass syntax checks against retained configured headers.
These are not fresh full builds, installation, real game behavior or FPS results.
The two newer swapchain autorelease wrappers remain excluded: their donor ABI
differs and matching caller/main-block pools already cover our legacy wrappers.
