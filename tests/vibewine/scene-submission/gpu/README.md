# Native Metal scene-submission discriminator

The integration owner alone runs this probe. Building it does not execute Metal,
Wine, a game, or the resulting native programs. It does not modify runtime
selection or installation. Use the existing GPU-lab owner's exclusive GPU slot
and admitted native execution/isolation policy, with no competing GPU tests.

```sh
python3 tests/vibewine/scene-submission/gpu/build.py --output /absolute/new/build-directory
```

The builder reuses the sibling CPU fixture's generator and inserts the actual
production `Present1`, `SyncFrameState`, `SyncFrame`, `PrepareFlush`, `Flush1`,
`Flush` and `Commit` bodies. Their queue/Metal boundary remains an adapter. The
native hooks create real command buffers and shared, hazard-untracked buffers;
scene work fills 4 KiB with `0x5a`, and final work waits an MTLFence then copies
that data. The actual production missing-drawable fallback encoder body is also
extracted and executed through native Metal wrappers. No production test gate
or diagnostic ABI is introduced.

Once the parent has admitted the native build, run **sequentially**, preserving
each natural exit code and capping stderr/stdout at 16 KiB each:

```sh
/absolute/build-directory/reference > reference.jsonl 2> reference.stderr
/absolute/build-directory/candidate > candidate.jsonl 2> candidate.stderr
/absolute/build-directory/negative-no-flush --expect-split > negative.jsonl 2> negative.stderr
/absolute/build-directory/negative-no-fallback-edge > fallback-negative.jsonl 2> fallback-negative.stderr
```

Both normal runs must naturally exit zero and emit exactly eight passing cases
(four cases, each with ordinary and MetalFX callback selection), then one
`{"summary":"pass","cases":8}`. The source mutation deliberately removes the
production Flush; with `--expect-split`, it must naturally exit 10 with
`FAIL production grouping does not match expected split`. Any timeout, unsupported
device (exit 2), unrelated failure, or missing/truncated record is inconclusive.
The process watchdog exits 124 after 25 seconds; the parent also needs an
independent 30-second deadline. No unbounded GPU wait is used by the probe.

The held-acquire case proves that reference scene data is still zero while
acquisition is blocked, whereas candidate scene data has completed on the GPU
before acquisition is released. It also retires the scene callback storage
without releasing final frame admission. The final chunk alone owns fence value
5 and destroys the frame token after actual final GPU completion. The immediate
case reports the extra command-buffer count and elapsed probe duration for an
overhead control; these one-shot timings are not a performance benchmark or a
zero-overhead assertion.

The fallback-edge source mutation must naturally exit 10 with
`FAIL fallback fence operations missing`, after its held-acquire cases pass.
The nil-drawable and encoder-failure **adapters** select the actual production
nil fallback body, complete native fence operations and retain final ownership.
They use distinct upstream/downstream fences and a following native consumer
that must read the scene golden; exact wait/update/end call counts prevent an
omitted production fallback edge from passing accidentally.
They do not create a CAMetalDrawable, make the actual presenter fail, force a
Metal command-buffer error, or test encoder-allocation failure. This native
fixture does not execute DXMT's actual queue worker, resource hazard optimizer,
query readbacks, bridge, shader compiler, scaler, ring allocator, or retirement
thread. In particular, explicit fence/data success here verifies the native
adapter transfer; it does not prove all production cross-chunk resource edges.
The held case has completed the scene before final encoding; only immediate
acquire permits GPU overlap, and no deterministic missing-fence negative is
claimed. Run matched-runtime GPU regressions separately, then visible gameplay
for displayed FPS and input-latency acceptance.

The builder records production-source, extracted body, generated source, shared
CPU adapter and binary hashes. Its compiler output uses the existing GPU-wave
bounded runner (64 KiB log cap, 60-second timeout, fixture-owned process-group
cleanup). It builds reference-off, candidate-on, no-Flush and missing-fallback-edge
source mutations.
Both switch paths compile from the same candidate source snapshot.

Owner: this fixture directory for source; integration owner for generated builds
and execution evidence. Generated scope is only an explicitly supplied new
directory. Data class is source snapshots/binaries/compile receipts and fixed
numeric test records; no game/user data, screenshots, capture or network data.
Retain under the existing campaign policy: acceptance plus seven days, 64 MiB
per build/run bundle, 16 KiB per runtime stream, 64 KiB per compile log, 32 KiB
receipt, 1 MiB generated source and 16 MiB per binary. Active runs, unresolved
failures and pinned recovery evidence are held. Manual owner cleanup of exact
fixture-created paths after holds clear; no automated cleaner is installed.
