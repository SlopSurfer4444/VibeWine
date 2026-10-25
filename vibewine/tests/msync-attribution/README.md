# Bounded optional MSync duration attribution contract

```sh
python3 vibewine/tests/msync-attribution/run.py --arch all --sanitize
```

This extracts the current production enum, state, constants and complete helpers
from the optional duration-attribution comment through the boundary before
`struct semaphore` in `dlls/ntdll/unix/msync.c`. Positive checks replace only
`getenv`, `clock_gettime` and `write` with deterministic observation seams. The
fixture contains no copied implementation of the attribution algorithm. It
prints the extracted-helper and full-client SHA-256 hashes and retains the
production LGPL header in compiled source.

Checks cover:

- Default and invalid environment values: no clock calls or output.
- Enabled completed calls: all four phases' count, total duration, maximum
  duration and error count, including serialized values, process identity and
  interpretation tags.
- First summary at ten seconds, no duplicate admission at that instant, and
  at least ten seconds between subsequent snapshots.
- A maximum of 64 writes, each at most 1 KiB, totaling at most 64 KiB; subsequent
  sequential and concurrent calls perform no clock reads or writes.
- Failed initial/begin/end clock calls, zero start time, and backward end time:
  invalid durations do not update metrics or emit output.
- A gated 32-thread cohort at the same timestamp: exactly one snapshot, no lost
  completed counts after join; another cohort competes for the final budget
  slot, followed by a cohort after shutdown.
- Failed and partial writes are attempted once and consume their admission;
  there is no retry within the interval.

Two independently compiled controls must fail with their expected reason:
raising the snapshot cap to 65, and removing the next-snapshot CAS admission.
Unrelated failures, timeouts or crashes cannot satisfy either control. The
matrix compiles and executes arm64 and x86_64/Rosetta with ASan and UBSan.

Concurrency qualification: the production shutdown flag prevents newly entering
calls from doing work. An already-in-flight call that passed its enabled check
may still read a clock or update cumulative counters after another thread
reaches the cap. Late admissions cannot emit more than 64 writes. Snapshot
fields are independent relaxed cumulative reads and are explicitly marked
approximate, rather than a mutually consistent transaction. The fixture checks
steady-state shutdown after in-flight cohorts have joined; it does not claim a
global barrier at the disabling store.

The runner owns a temporary directory bounded to three C sources and three
executables per architecture, removed on exit. Each owned process has a
ten-second alarm and a 15-second parent kill/reap deadline. It creates no files
or background producer in the Wine runtime. Captured summaries stay in bounded
fixture memory and process output; no retained raw capture is created. Test
source belongs to the Wine test owner. These are focused helper-contract checks,
not a production runtime activation or overhead measurement.
