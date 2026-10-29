# Completed reference-tracker storage retirement

The queue's reference tracker borrows linked-list nodes from a 4 KiB host ring.
The existing ring cleanup already handles completed high-water bursts; this
test finds a separate missing consumer: the finish thread never called that
cleanup for `reftracker_storage_allocator`. A scene's tracking-node high-water
set survived until queue destruction even after every tracked resource released.

The repair adds that ring to the existing finish cleanup, after the command
chunk clears and detaches its tracker and publishes CPU coherence. Encoding
retains resources before publishing the chunk; ring reuse requires a strictly
older coherent sequence, and the ring mutex protects allocation versus erasure.
The 32 queue slots and later sequences preserve pending tracking storage.
Independent/deferred resource references remain separately owned.

```sh
python3 tests/vibewine/reftracker-retirement/run.py --sanitize
python3 tests/vibewine/reftracker-retirement/run.py --arch x86_64 --sanitize
python3 tests/vibewine/reftracker-retirement/run.py --sanitize \
  --source-ref 40e7b88f7f10240d3fe00c4b69669816b1f3791a --expect-retained-baseline
```

The runner snapshots and hashes its inputs, then compiles the complete production
`WaitForFinishThread`, `Retain`, `CommandChunk::reset`, `AllocationRefTracking`,
host allocator and ring bodies. Only dependency includes and unrelated Metal
block allocators are omitted. The host allocator's malloc/free boundary is
counted; thread scheduling, Metal completion, logging, and other pools are mocked.
The production methods are not rewritten. The new cleanup is unconditional;
idle-retirement and residency experiments are disabled in this fixture.

Unchanged acceptance requires ten completed 4 KiB blocks to remain through
completion 301 and shrink to one at 302. The predecessor instead retains ten
and fails with exit 2. Additional old-failing cases require reclamation while
preserving four pending blocks and retirement after all 32 tracker slots clear,
including the historical terminal-error behavior. The current terminal-failure
contract instead freezes true coherence at16 before Error17, retains pending
trackers/storage, and resets them only through the explicit drained-shutdown
seam. Historical source-ref runs select their historical completion contract;
current source must satisfy cancellation, not ordinary Error completion.
Controls cover inline-only tracking,
strict frontier lag, independent resource ownership, pending storage remaining
readable at reset, throwing C++ `operator new` during finish, and ordinary sequence
arithmetic near UINT64_MAX without using the teardown sentinel. Existing ring
retention fixtures provide concurrent producer/collector coverage. This does not
inject `malloc` failure or validate allocation-failure handling inside `Retain`.

This adds no pool, worker, Metal eviction policy, or memory-pressure attribution.
DXVK's pinned [actual-free-chunk reservation](https://github.com/doitsujin/dxvk/commit/9c51dd8b5d67ee3236b7839e6e8827b1f78075c9)
and [rotating relocation cursor](https://github.com/doitsujin/dxvk/commit/e197a8a576de2748e40dcdbfd7a4a81a326115ce)
were inspected through the GitHub connector. Its dedicated-GPU eviction branch
explicitly excludes integrated GPUs; it is not a Metal unified-memory recipe.
Local reuse of the existing completed-ring owner is smaller than transplanting
Vulkan relocation. No donor code was copied and generic ring behavior is unchanged.

Cleanup inherits the ring's linear scan/erase cost and needs later queue
completions; no absolute memory cap, retirement during a fully idle queue,
process-footprint reduction, game stability or FPS result is claimed. Full
matched build and runtime acceptance belong to the integration owner.

No persistent producer is added. Fixture build products use a private temporary
directory removed on exit. Optional retained receipts belong to the wave's
`memory-queue-wave-next/retirement` scratch owner: at most 5 MiB, raw seven days,
results thirty days, active review/recovery holds, manual exact-path cleanup.
Committed source/tests and provenance remain durable project results.
