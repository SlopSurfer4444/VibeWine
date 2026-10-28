# MSync clock domains and finite waits

Run on Darwin from the Wine component root:

```sh
python3 vibewine/tests/msync-deadline/run.py --arch arm64 --sanitize
python3 vibewine/tests/msync-deadline/run.py --arch x86_64 --sanitize
python3 vibewine/tests/msync-deadline/run.py --revision bb2c8919 --expect-regression --arch arm64 --sanitize
python3 vibewine/tests/msync-deadline/run.py --revision bb2c8919 --expect-regression --arch x86_64 --sanitize
```

The runner extracts actual `msync_get_deadline`, `update_timeout`,
`msync_timeout_ns`, `ulock_wait` and `msync_wait_single` bodies. Negative relative
NT durations become negative encoded monotonic deadlines; positive absolute NT
deadlines stay in the system-time domain. Wine's `NtQueryPerformanceCounter`
returns 100 ns ticks on Darwin (`sync.c` uses `mach_continuous_time`). The fixture
substitutes independent system/monotonic clocks without changing host clocks.
It checks both directions of wall-clock adjustment, monotonic expiry, absolute
clock behavior, zero polling, extreme relative duration and nanosecond saturation.

The real legacy wrapper is forced through its availability seam to check the
32-bit microsecond limit, finite rounding and the zero infinite value. A finite
legacy wait longer than one syscall can be served in chunks; an early kernel
`ETIMEDOUT` must recheck the remaining deadline. The single-wait production loop
is exercised through both an interrupted syscall and an early chunk expiry.
The separate ACK fixture covers the same finite continuation after registration.

Candidate mode also uses a private shared anonymous mapping and the actual
Darwin `__ulock_wait2`/`__ulock_wake` APIs to observe a 20 ms finite expiry and a
thread-triggered wake before a one-second deadline. No host clock, existing
Mach right, installed Wine runtime or game is changed. `sysctl.proc_translated`
records native versus Rosetta execution. ASan/UBSan detect memory/undefined
behavior; elapsed bounds distinguish actual timeout/wake from immediate success.
The real-API cases use the fixture's monotonic counter, not an installed NTDLL.

The old-source mode wraps the exact former initialization block from
`msync_wait_objs` and extracts the old timeout/wait bodies. It must fail exactly
eight named assertions while the six unchanged controls pass. It deliberately
omits the new extreme-duration and native cases; an unrelated crash or sanitizer
failure is never accepted as the expected regression. Candidate mode requires
all 18 assertions. The existing WaitAll fixture separately compiles the complete
production top-level wait and verifies its new helper/deadline arguments, APC
precedence and rollback paths.

This repair adds no mapping-RPC timeout, peer-cancellation protocol or pump
validation. An absolute deadline is re-read between kernel waits; it does not
subscribe a sleeping syscall to wall-clock-change notifications. Infinite and
zero object waits retain their existing contract.

The invoking branch owns temporary source/binary artifacts, removed through
`TemporaryDirectory`; compile/process watchdogs are 60/10 seconds. Output is
bounded by the fixed cases. Saved logs are branch-owned for 7 days/32 MiB;
results/source evidence 30 days/8 MiB, excluding active review and explicit holds.
Cleanup is manual by the exact owner. No persistent producer or cleaner is added.
