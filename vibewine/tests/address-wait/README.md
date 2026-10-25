# Address-wait timeout and queue publication regressions

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
The timeout adaptation forces the queued/claimed decision under the queue lock
on every timeout. The later publication adaptation adds `ReadPointerAcquire`
only to the non-timeout precheck; every timeout still acquires the queue lock.
It preserves backend drain errors using the existing `STATUS_ALERTED` to
`STATUS_SUCCESS` conversion, rather than unconditionally reporting success.

This is source-level protocol and native Darwin backend evidence, not a full
Wine build, Windows ABI test, allocation/table lifecycle test, installation,
activation, game test or performance measurement. It does not resolve
independent raw alerts racing the drain:
the backend has one coalescing alert flag, so an unrelated alert can satisfy
the drain before the selected waker delivers its own alert. Non-timeout cleanup now uses the acquire/release lifetime protocol described
below. It does not add a drain to raw-alert returns. A claimed waiter depends on the waker
eventually delivering its promised alert.

The test owns its temporary generated source/binary and per-process native
kqueue descriptors. The files are removed by `TemporaryDirectory` at process
exit; descriptor replacement happens only after workers join, and process exit
closes the final owned descriptors. No background producer is started.


## Queue publication adaptation (2026-10-25)

Source: Paul Gofman's upstream Wine commit
[`81e52628b25ac173d04796e8a9b9cda637d3e233`](https://github.com/wine-mirror/wine/commit/81e52628b25ac173d04796e8a9b9cda637d3e233),
authored 2026-10-14. The checked donor changes only `dlls/ntdll/sync.c`.
Its existing Wine header and `COPYING.LIB` license it LGPL-2.1-or-later.
The upstream hunk is reused because it repairs the existing queue protocol
without importing another backend, changing the per-thread alert flag, or
replacing our accepted GE timeout-drain adaptation.

`entry.addr == NULL` permits a non-timeout waiter to return its stack frame.
Both wakers therefore detach the link and snapshot every required field before
`WritePointerRelease` publishes NULL. The waiter's unlocked precheck uses
`ReadPointerAcquire`. WakeAll keeps the captured thread ID across a full
256-element batch flush. Every timeout still takes the queue lock and a claimed
timeout still consumes the promised alert with `NtWaitForAlertByThreadId(NULL,
NULL)`; drain errors retain their original NTSTATUS. No Unix backend changed.

`publication.c.in` extracts the complete current address-queue bodies verbatim
and uses real `wine/list.h`. Atomic operations, a raw per-thread alert result,
thread identity, delivery and scheduling are fixture seams. A waker pauses
immediately before the real list removal, then an independent raw alert lets
the waiter continue. On the old source the waiter reaches its final production
TRACE before detach; on the current source it attempts the cleanup lock and
cannot proceed until the waker detaches the entry. The TRACE seam holds the
waiter's still-live stack frame before the return, so the exact old failure is
detected without letting either thread access an expired frame. Both Single
and All run. Three-second schedule watchdogs, a 60-second compile bound and a
30-second process bound apply. The runner admits only the complete expected
stdout, stderr and exit status, including expected-baseline failures.

A separate WakeAll case queues 257 selected entries and two unrelated addresses
in the same hash bucket. The release seam changes the published entry's thread
ID; delivery must still contain the original IDs 1 through 257 in order. It
checks the 256-entry flush while holding the queue lock, the final one-entry
flush after unlock, and both retained collision entries and links. This checks
that no later ID read depends on a published entry. The fixture does not prove
arbitrary machine reorderings or integrated PE/Unix ABI behavior. The actual
pointer helpers already exist in `include/winnt.h`; fixture atomics use Clang
acquire/release builtins at that seam.

Verified on 2026-10-25 on macOS arm64, with `-O2`, `-Wall -Wextra -Werror`, ASan
and UBSan, both native arm64 and translated x86_64:

```sh
# Run each command natively and again with --arch x86_64.
python3 vibewine/tests/address-wait/run_publication.py --sanitize
python3 vibewine/tests/address-wait/run_publication.py --revision 85864be47d483d4518f729baef48cbf52ea1e5cf --expect-regression --sanitize
python3 vibewine/tests/address-wait/run.py --sanitize
# Earlier source predates the separately accepted GE timeout fix.
python3 vibewine/tests/address-wait/run.py --revision d2237a9a82d9960360ce8400242ddeae6646c347 --expect-regression --sanitize
```

The first command passes both raw-alert schedules and the 257-entry control;
the publication baseline reports exactly two regressions. The timeout suite
still passes six controlled races per backend, ordinary/raw/size/coalescing
controls and backend/native drain error propagation; its earlier baseline
reports exactly six regressions per backend. Darwin runs both production Unix
branches, including native kqueue. No installed runtime, game, FPS result or
full Wine build is claimed here. A separate read-only review found no
production blocker and corrected the fixture's initially unspecified order of
two test calls before final verification.

The fixtures own only temporary generated C sources and binaries, each below
64 MiB, and process-local threads/native descriptors. `TemporaryDirectory`
removes their files on normal exit; a crash leftover is private raw scratch
with a seven-day TTL, 64 MiB total byte cap and manual owner cleanup after
checking there is no live process/active hold. No active test artifact may be
removed. Source fixtures and these curated results are durable project source;
no persistent data producer or background worker is activated.
