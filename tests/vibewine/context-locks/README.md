# Context lock contract regression

Run from the DXMT component root:

```sh
python3 tests/vibewine/context-locks/run.py --sanitize
python3 tests/vibewine/context-locks/run.py --arch x86_64 --sanitize
python3 tests/vibewine/context-locks/run.py --source-ref 1d1b8e65 --expect-unlocked-baseline --sanitize
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
