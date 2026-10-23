# Integrated renderer wave, 23 October 2026

This candidate builds on `fab325e0adcd639d822dbfac87a1b840ccb2d4bf`.
The changes below are general renderer changes; no DS3 image-name gate is added.
They are a development checkpoint, not an installed-game or FPS receipt.

## Source changes

- Visibility query metadata/result publication shares a per-query mutex;
  timestamp completion uses release/acquire. Queries ending in a later chunk
  with no new visibility offsets now retain their completion readback.
- Windows D3D11 final device destruction runs on a precreated private worker.
  It drains the context and joins queue workers while pipeline/cache owners
  remain alive. Ordinary COM classes keep synchronous destruction. This does
  not change the legacy non-Windows device teardown path.
- Successful shader/pipeline readiness precedes optional cache persistence.
  The separate writer owns its keys/data, has active-plus-queued admission
  limits, drops optional overflow and joins active work at shutdown. The
  existing cache keys, epochs and user entries remain intact.
- Additive checked cache calls expose actual SQLite outcomes rather than
  treating callable completion as persistence success. Failed initialization
  releases its resources and static bindings are cleared before caller storage
  dies. Both PE/native dispatch tables carry the matching appended calls.
- Descriptor getters are hoisted, and existing render/compute/present command
  setup is batched through the same synchronous command-list ABI. This reduces
  bridge calls without a new transport or relaxed resource lifetime.
- Fullscreen capture generations come from one nonwrapping DXGI allocator,
  shared by separately linked WSI copies. Reentrant stale cleanup cannot restore
  a newer capture. HDR metadata is validated, copied and converted explicitly
  instead of silently returning success without applying it.
- Default-off bounded detailed events distinguish runnable/dependency delays,
  optional persistence admission, native SQLite results and readback/coherence
  publication. Their timestamps remain CPU steady-clock observations.
- Metal source modules use a build-local compiler module cache, avoiding an
  undeclared write to the operator's global Clang cache in restricted builds.

`DXMT_SCHEDULER_BACKLOG_GROWTH=1` separately opts into backlog-aware worker
growth. Its numerical cap and existing priority policy are retained. Spatial
upscaling, temporal reconstruction, ARM execution and new transport protocols
are not enabled by this checkpoint.

## Matched build requirement

Ship D3D11, DXGI, D3D10Core, WineMetal PE/native and the corresponding compiler
together. Checked cache calls append IDs 146–148; existing IDs/layouts remain.
The new internal `DXMTAllocateFullscreenToken` import requires the matching DXGI.
AIRCONV API 26 and VibeWine compiler-cache epoch 2 remain unchanged by this wave.
Never replace just the new D3D11 DLL in an older bridge installation.

The integration build also binds Wine client/server source
`ad5b79cfb1641634bac15d723cf5f9023f73d328`. Existing loader/dependency binaries
were retained explicitly; this is not a claim that every vendor binary was
rebuilt. A paired client/server update needs the supported complete installation
transaction, not the older ten-file renderer-only map.

## Verification checkpoint

The integration owner reran 132 telemetry tests, 50 gameplay JavaScript tests,
30 controller tests and 13 actual child-executor tests. Source-level renderer
checks cover query publication, concurrent polling, empty completion, compiler
persistence ownership/caps/failures, real native SQLite contention/corruption,
encoder marshalling, fullscreen reentrancy and HDR validation. Device ownership,
retirement and concurrent queue shutdown passed ASan/UBSan and TSAN fixtures;
restoring the old direct-delete hook fails its specific negative control.

Fresh affected Wine modules link for both guest architectures, and the complete
DXMT PE/native/compiler build links against that Wine build. These builds retain
existing upstream warnings; they are not warning-free-build claims.

A separate authless disposable prefix, private wineserver namespace and
network-denying sandbox executed the procedural offscreen GPU guest. All five
stages passed: basic draw, MRT/depth across Flush, compute UAV, four-sample MSAA
resolve, and a visibility query ended in a later empty chunk. There were 146
positive checks. Three separate negative-control processes rejected their
intentionally changed pixel, compute and query goldens with the expected natural
exit status. Independent process mapping plus hashes bound the new D3D11, DXGI,
WineMetal PE/native, NTDLL, WineMac and Win32U. The private shader database had
five entries in the epoch-2 table and passed SQLite integrity checking after
all its producers exited. No user cache was opened or modified by these tests.

CPU fixtures replace selected platform/GPU seams and are described in their
own READMEs. The offscreen guest has no swapchain: it cannot verify presentation,
HDR display output, fullscreen/resize/input behavior, gameplay, displayed FPS or
long-session stability. Real-game lifecycle and controlled performance comparisons
remain integration gates. A successful source build or offscreen result must not
be reported as an installed DS3 fix or a measured frame-rate increase.

Source-level changes reuse existing DXMT/Wine/SQLite mechanisms and preserve
their original notices. Local changes and checked donor origins are tracked in
the component history and the hub's provenance/integration documents.
