# Pool CPU cost candidate, 2026-10-26

Base: `96940d73ac28f57fe12df832d246d3e84965f2cd`.
Owner: pool-cost branch; integration/index/build/runtime ownership remains with
its parent. No runtime, game cache or installation was changed.

## Changes and bounds

- Both dynamic allocation owners inspect the FIFO head by reference and move its
  Rc before popping it. Ready and incomplete head checks no longer copy/release
  that Rc, avoiding one atomic increment/decrement pair per inspected head in
  the default path. No reference is used after pop.
- When the existing `vibewine_idle_retirement` opt-in is compiled in, each
  BufferAllocation lazily caches its first positive native allocatedSize.
  Dynamic-buffer and staging sweeps use that value for the same charge/category
  accounting. An unknown/zero result retries on a later read, and still cannot
  authorize release against the byte budget.
- The cache belongs to the allocation object, not a native handle or staging
  vector ID. New native allocations start unknown even when pool IDs are reused.
  Renames and suballocation offset changes preserve the same buffer. Its native
  obj_ and fixed length are set only at construction; these pool owners use
  ordinary fixed-length buffers, with no sparse mapping, aliasability or
  purgeability mutation path. No zero/length estimate substitutes for the
  actual native charge. The atomic is a standalone scalar and publishes no
  other state; overlapping first readers may redundantly query it.
- Textures remain uncached. Apple documents allocatedSize as the resource size in bytes,
  without an explicit guarantee that all texture kinds have immutable charges:
  <https://developer.apple.com/documentation/metal/mtlresource/allocatedsize>.
  This change makes no sparse/compression/heap-wide caching assumption.
- All existing 300-sequence age, byte/release/scan budgets, warm-spare behavior,
  current/inflight/command-list/reserved-ID exclusions and nonblocking locking
  remain in force. There is no new retention rule, collector or global map.
  The opt-in adds one atomic uint64_t member per BufferAllocation; it is absent
  when retirement is compiled out. These are allocation charges/pool references,
  not physical GPU residency or process RSS.

## Focused evidence

The idle-retirement runner compiles the production registry and pool owner
headers/methods verbatim, the actual production cache method/member extracted
from BufferAllocation, and the real intrusive Rc. Only native allocations are
substituted by counted boundary objects; this does not execute Metal or prove
native bridge latency, GPU behavior or FPS.

- Two unchanged sweeps over current+8 FIFO allocations: DynamicBuffer and
  StagingResource query counts 18 -> 9; DynamicLinearTexture remains 18.
- Warm-spare rename adds zero native queries for the cached buffer owners.
- Retirement releases the same 7 allocations / 28,672 bytes; samples, lifetime,
  byte limits and protected owners remain checked.
- Zero -> zero -> positive retries then releases exactly charged allocations;
  new current objects and reused staging IDs report their new 8,192-byte charge.
- Ready and incomplete dynamic head checks both perform zero extra Rc increments
  or decrements; object/reference lifetime and returned identity remain equal.
- Concurrent first readers exercise the actual cache method, followed by a
  no-new-query warm read. Existing destruction/unregister races still run.

Commands passed from this source tree:

```
python3 tests/vibewine/idle-retirement/run.py --sanitize
python3 tests/vibewine/idle-retirement/run.py --arch x86_64 --sanitize
python3 tests/vibewine/idle-retirement/run.py --check-reuse-with-retirement-off
python3 tests/vibewine/idle-retirement/run.py --source-ref 96940d73ac28f57fe12df832d246d3e84965f2cd --expect-uncached-baseline
python3 tests/vibewine/dynamic-retention/run.py --sanitize
python3 tests/vibewine/staging-retention/run.py --sanitize
git diff --check
```

The immutable predecessor passes its old count expectations and is independently
rejected by the unchanged getter target and reference target, both exit 2.
Native and x86_64 cache/owner suites ran AddressSanitizer + UndefinedBehaviorSanitizer.
The feature-off check compiles and exercises the default dynamic reuse path.
Full matched PE/native build and controlled gameplay/FPS validation remain with
the integration owner. No FPS improvement is claimed here.

## Further source hypotheses, not implemented

| Candidate | Source and possible cost | Measurement and rejection condition |
|---|---|---|
| Completion status bridge reuse | `src/dxmt/dxmt_command_queue.cpp:271,277,287` queries command-buffer status initially, again for tracing, and for errors. `status()` crosses the Wine/native bridge. Cache terminal status after any wait and reuse it. | Count/time calls per completed chunk, preserve pre-wait/terminal/error behavior and trace data. Reject if status semantics require another read or saved CPU cost is negligible. |
| Repeated ring-block scans | `src/dxmt/dxmt_ring_bump_allocator.hpp:192-223` scans a completed prefix twice under allocator mutex each free_blocks call; command queue calls three allocators at `dxmt_command_queue.cpp:329-331`. | Sample list lengths, total visited blocks and lock duration. Check warm standard-block preservation, paused-frontier call aging (>300), reuse order and oversized block release. Reject if lists are tiny or new bookkeeping changes these boundaries. |
| Foreground burst destruction | `src/dxmt/dxmt_dynamic.cpp:9-18,122,226` and `dxmt_staging.cpp:155-170` may pop a whole old burst during Map/reuse. Each last Rc can destroy native resources synchronously. | Correlate pop/destructor counts with CPU Map-tail latency and hitch traces. A bounded foreground slice would require an admitted background owner and equivalent retention. Reject if no stalls appear or retained-byte/lifetime acceptance would regress. |
| Repeated unchanged pool classification | `src/dxmt/dxmt_dynamic.cpp:46-55` and staging sweep still revisit FIFO categories every heartbeat despite a bounded 128-entry pass. The buffer cache removes bridge calls but not repeated traversal/locking. | Count entries revisited at repeated coherent frontiers and measure sweep CPU/lock time. Any cached totals or cursor must account for renames, reserved IDs, owner detach and bounded release without changing truthful unknown tails. Reject if traversal is negligible or invalidation makes correctness more expensive than scanning. |
