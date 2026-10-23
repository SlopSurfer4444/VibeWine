# Retained experimental runtime: exact native/MSync source patches

These patches publish the remaining native-driver and MSync source deltas for
the retained 2026-10-23 experimental module preparation. XInput and network source
are separate Wine experimental histories identified in
[Wine provenance](../../docs/provenance/wine.md). This directory contains source,
license texts and a source verification record; no installed binaries or raw
private receipts are included.

| Patch | Exact base | Result |
| --- | --- | --- |
| `winemac.patch` | [Wine `7469fcf53c4f537dfe305e961bffb0495d7b750d`](https://github.com/SlopSurfer4444/VibeWine/tree/7469fcf53c4f537dfe305e961bffb0495d7b750d) | Eight driver-file deltas reconstruct the exact 54-file retained `winemac.drv` source subtree from source checkpoint `4e6b235421bf3ded37b08ca12928e1998285c390`. |
| `msync.patch` | [Network source `0b4261775ea2016db16d7a77aa1e3586c808073f`](https://github.com/SlopSurfer4444/VibeWine/tree/0b4261775ea2016db16d7a77aa1e3586c808073f) | The exact retained `dlls/ntdll/unix/msync.c`, SHA-256 `2f29d50b13855701d87791d9af3cc49984e184bbdee4b0b789111486deb4cb28`, from fix `c5f9649ba8d4eeb35d7d13c716468c740fdd9b6f` / checkpoint `611fe80cdfb09377f94c505d09dad962c6bee2e3`. |

The native patch includes local explicit AX Raise behavior, DS3-specific opt-in
startup visibility instrumentation and bounded opt-in drawable/queue phase
diagnostics. It does not change the existing Sikarugir cursor-reset source already
in the base. The MSync patch adds the failed-registration cleanup/termination guard;
it does not establish why the Wine server exited or repair that original cause.

Apply each patch from the root of its **own exact base checkout**, using
`git apply --check` before `git apply`. These are separate reconstruction recipes:
the network history already includes AX Raise, so applying the complete native
patch to that history is not the documented procedure. Combining them into a new
component baseline requires a separate integration review and source manifest.

`provenance.json` records the patch hashes, every changed source hash, all 54
resulting driver-source hashes, installed module hashes and source identities.
The retained source checkpoints identify original evidence, not public task
history branches to merge. The private native/MSync Git histories are intentionally
excluded because they also hold local build metadata and receipts.

On 2026-10-23, both patches passed `git apply --check` and actual application in a
disposable source-only scratch tree. It contained only the 50 base driver files
and one network-base `msync.c`. The resulting exact driver path set and all 54
file hashes matched the retained driver source; `msync.c` matched its retained
source hash. The scratch tree was then removed. No compilation, linking, signing,
installation or game run was performed for this export.

The retained `winemac.so` was linked from two replacement Objective-C objects plus
prior donor objects, then ad-hoc signed. The retained `ntdll.so` combines network
build-v4 frozen objects/configured headers with one replacement MSync object,
a matched-g18 relink and g19 ad-hoc signing. These patches reconstruct the changed
source; they do not supply all donor objects, toolchains, generated headers or
dependency packaging needed for exact binary reproduction. The five-module
experimental runtime is not a full rebuild of either base or either source
checkpoint. No new binary, runtime or performance acceptance is claimed.

Wine-derived source retains LGPL-2.1-or-later terms and its per-file authorship
notices. `LICENSE` and `COPYING.LIB` are copied unchanged from the public Wine
base. Original authors remain listed in that base's
[AUTHORS](https://github.com/SlopSurfer4444/VibeWine/blob/7469fcf53c4f537dfe305e961bffb0495d7b750d/AUTHORS).
Publishing these deltas does not relicense Wine or certify the source/dependency
completeness of a future compiled-engine distribution.
