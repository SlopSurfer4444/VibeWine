# Bounded MSync registration acknowledgment

The client spins at most 4,096 processor-yield iterations while waiting for the
server to register a multiple-object wait. It checks object/alert readiness and
the deadline every 64 turns, then arms thread state 3 (`MSYNC_ACK_PARKED`) and
parks. The server atomically publishes registered state 1 and wakes an armed
client. This is a paired client/server change; message sizes and ordinary
registered-wait state 1 are unchanged. Existing early-unregister and
signaled-index-one cleanup are preserved.

The ACK park uses at most 10 ms per call, or the remaining object deadline if
shorter. A slice expiration rechecks the ACK, objects, alert and deadline; it is
not itself an object timeout. EINTR, EFAULT, compare mismatch and spurious wakes
recheck the state. A timeout, object/alert cancellation or unexpected park error
removes the one submitted registration and balances its local waiter counts.
An unexpected park error returns `STATUS_PENDING` for the outer wait to retry.

Before each ACK park, and after failed registration transport, the client checks
its Mach send right. A dead/invalid right follows Wine's existing
`abort_thread(0)` server-loss convention. It sends no removal to a dead server;
that server epoch is gone. Other Mach inspection errors leave the ordinary
bounded recheck active. This guard covers registration acknowledgment, not
every later Wine/server wait or global server-death handling. No watcher thread,
additional Mach port, background producer or installed runtime change is added.

Adaptation source: kageroumado,
[Dormison c9966373a268a1fdafe398634c19be653c674ed0](https://github.com/kageroumado/dormison/commit/c9966373a268a1fdafe398634c19be653c674ed0),
original co-author Claude Fable 5.1 `<noreply@anthropic.com>`. The paired ACK park
and interrupted-wake retry are adapted, without its direct-object spin options,
metrics subsystem or address-wait changes. The fixed ACK slices and send-right
inspection are local adaptations. MSync remains LGPL-2.1-or-later; retain
Zebediah Figura and Marc-Aurel Zent's original source notices and the existing
registration-cleanup attribution.

Run from the Wine component root:

```sh
python3 vibewine/tests/msync-ack/run.py --sanitize --native
python3 vibewine/tests/msync-ack/run.py --revision bee453b09460e0c3eb49b7c491a6d04e65cc38a5 --expect-regression --sanitize
python3 vibewine/tests/msync-ack/run.py --mutation missing-ack-reset --sanitize
python3 vibewine/tests/msync_registration.py --source-dir .
```

The first fixture compiles the actual client registration/removal/contention/
multiple-wait bodies and server list/pump/wake/ACK bodies. It requires 19 current
assertions to pass; the named baseline must fail exactly 15 assertions, with
four unchanged-path controls passing. Deterministic transport, time, load and
ulock seams place ACK before arming and after parking, delay the pump, inject
EINTR/EFAULT/EAGAIN, spurious wakes, alert cancellation, deadline expiration,
unexpected park failure and server loss during registration/before/after park.
Cancellation is drained through the production pump, with zero surviving nodes
and balanced object/alert counts. An observation cap releases baseline busy
loops; every affected assertion requires that cap untouched. It is not an
expected-timeout or expected-crash test. The pre-existing registration fixture
still requires all 16 assertions and its six exact older-baseline failures.
Infinite object waits must receive a zero timeout after ACK; finite waits must
receive their remaining deadline. The `missing-ack-reset` mutation requires two
specific failures when the ACK slice leaks into the ordinary infinite wait.

On Darwin `--native` additionally compiles the actual server ACK/wake helpers
against real `__ulock_wait`/`__ulock_wake`, shared anonymous memory and one owned
forked child. Its client is explicitly a harness protocol driver. It performs
2,000 cases (500 each ACK-before-wait, parked, racing and signaled), requires real
successful cross-process wakes and at least one real interrupted wait, checks
missing/dead-child deadlines and reaps its child. A store-only ACK negative
control must fail specifically because ACK is observed only after the kernel
wait times out. Wake EINTR is deterministic-seam coverage; it need not occur in
the native run. A separate native check exercises the production client
liveness helper against live, dead-name and invalid Mach rights and releases
all test-created rights. This checks actual port teardown, not a Wine server
process lifecycle.

Compilation is capped at 60 seconds; each fixture process at 20 seconds. Native
parent/child watchdogs are 10/9 seconds. Generated sources/binaries belong to
`TemporaryDirectory` and are removed on exit; anonymous mappings are unmapped
and owned children reaped. There is no persistent log/capture producer or
retention change. Source, fixture and these instructions are retained in Git.

ASan/UBSan are available with `--sanitize`. These checks do not launch Wine,
installed prefixes or games, replace a runtime, establish full Wine/Mach
transport integration, or measure FPS. Reduced ACK busy work is an implementation
property; workload latency, CPU, power and game-performance effects require a
separate matched-build comparison.
