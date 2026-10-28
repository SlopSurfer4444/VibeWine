# MSync shared-object reuse

Run from the Wine component root on Darwin:

```sh
python3 vibewine/tests/msync-freehint/run.py --arch arm64 --sanitize
python3 vibewine/tests/msync-freehint/run.py --arch x86_64 --sanitize
python3 vibewine/tests/msync-freehint/run.py --revision bb2c8919 --expect-regression --arch arm64 --sanitize
python3 vibewine/tests/msync-freehint/run.py --revision bb2c8919 --expect-regression --arch x86_64 --sanitize
```

The runner compiles the actual `destroy_all_internal` and `msync_alloc_shm`
bodies, their shared layout and allocation-hint declarations. Only the shared
storage accessor and protocol index bound are fixture seams. The bound is 15
so a few legal operations expose false exhaustion without allocating the real
28-bit domain. Live payloads/refcounts must retain identity.

Legal frees of 2 then 4 must allocate 2 then 4. Two thousand rounds with four
live objects must stay within their original high watermark. Controlled seams
place destruction below an already-scanned frontier, publish a lower free at
the maximum-index boundary, and simulate a delayed stale hint after reuse. A
truly full domain must still terminate. These are deterministic interleavings
in one thread, not a kernel scheduling or data-race-detector claim. ARM64 and
Rosetta each require seven passing assertions with ASan/UBSan. Old source must
fail exactly three named checks; unrelated crashes/sanitizer errors fail the run.

The pump is the free-hint producer; the normal server thread is the sole
allocator. A CAS publishes the minimum freed index. An allocation exchanges
that hint with the sentinel before scanning. Frees published during its scan
remain pending for the next allocation. After selecting G, all free indices
above G remain reachable from G+1, while lower concurrent frees are represented
by the new hint. Before false exhaustion, another exchange admits a lower free
published during the scan. A delayed hint for an already-reused slot only causes
an occupied-slot scan. Shared-entry size, refcount ownership, wait-message layout
and the reserved initial indices remain unchanged.

The runner owns temporary source/executable files and removes them on exit;
compile/process watchdogs are 60/10 seconds. No Wine process, installed runtime,
service or game is launched. Saved branch logs expire after 7 days/32 MiB and
source/results after 30 days/8 MiB. Active review and explicit holds exclude
retirement; manual exact-owner cleanup only, with no persistent producer/cleaner.
