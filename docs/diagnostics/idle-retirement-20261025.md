# Opt-in completed-idle pool retirement

P4 extends the existing Dynamic/staging retention owners. With
`DXMT_IDLE_RETIREMENT=1`, each D3D11 command queue owns a separate
`IdleRetirementDomain`. Exact creation callsites attach DynamicBuffer and staging
resources to that queue; there is no process-global completion frontier. The
DynamicLinearTexture owner receives the same domain at its linear-texture creation site.
The normal build remains default-off.

After GPU completion, query readback/command closures are destroyed and the queue
publishes CPU coherence. Its existing finish thread then performs a bounded
maintenance pass. No resource Map/allocate, new worker, GPU wait, global cache
purge or save-data change is required. Completed FIFO pool references at least
300 queue sequence IDs old can be dropped while retaining the current name and
one warm spare. Sequence age is neither wall time nor frame count; additional
scene submissions affect it. No work occurs after the queue stops completing
chunks, and pools whose retirement ages do not reach this window remain held.

The registry borrows resource callbacks, never a strong resource reference.
Registration retains the domain, so resources may outlive their queue without a
keepalive cycle. Explicit owner destructors unregister before destroying the
FIFO/mutex. The registry mutex pins callbacks during unregister; callback resource
locks use try-lock. The only nested lock order is registry then resource; ordinary
Map/allocation methods do not acquire the registry lock. Busy resources and busy
registry passes are skipped. Current allocations, incomplete FIFO entries,
command-list-owned Dynamic allocations, staging stable numeric IDs reserved before
publication, and independent external Rc references are preserved. Releasing a
pool Rc never claims to destroy another owner's reference.

Each pass visits at most 16 resources, examines at most 128 FIFO entries and drops
at most 64 references charged at no more than 64 MiB. Native size queries are
bounded by those entries plus at most two protected names per visited owner.
FIFO ordering remains conservative: a young/incomplete/uncharged/oversized head
can retain later entries. A zero allocatedSize is explicitly unknown and cannot
be released against a guessed byte budget. Individual allocations above 64 MiB
are held by this experiment. At most 4096 objects register per queue. Additional
objects retain existing lazy cleanup and are counted as exclusions; they are not
automatically backfilled when a slot later becomes free. Registration/unregister
are O(1), a persistent cursor rotates work, and callbacks allocate no C++ memory.

Per-pass aggregate Sample categories distinguish current names, in-flight retired
references, and completed-idle references. An explicit unclassified count covers
unvisited FIFO tails and staging reserved slots; their bytes are unknown. Native
allocatedSize charges avoid guessing from logical buffer lengths (small buffers
can have page-backed suballocation). Zero-size category entries have a separate
unknown-byte count. Samples report visited/registered/excluded resources, skipped
busy owners, allocations created by visited owners, and pool references/charged
bytes released by that pass. Samples describe visited owners only, are not an
exact all-resource census, and count shared references rather than unique physical
allocations. They do not measure released RSS or prove memory-pressure attribution.
Queue reporting is aggregate, never a per-resource log.

## Reuse decision

The current owners already supply intrusive Rc, FIFO retirement, stable staging
IDs, resource mutexes and allocation-failure-safe staging freelists. Extending
those owners avoids a second allocator or a strong keepalive registry. The
[upstream Dynamic source](https://github.com/3Shain/dxmt/blob/main/src/dxmt/dxmt_dynamic.cpp)
was inspected and uses on-allocation FIFO recycling. The small
[elseform fork](https://github.com/elseform/dxmt-wine/blob/release/src/dxmt/dxmt_dynamic.cpp)
was also inspected: its process-global idle counters use logical buffer length
and do not distinguish completed from incomplete FIFO entries or supply a safe
idle sweep. Neither is a ready donor for this queue-specific policy. No external
code was copied; existing production-source test fixtures were reused as the
verification pattern. Searches were bounded, not exhaustive.

## Focused verification

From `components/dxmt`:

```sh
python3 tests/vibewine/idle-retirement/run.py --sanitize
python3 tests/vibewine/idle-retirement/run.py --arch x86_64 --sanitize
python3 tests/vibewine/idle-retirement/run.py --expect-retained-baseline \
  --source-ref e11476e33117b454bac60b997532f9823e34855b --sanitize
python3 tests/vibewine/dynamic-retention/run.py --sanitize
python3 tests/vibewine/dynamic-retention/run.py --arch x86_64 --sanitize
python3 tests/vibewine/staging-retention/run.py --sanitize
python3 tests/vibewine/staging-retention/run.py --arch x86_64 --sanitize
```

Native arm64 and Rosetta x86_64 ASan/UBSan passed. The new fixture extracts the
actual production owners and registry, preserves real intrusive Rc, and replaces
only Metal/Windows dependencies with counted allocations. It observes a 32-rename
burst retained at age 299 and reduced from 33 to two live allocations at age 300
using completion sweeps alone. Immutable old owners retain 33; the unchanged
two-allocation acceptance predicate rejects them with exit 2. Cases cover current,
inflight, recorded/deferred Rc ownership, stable staging IDs and allocated but
unpublished staging slots, a second queue with a very different frontier,
registration destruction/unregister races, busy locks, more than one sweep's
resource/entry/release work, cap overflow, and domain survival without resource
keepalive. Maintenance is also exercised with operator new forced to throw.
All five D3D11 pool-construction sites are checked for explicit domain wiring.
An exact 16-resource regression also preserves progress behind an oversized head
that consumes a pass entry budget. Existing lazy Dynamic/staging fixtures remain
passing with the switch disabled.

These are ownership and reclamation source-fixture results. Full matched native/
PE builds and aggregate log wiring belong to the integration owner. No game or
GPU workload was launched for this change; no observed process-footprint, paging,
freeze or FPS improvement is claimed. Hardware correctness and scene-transition
memory attribution remain required before promoting the experiment.

## Ownership and retention

The command queue owns maintenance; resource owners retain their existing FIFO,
slots and backing references. Only the above bounded completed-idle subset is
eligible. Current, incomplete, recorded/external and reserved references are held;
unknown or oversized allocations and cap exclusions retain existing behavior.
There is no new bulk capture producer, persistent service or archive. Aggregate
logs use the existing renderer logger and its existing retention owner; the
experiment creates no separate log file or vendor-retention mechanism. Source
fixtures use a TemporaryDirectory removed by the runner. This document and test
sources are durable result/evidence; the integration owner retains build/check
receipts under the existing experiment retention contract. No unrelated data is
eligible for cleanup.
