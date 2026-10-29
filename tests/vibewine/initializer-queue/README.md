# Initializer failure before queue publication

Run `python3 tests/vibewine/initializer-queue/run.py` from the source root.
The runner has current and immutable legacy contract lanes. The historical
lane uses reachable source `3f137007699b139e21db13e060c0d9bd9a449d5a` and retains
every original exception/retry, async/sync, debug, ordering and mutation assertion.
The fixture snapshots and compiles the complete production `CommitCurrentChunk`
method with mocked queue, initializer, statistics, trace, allocator and internal
commit endpoints. It checks that a throwing initializer leaves the event
sequence, all chunk metadata, ready/ongoing counters, statistics, traces and
allocator untouched. A successful retry publishes the same chunk exactly once
with the next unconsumed event value. A following zero initializer event is
propagated correctly. Both ASYNC_ENCODING paths and debug enabled/disabled are
compiled with ASan/UBSan; a mutation restoring the early metadata/event changes
must fail all four modes.

These are logical publication checks, not thread scheduling, real native/GPU
submission, device-error recovery or an installed-runtime test. Root owns the
production change, integration, index, commit and GPU acceptance.

The runner owns only its exact private temporary directory, removes it on exit,
and uses 45-second compile / 15-second execution limits. Generated artifacts
remain below 64 MiB; each captured diagnostic is checked below 1 MiB. No producer
survives execution. Sources and compact receipts are reviewable results under
the existing initializer-repair contract; saved raw receipts inherit its 7-day
expiry, combined 64 MiB cap and active/unresolved holds. Root owns retirement.

## Current terminal-failure contract

The current complete production method is extracted unchanged. Its queue facade
models healthy/terminal status, stop, admission mutex and wait-state notification
and cancellation. Generic initializer exceptions latch DeviceRemoved; bad_alloc
latches OutOfMemory. Both leave published metadata, statistics, counters, traces
and allocator state unchanged; only explicit failure/cancellation endpoints change.
Repeating CommitCurrentChunk cannot call the initializer or admit a retry, and a
second failure cannot overwrite the first or cancel again. Prefailed/stopped calls
do no initialization or publication. Healthy calls publish once, with fresh event
identity and exactly one notification; idle initializer event zero remains zero.

Both ASYNC_ENCODING defines and debug defines are compiled. ASYNC_ENCODING no
longer selects distinct current production branches: current admission always
advances ready_for_encode. Only the immutable historical lane still checks the
old synchronous direct-commit and asynchronous notification/wait paths. Both
lanes retain the early-event mutation and reject changed unpublished metadata.
Captured compiler diagnostics now reach failures instead of being hidden.

Add `--arch x86_64` for the translated architecture. All four define combinations
per lane/negative use ASan/UBSan. The facade does not implement actual wakeups,
workers, native failure cancellation or GPU submission; those are queue-owner
tests. No current guard/catch is removed or replaced by a successful stub.
