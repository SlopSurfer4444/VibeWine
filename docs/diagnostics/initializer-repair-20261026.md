# Initializer transaction repair — 26 October 2026

The [preceding deep round](deep-hotpaths-20261026.md) reproduced resource
initializer defects at DXMT `66bae06c`; this checkpoint repairs their common
reserve/fill/publish mechanism and the resource-creation callers. This is general
DXMT behavior, with no DS3 name, game policy or additional experiment switch.
Installed games and runtime selection are preserved.

Source milestones: transaction/factory/ring/Mach cleanup `b49b495f`, consumer
queue preflight `55150d0a`, strict GPU report gate `99e1a1c3`. The hub pins the
last commit; its production bytes equal the built `55150d0a`. Wine remains
`5d4b43e4`. The [machine-readable receipt](initializer-repair-20261026.json)
retains full identities, artifact hashes and qualified results.

## What changed

- Initializer operations check CPU command capacity before reservation, including
  tracker spill space. They flush only an already-valid prefix. The natural
  capacity fixture now publishes and executes all 18,454 requested commands.
- The checked ring API reserves one or two staging spans atomically. Failure
  preserves offsets, pending work and ownership; depth/stencil cannot publish
  only one plane. At most one additional attempt is admitted after valid-prefix
  submission or newer completion. Existing allocator clients retain their API.
- Zero-buffer growth retains the previous cache until submission succeeds and
  publishes its replacement fill/copy together. Source-pitch, footprint,
  alignment and sequence-overflow checks reject invalid work before publication.
- Every initializer result carries a checked status. Buffer and texture factories
  leave output pointers null on failure, return `E_OUTOFMEMORY` or `E_FAIL`, and
  unwind local allocations. Shared registration occurs only after successful
  initialization, with explicit ownership of acquired KMT handles and fresh
  unpublished Mach send rights.
- Native command-buffer/encoder creation failure preserves the pending prefix
  and sequence. Explicit flush throws before publishing a consumer wait. Consumer
  queue bookkeeping now also starts after this fallible preflight, so retry
  cannot consume an event or mutate an unpublished chunk.
- The matching PE/native bridge appends `WMTMachPortDeallocate` at ordinal 150;
  all earlier slots 0–149, including null slot 83, remain unchanged. Both dispatch
  tables have 151 entries. These artifacts must be delivered as one matching
  group; this source checkpoint does not replace installed files.

The implementation reuses local tracker, ring, arena and native retained-resource
owners. The checked donors `3Shain/dxmt` at `a148782d` and `elseform/dxmt` at
`810c4dd` retained the reproduced defects; their hidden in-allocation flush was not imported.
No external implementation, new transaction framework or persistent service was
added. File/component attribution and licenses remain intact.

## Evidence

Root reran 31 actual-initializer/ring/tracker/Metal-wrapper ASan/UBSan cases,
checked ring and existing retention checks, 119 extracted production factory
cases and five rejected mutations. Queue failure/retry/idle propagation passes
all four async/sync × debug/release configurations and rejects old-order controls
in each. Native private Mach right cleanup, exact dispatch/ABI checks, 11 actual
texture-allocation failure cases and ownership mutants pass. These use the
seams stated in the component fixtures: mocked native/COM/KMT endpoints are not
actual native failure injection.

Compatibility checks pass for the native capture bridge, checked SQLite cache
and texture-view prefix fixture. The historical reproducer remains separate:
`initializer-failure-proof --expect-known-defects --source-ref 66bae06c` still
reproduces its 16 original observations from immutable Git source. It is excluded
from ordinary success CI; new regression tests require repaired behavior.
There are also 13 strict captured-report parser tests, 221 offline telemetry
tests and 13 workspace tests. The preceding deep-round hosted run 40246188105
passed; this new publication has its own hosted result.

Both fresh complete 161-target DXMT builds passed, with eighteen experiment
booleans all off/on and matching actual PE/native definitions. Both were then
incrementally rebuilt for the sole later production change in `dxmt_command_queue.cpp`.
Seven final artifact hashes per variant include the compiler helper and Metal
library. Retained Wine, LLVM and dependency scaffolding are reused; this is not
a newly built complete Wine distribution.

The isolated native GPU lab passed **24 mandatory positive stages per variant**:
17 existing draw/compute/MSAA/query/resolve/MRT/depth/staging/view stages plus
seven initializer stages. New checks include nonzero padded 2D array/mip data,
zero-buffer growth, render/depth/stencil zero state, D32S8 and emulated D24S8 plane
uploads, and padded 3D slice data. Seven intentionally wrong pixel/query goldens
were rejected at their intended checks. All 17 processes were admitted with a
stable process birth and seven required PE/native mappings, bounded capture and
private-server cleanup. The exact lab plan/files were restored with no private
IPC sockets. No DS3, installed profile, VPN or save data was touched.

## Acceptance boundary

This repairs CPU transaction and error-propagation correctness with covered
real GPU success/readback checks. It does **not** establish a displayed FPS gain,
repair all long stalls, or prove installed DS3 behavior. Zero readbacks alone
cannot prove that an internal zero command executed; the CPU command-count tests
supplement that evidence.

Actual GPU OOM, asynchronous command-buffer/device failure and device-lost
liveness remain open. Shared KMT/bootstrap creation was not exercised on GPU;
the previous successful KeyedMutex/bootstrap lifetime is not redesigned here.
Padded array/mip/3D data coverage does not imply every compressed, alias,
attachment, GS/TS or presentation path is covered. Any later installation needs
the complete matching artifact group and its own runtime acceptance.

Component details and runnable controls:
[transaction](../../components/dxmt/docs/initializer-transaction-repair-20261026.md),
[factory ownership](../../components/dxmt/docs/initializer-creation-failure-20261026.md),
[GPU guest and parser](../../components/dxmt/tests/vibewine/initializer-transaction/gpu/README.md),
[consumer queue](../../components/dxmt/tests/vibewine/initializer-queue/README.md).

## Retention and ownership

Root owns integration, canonical Git index, GPU lab and this repair's exact
private `initializer-repair-20261026` output scope plus the 17 named
`gpu-lab/runs/initializer-repair-*` receipts. Data classes are generated test logs,
procedural stdout/readbacks, process/mapping receipts and build/source evidence;
no game/account/user-content collection was added. Raw receipts share a seven-day
TTL and combined 64 MiB cap (612,554 bytes at closure); curated results use
30 days/4 MiB. Source checkouts and selected build/recovery evidence are separately
identified holds under the existing 40 GB build cap and 20 GiB free-space reserve.

Temporary fixture scratch is removed on exit. There is no surviving collector or
new automatic cleaner. Manual retirement rechecks exact ownership, references,
symlinks and active/unresolved/candidate/recovery holds before deletion. Versioned
minimal reproducers and commit history are source, not expendable raw logs.
Hosted Actions storage follows GitHub's existing repository retention; local
caps are not enforced there. Owners can inspect/delete an exact completed run
through Actions or the CLI after preserving required evidence and holds.
