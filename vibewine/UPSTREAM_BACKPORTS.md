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
