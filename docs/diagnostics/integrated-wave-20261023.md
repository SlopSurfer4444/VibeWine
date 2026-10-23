# Integrated matched candidate, 2026-10-23

This is a source/build/native-fixture checkpoint. It is not installation, game
acceptance or a measured FPS improvement. No game, account or user prefix was
used in these checks.

## Source and build closure

- Wine `e20af58312ef059e54f8d16767ee7b74cb8358e5`: selected source overlays,
  bounded paired MSync ACK parking, included-file dependency fix, and terminal
  SIGQUIT masking before both thread-count decrements.
- DXMT `828429ca6b142230399d98272207af7714f12895`: query publication/retirement,
  readiness before optional bounded persistence, backlog-aware compiler work,
  command batching, HDR/configuration generations and detailed probes.
- Fresh native x86-64 and i386/x86-64 PE Wine modules, matching wineserver,
  D3D11/DXGI/D3D10Core/WineMetal PE, native WineMetal and static AIRCONV. The
  unmodified loader and remaining engine modules/dependencies are retained.
- Apple SDK 27 / clang 21 for native Wine, pinned llvm-mingw toolchain for PE,
  LLVM 15/Meson/Metal toolchain for DXMT. Build-local Metal module cache.
- FreeType 2.14.3 official headers match the existing 2.14.3 runtime library.
  No external FreeType build script was executed. The selected rebuild preserves
  the installed win32u and server relative Frameworks rpaths before ad-hoc signing.

Exact build recipes, unsigned-to-signed hashes and private immutable test
receipts are retained by the experiment owner; private prefixes, account paths
and gameplay traces are not public source assets. The private build data
contract limits raw logs/deadlines and reserves working disk space; unresolved
acceptance/recovery material remains held.

## Native checks actually performed

A separate disposable authless prefix ran under a filesystem/network sandbox.
Its broad DOS mapping was restricted to the lab; no game prefix was copied.
The parent checked process birth, exact selected hashes and actual loaded PE
and native module mappings, then stopped only this private prefix server.

| Check | Observation | Boundary |
| --- | --- | --- |
| Font | TrueType enumeration, positive metrics/extent, DIB text and 351 nonwhite pixels; blank control produced zero pixels and natural exit 1 | Native win32u/FreeType mappings observed; not all text/application rendering |
| Terminal exit | 32 PE runs: plain, 16-worker concurrent, joined workers and private threadpool; all intended status 20 reached Unix | Baseline concurrent run returned zero; controlled SIGQUIT fixture establishes the repaired race, not every prior game hang |
| GPU normal | Basic draw, MRT/depth across Flush, compute UAV, four-sample MSAA resolve, query completion in a later chunk passed | Offscreen correctness only; no displayed frames or FPS |
| GPU controls | Corrupted pixel, compute and query controls each rejected with natural exit 20 | No accepting a printed error with host exit zero |
| Module identity | All seven required PE/native graphics and Wine mappings present for every GPU case; hashes unchanged | Does not prove DS3 loads the same files |

The actual-production-body shutdown fixture passed 224 native and 112 ASAN/UBSAN
cases. The predecessor reproduced 40 injected count-corruption exits zero plus
16 valid controls. The integration owner independently reran 28 candidate cases.
Global RPC masking and normal last-thread exit-status semantics remain unchanged.

## Corrected performance reference

The build-only `vibewine_perf_reference` option defaults to `none`. A separate
`w4` build restores repeated attachment getters and individual render/compute/
present calls while keeping the same query/lifetime/persistence/WSI fixes, ABI
and epoch-2 cache keys. `getters` and `batching` isolate each part. No runtime
configuration or profile enables this control. Source fixtures compare identical
semantic command records in both modes; this is an attributable reference, not
a request to restore known correctness failures. Both full builds completed;
real-game comparisons remain pending.

## Remaining acceptance

Publish/install only through the affected matched-runtime transaction, preserving
existing recovery, saves, physical-controller prefix DLL and unrelated software.
Verify ordinary icon launch and true current mappings, offline world and safe
action, short bounded presented-frame measurements, mode/size transitions, normal
exit and a fresh target with Steam left open. Keep source correctness, observed
game behavior and controlled FPS attribution separate.
