# Deferred initializer completion refresh

The initializer reads its upload event immediately after commit. With real
asynchronous completion, that value can still be stale when the next upload
arrives. `allocateChecked` may successfully allocate another 32 MiB block, so
its allocation-failure retry never discovers the already completed storage.

The production change reuses the existing shared event and checked ring owner:
before the first reservation in a new submitted generation, read completion
once if the cached frontier is still behind. Generation 1 adds no reads;
already current completion adds no reads; further reservations in the same
pending generation add no speculative reads. Existing finite OOM submission /
refresh / retry remains unchanged. No wait, ring API, retirement call, reference
owner, policy default or inclusive/exclusive frontier rule changes.

```sh
python3 tests/vibewine/initializer-completion-refresh/run.py --mode default
python3 tests/vibewine/initializer-completion-refresh/run.py --mode off
python3 tests/vibewine/initializer-completion-refresh/run.py --mode on
python3 tests/vibewine/initializer-completion-refresh/run.py --mode on --arch x86_64
```

Run each of the three modes on both architectures for the full matrix. The
runner snapshots/hashes the complete production initializer, checked ring,
allocation tracker and actual Metal wrappers. It imports the existing
transaction runner and preserves all 31 transaction cases. Only dependency
includes are removed from production bodies; the native C API mock is extended
with independent submitted/completed sequences, counted event reads and real
byte-backed staging buffers. These are CPU mocks, with no Metal/GPU execution.

Seven added cases check:

- Two full submitted blocks completed after their immediate post-commit reads
  serve the next upload with two native allocations, avoiding a third. This
  works with default, explicitly disabled and enabled inclusive reuse.
- A latest completed full block is reused only by inclusive mode: one native
  allocation instead of the stale-cache predecessor's two. Default/off retain
  the existing strict frontier and allocate a second block.
- Distinct full 32 MiB payloads remain readable until controlled completion,
  covering both submitted-inflight and recorded-pending generations. Submission
  records a hash of every source byte; completion reads those backing bytes
  again, so an early overwrite is observable.
- 128 successful reservations before submission perform zero reads; 128 more in
  the next pending generation add exactly one speculative read; the following
  submitted generation gets its own opportunity.
- A fresh cached completion neither polls nor regresses.
- Completion arriving after the generation's first read still permits the
  existing failed-growth / valid-prefix submission / successful-retry path.
- Encoder failure preserves sequence, queued payload, CPU arena and poll count;
  a subsequent retry submits exactly once.

The unchanged acceptance rejects the exact predecessor without checking it out:

```sh
python3 tests/vibewine/initializer-completion-refresh/run.py \
  --source-ref b3e92edfb749eac59d90dcf2932dfd4cef24bbe5 --mode on --case latest
```

It observes two allocations where one is required. Explicit mutations also
must fail, each through an exercised seam:

```sh
python3 tests/vibewine/initializer-completion-refresh/run.py --mutation no-refresh --case latest
python3 tests/vibewine/initializer-completion-refresh/run.py --mutation always-poll --case bounded
python3 tests/vibewine/initializer-completion-refresh/run.py --mutation unsafe-frontier --case pending
```

The unsafe-frontier mutation is rejected by the delayed full-payload read,
showing the mock can detect premature upload overwrite. Separate existing
ring-checked, ring-retention, dynamic-retention, staging-retention and
initializer-inclusive-reuse suites retain their ownership, rename, replay,
destruction, allocation-failure, sequence-limit and retirement controls.
The existing inclusive suite's `refreshed_completion_upload` first consumes the
new generation opportunity through the production helper and checks its marker
and observed frontier, then deliberately stales its private cache. This keeps
its independent OOM retry and original failed-allocation counts exercised;
the new latest-completion case measures the ordinary avoided attempt.

This is bounded opportunity reuse, not a heap cap: completion can arrive after
the one speculative read, and successful growth within the same generation
still uses its cached frontier. One extra native event read per stale generation
has a CPU cost which has not been timed in the production bridge. The 32 MiB
source discriminator establishes allocation work avoided, not process RSS,
game-memory leak attribution, real GPU lifetime, FPS or installed acceptance.
Native retain/release remain mocks; delayed hashes establish source-byte
preservation under this model rather than native object ownership.
Root owns matched builds and any live integration verification.

No new producer, cache or retention policy is introduced. The runner owns an
exact private temporary directory removed on exit; each captured diagnostic
has a 4 MiB hard limit, compilation a 60-second deadline and execution a
45-second deadline. It executes under the existing network/write-denying
sandbox. Retained branch logs inherit seven days / 32 MiB, curated results
thirty days / 8 MiB, active/unresolved/recovery holds and manual exact-path
cleanup from the sustained campaign. There is no installed cleaner.
