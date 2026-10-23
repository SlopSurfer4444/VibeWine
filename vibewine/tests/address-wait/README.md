# Address-wait timeout regression

The native fixture extracts the current `RtlWaitOnAddress`,
`RtlWakeAddressSingle`, `RtlWakeAddressAll`, their queue helpers, and the Unix
`NtAlertThreadByThreadId`, `NtAlertMultipleThreadByThreadId`,
`NtWaitForAlertByThreadId` and timeout helpers verbatim. It uses the component's
actual `wine/list.h`. Thread identity, alert-entry allocation and
scheduling/atomic seams are supplied by the fixture. On Darwin the runner
defaults to both production branches: `USE_FUTEX` with a syscall shim, and
`HAVE_KQUEUE` with native `kqueue`/`kevent` syscalls in this process. Select one
with `--backend futex` or `--backend kqueue`. Other hosts default to the futex
fixture.

```sh
python3 vibewine/tests/address-wait/run.py
python3 vibewine/tests/address-wait/run.py --sanitize
python3 vibewine/tests/address-wait/run.py --revision d2237a9a82d9960360ce8400242ddeae6646c347 --expect-regression
```

Each Single/All race has three deterministic schedules: the waker dequeues the
waiter but delays its alert; the alert is already pending when the kernel
reports timeout; and the waker dequeues while the waiter is about to acquire
the cleanup lock. On the baseline all six calls return `STATUS_TIMEOUT` and
leak an alert that makes an actual subsequent `RtlWaitOnAddress` on a different
address return success. With the change all six return success and the unrelated
wait times out. Delayed delivery explicitly observes entry into the second
infinite-timeout wait. Each schedule has a bounded watchdog, compilation has a
60-second timeout, and each binary has a 30-second timeout. The runner accepts
only the complete expected stdout, stderr and exit status; sanitizer reports or
additional failures cannot pass `--expect-regression`.

The Darwin branch registers the same native event as production
`get_tid_alert_entry`: ident 1, `EVFILT_USER`, `EV_ADD | EV_CLEAR`. The first
zero-timeout `kevent` really returns 0 before its result is paused for the test
schedule. Subsequent production `NOTE_TRIGGER` calls really reach the kernel,
and their syscall success is checked because production `NtAlertThreadByThreadId`
itself ignores that return value. The native infinite-timeout drain retrieves
the real event; its identity/filter are checked. The following native zero poll
confirms that no event remains. No diagnostic poll consumes the event before
the drain. This proves native event retention and consumption across the
controlled timeout race, without claiming that every scheduled drain actually
slept in the kernel before delivery.

Controls exercise comparisons and true timeouts for 1/2/4/8-byte addresses,
invalid size, empty/null wakes, raw per-thread alert consumption, and ordinary
Single/All wakes without an extra infinite wait, and two raw alerts coalescing
into one retrieval followed by timeout. An injected failure on the
second production Unix alert-entry lookup verifies that a drain failure remains
an error; it does not pretend the outstanding alert was consumed. Darwin also
substitutes fd -1 for the drain's real `kevent` call and checks production
`STATUS_INVALID_HANDLE` propagation. The valid owned descriptor is preserved
for the later alert and wait.

The source change adapts Jason Eggleston's patch supplied by
[GE commit `98efe1cdefd7fc2f7ffb34044c194e046d739d6e`](https://github.com/GloriousEggroll/proton-ge-custom/commit/98efe1cdefd7fc2f7ffb34044c194e046d739d6e).
The supplied donor patch was read from that repository at
`fc1e3cb72de062d536e84bf357d1aa55ff6c62a4`. Its author header reports original
upstream commit
[`jegglest/wine@dee180fa836108dcb61c520708721b8bed85ca42`](https://github.com/jegglest/wine/commit/dee180fa836108dcb61c520708721b8bed85ca42);
that original commit was not independently retrievable during this review.
The Wine source and these fixtures are LGPL-2.1-or-later; see `COPYING.LIB`.
The local adaptation forces the queued/claimed decision under the queue lock on
every timeout instead of importing the donor's `ReadPointerAcquire` fast path.
It preserves backend drain errors using the existing `STATUS_ALERTED` to
`STATUS_SUCCESS` conversion, rather than unconditionally reporting success.

This is source-level protocol and native Darwin backend evidence, not a full
Wine build, Windows ABI test, allocation/table lifecycle test, installation,
activation, game test or performance measurement. It does not resolve
independent raw alerts racing the drain:
the backend has one coalescing alert flag, so an unrelated alert can satisfy
the drain before the selected waker delivers its own alert. Existing non-timeout
lockless cleanup behavior is unchanged. A claimed waiter depends on the waker
eventually delivering its promised alert.

The test owns its temporary generated source/binary and per-process native
kqueue descriptors. The files are removed by `TemporaryDirectory` at process
exit; descriptor replacement happens only after workers join, and process exit
closes the final owned descriptors. No background producer is started.
