# Initializer failure before queue publication

Run `python3 tests/vibewine/initializer-queue/run.py` from the source root.
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
