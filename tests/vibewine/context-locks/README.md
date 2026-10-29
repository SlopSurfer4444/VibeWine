# Context lock contract regression

Run from the DXMT component root:

```sh
python3 tests/vibewine/context-locks/run.py --sanitize
python3 tests/vibewine/context-locks/run.py --arch x86_64 --sanitize
python3 tests/vibewine/context-locks/run.py --source-ref e7b02196a31dc633ac802c9c27b88dbdd8f40dca --expect-unlocked-baseline --sanitize
```

The final command confirms that all four old entries reach their dependencies
without owning the protected device mutex. Omitting `--expect-unlocked-baseline`
must fail against that old source. `--source-ref` reads Git objects without a
checkout or worktree mutation.

The runner copies the complete production `WaitUntilGPUIdle`, `TemporalUpscale`,
`BeginUAVOverlap`, and `EndUAVOverlap` methods verbatim into CPU contexts. It also
copies the recursive device mutex and deferred `null_mutex` verbatim. Resource,
Metal, queue and tracing dependencies are observable doubles; Windows thread IDs
are represented by unique thread-local IDs. The mutex's processor pause hint is
mapped to an observable yield, retaining its compare-exchange, spinning,
recursion, and memory ordering. Test-only private visibility exposes ownership.

The main thread holds the real device mutex until the worker is observed in its
contended spin loop, or until an old unlocked operation finishes. The protected
dependencies assert actual ownership. No scheduling sleep establishes success;
timeouts bound a stalled test. Additional cases exercise outer recursive locks,
temporal cache hit/miss and downscale allocation, an early resource return,
deferred commands, disabled protection, idle commit/fence sequencing, and UAV
command values.

This verifies the CPU entry-lock contract, not GPU execution, the full renderer,
game behavior, or performance. Generated sources and binaries exist only in a
temporary directory removed by the runner. Receipts are printed to stdout with
source, fixture and runner hashes; the caller owns any retained output.

The queue adapter returns an explicit healthy/failed bool from `WaitCPUFence`
and exposes `ShutdownAndJoin` with observable wait/shutdown counters. A failed
wait is exercised through the complete unchanged production method under the
real contended mutex. Its dependency order must be sequence→invalidate→commit→
wait→shutdown; shutdown endpoints assert lock ownership, and method return must
release the mutex. Healthy waits must never shut the queue down. Existing mutex,
recursive/cache/readback endpoint and unlocked-baseline assertions are retained.

```sh
python3 tests/vibewine/context-locks/run.py --sanitize --mutation skip-failed-wait-shutdown
```

The generated negative removes only the failed-wait shutdown guard and must fail
exactly the shutdown/join assertion. It cannot pass through a compiler error or
a sanitizer crash. Queue execution/joining remains an observable CPU facade; this
does not test native queue threading, GPU completion or cancellation internals.
The historical public baseline keeps its original four unlocked-entry checks and
does not assert failure behavior absent from that source.
