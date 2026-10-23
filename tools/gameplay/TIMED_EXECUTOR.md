# Local timed executor

`timed-executor.mjs` is the actual Node adapter for `createLifecycle`'s `timed`
argument. It spawns `timed-executor.py` once per interval. The Python worker uses
the existing virtual-controller functions under one nonblocking session lock and
executes the native `cs2trace inspect`, `arm`, `stop` and `decode` verbs. It never
launches a game, sends GUI input, changes focus, or starts a background daemon.

The root test owner must first admit the matched candidate, ordinary launch,
real process birth generation, single loaded controller provider and its Windows
PID. The file protocols cannot establish loaded DLL identity or process birth by
themselves. In particular native `inspect` reports `live_pid_verified:false`.
The worker verifies fresh controller receipts against the supplied exact token,
executable and Windows PID, and verifies trace device/inode and generation.

## Integration

Import `createTimedExecutor` from this directory's `timed-executor.mjs`, then use:

```js
const timed = createTimedExecutor({
  python: pythonExecutable,        // absolute Python 3 executable, stdlib only
  workerPath: absoluteWorkerPath,  // tools/gameplay/timed-executor.py
  admission: {
    candidateId,
    controllerDirectory, controllerToken, windowsPid, exe,
    traceFile, traceIdentity, traceGeneration,
    plannedRecords, tailReserve,
    traceCli: absoluteNativeCollector, // build/telemetry/cs2trace
    outputDirectory,                 // dedicated, existing private 0700 directory
    inputAdmissionId, telemetryAdmissionId,
    inputExpiryMs: 250,
    scheduleToleranceMs: 5,
    cleanupTimeoutMs: 1000,
  },
});
// Pass this real adapter as `timed` when constructing createLifecycle(...).
```

These values must come from the currently admitted runtime/session, not example
PIDs or guessed paths. `controllerToken` is a capability: do not log it or put it
in a command-line argument. The bridge sends it only through child stdin. Native
trace identity is the decimal `DEV:INODE` from actual `inspect`; admit an unused
positive uint32 `traceGeneration`. The bridge consumes a fresh generation for
**every attempted call**, including failures, so stationary and route use distinct
generations. It never resets a live trace or wraps/reuses a generation. An existing
native generation ledger may still reject an already-used starting generation.

`run` receives the normal lifecycle fields, including `signal`, parent monotonic
`deadline`, fixed `durationMs`, exact process/window identity, scene/settings keys,
and null or explicitly preadmitted route. Immutable admission wins over run
arguments. All adapters for a new game process must use its new private input
session/token/provider and newly admitted trace; never rebind an old manifest.

The Python worker admits an idle protocol-v2 trace and enough remaining lifetime
record capacity before arming. A neutral ARM must get a fresh matching poll before
any active state. A route is capped at 64 non-overlapping steps / 60 seconds;
DS3 actions use only left-stick axes and bounded 1..500ms holds, at most the
admitted active input expiry. The first ARM is prepared before the interval;
subsequent neutral ARMs are scheduled ahead of each step. Each STATE uses only
the remaining fixed hold budget. Sequence persistence is followed by a lease recomputation; an opt-in fence
checks the deadline immediately before atomic replacement. Actual replacement
lower/upper bounds are retained and checked against the admitted tolerance.
Publication and receipt latency consume the budget. A late step, expired ARM, late ACK, missed end deadline or cancellation
fails rather than extending a hold, replaying a step or renewing active input.
The separately admitted scheduling tolerance is 0..20ms (default 5ms); actual
publication offsets and sequences are returned for review. Short pulses are
expected to fail when filesystem/poll latency exceeds their budget.

Stationary and route timing executes inside one local Python process. Local
collector subprocesses supply actual native protocol operations; there are no
agent/model/tool round trips, screenshots or AX calls inside the interval. These
collector subprocesses add real instrumentation overhead, which still needs its
separate admitted overhead comparison. Decode runs after neutral/trace stop. The raw trace envelope begins inside
`arm` and ends inside `stop`; it includes setup and neutral-cleanup tails outside
the fixed schedule. `traceEnvelopeMonotonicBounds` preserves arm/stop invocation
and receipt bounds separately from schedule start/end. These CPU clock domains
must not be merged with producer timestamp epochs without an admitted mapping.
`fixedWindowMetricsStatus:'inconclusive-unisolated-trace-window'` prohibits treating
the whole NDJSON as an exactly isolated stationary/route performance interval.
No native marker/filter was fabricated; exact window attribution remains an
observer/protocol integration requirement.

## Stop, failure and clock evidence

Every bound worker attempts one final neutral RELEASE and exact-generation trace
stop in `finally`, including SIGTERM/SIGINT cancellation. The shared cleanup budget
is at most 1000ms; neutral waiting reserves time for trace stop. A fresh RELEASE
receipt and native zero-writer/quiescence response are reported separately. Stop
verifies admitted-writer quiescence, not completion of all asynchronous callbacks.
Reader expiry remains necessary if the worker is killed or cannot publish.
Consumer leases are relative to the producer's poll, not an absolute host stop
time. Host publication bounds and fresh receipts do not establish the exact
instant of game consumption or a hard real-time neutral deadline. Late delivery
invalidates the run and triggers neutral cleanup; it never grants a fresh hold.
The Node bridge sends TERM then KILL after 1500ms on cancellation/deadline, bounds
stdout+stderr to 128KiB, checks the protocol and waits for successful worker exit.
A dead/uncooperative child leaves cleanup UNKNOWN. An ordinary worker failure can
return sanitized neutral/stop evidence in `error.workerResult`; explicit neutral,
stopped and quiescent receipts establish cleanup only, never a successful capture.

Python monotonic timestamps are **not** relabelled as parent `performance.now()`.
The bridge anchors the child's ready timestamp between parent before-spawn and
ready-receipt times, returning honest start/end lower/upper bounds, the offset
range and the independently measured child duration. `lifecycle.mjs` validates
that mapping under `clock:'anchored-child-monotonic'`. The mapping width is real
startup/IPC uncertainty; do not use an invented midpoint as an exact timestamp.

The native decoder's actual generation/identity summary must be structurally
complete with stable counters, nonzero events, no loss/uncommitted reservations
or boundary rejections. Overflow, no records and failed stop cannot become valid
captures. The lifecycle additionally requires fresh neutral and actual end-scene
verification before accepting the reference.

Without the explicit stationary admission below, this executor reports
`displayedMetricsStatus:'inconclusive'`; it does not claim to exclude every
scene transition during the interval. It does not replace that observer with
caller booleans. Valid raw metadata and successful controller receipts do not
establish displayed FPS, movement, safety, route return or performance gains.
Use actual images and the separately admitted observer/analyzer for those claims.

## Data ownership and bounds

The test integrator owns the dedicated output directory and its `capture-*.ndjson`
derivatives. Each decoded output and the aggregate derivatives in that directory
are capped at 12MiB; at most 64 directory entries are admitted. A private
`.timed-output.lock` serializes the byte-cap check and exclusive publication.
Unknown files are not removed; unsafe owned-name entries fail collection. Existing
raw trace files retain their telemetry owner and 8,392,704-byte per-file cap.
The derivative allowance plus eight raw producer files remains below the existing
80MiB experiment cap; use one output directory for that experiment. Other
experiment-owned derivatives must share that overall budget. There is no new
collector that survives return and no automatic deletion/reset/VACUUM.

Retain raw captures/derivatives for the telemetry contract's 7-day TTL; source-bound
summaries/minimal failure evidence retain its 30-day / 4MiB allowance. Active
mappings, pending analysis and unresolved recovery/acceptance investigations are
holds. The owner inventories and explicitly retires exact owned files only after
preservation checks. A cap failure preserves existing evidence and refuses growth.

## Offline checks

```sh
node --test tools/gameplay/session.test.mjs tools/gameplay/lifecycle.test.mjs tools/gameplay/timed-executor.test.mjs
python3 -m unittest discover -s tools/gameplay -p 'test_virtual_controller.py' -v
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tools/gameplay -p 'test_timed_executor.py' -v
```

The executor fixtures use the actual built native collector and offline binary
trace files, plus fake pollers. They exercise stationary/route, exact identity,
late ACK, cancellation, failed neutral, native overflow/empty capture, quiescence
failure and derivative bounds. One test executes the actual Node → Python →
native collector chain against independent file pollers; another sends TERM to
the real Python worker and verifies its fresh neutral/stop receipts. Bridge
fixtures separately exercise dead children, bad protocol, output limits, exhausted
generations and TERM/KILL escalation. None launches a game or proves live provider,
GUI, scene or displayed-frame behavior.

The successful finite-route schedule contract and post-STATE cancellation case
explicitly simulate only the local publication/scheduling monotonic clock. They
still publish/read actual packet files and run the real native ARM/STOP collector.
This isolates ordering, fixed offsets, bounded leases and active-input cleanup
from an accidental OS/fsync 5ms scheduling SLA. Separate late-publication,
late-ACK and sequence-persistence fence tests retain actual delayed failures;
the real Node/Python/native stationary chain retains wall-clock execution.
An additional cancellation test gates on the real trace ARM then cancels at the
first route wait/check. Fixture passes do not admit live scheduler timing.

## Optional exact stationary presentation window

An independently admitted `presentationWindow` may be supplied in immutable
`createTimedExecutor` admission. Run arguments cannot introduce or replace it;
the Node bridge removes it for a moving route. Its closed shape is:

```js
presentationWindow: {
  collectorSha256, measurementId, // actual executed collector hash; fresh UUID
  swapchain, width, height, revision, // observed current drawable/configuration
}
```

The worker verifies the actual collector bytes before input and around both
`clock-sample` executions. After trace arm it records 250 ms of pre-roll, samples
the public native CoreAnimation/Mach clock, runs the fixed stationary interval,
records 250 ms of post-roll and samples again. The same bounded local command
runner/deadline applies; no CUA/image/model calls occur inside this envelope.
Invocation bounds in the worker monotonic domain remain separate. The exact CA
window starts at the first sample's upper bound and has the requested duration;
it is not claimed to coincide exactly with Python schedule edges.

Only after neutral, native writer quiescence and decode does the maintained
`fixed_window_analysis.analyze_fixed_window` join actual 71/72/73 feedback,
configuration, consecutive boundary frames, native loss counters and paired
clock samples. Rejection preserves decoded evidence and reports no valid window.
Success reports `accepted-native-presentation-window` and the complete native
window result. This measures drawable feedback, not physical scanout, motion
route alignment, full display latency or disabled-instrumentation overhead.
Ordinary CPU trace captures retain their original inconclusive display status.

Two bounded clock envelopes and the analysis are retained in the existing
experiment result, not a new daemon/log. Charge them to the existing 80 MiB /
7-day experiment cap and 4 MiB / 30-day summary allowance; raw sample JSON is
limited to 8 KiB per sample. Active mappings, unresolved measurements and recovery
holds remain excluded from retirement. No new cleanup or extra aggregate quota
is installed. Tests exercise real native clock/arm/stop with synthetic trace
content and prove missing presentation evidence cannot claim success; actual
game/display measurements remain live acceptance.

The worker also saves a mode-0600 `capture-*.presentation.json` next to the
immutable decoded NDJSON. This at-most-32-KiB sidecar preserves exact Python
integer clock samples, admission and trace identity even if a JavaScript reader
would round a uint64 on a long-uptime host. Both files count against the same
12-MiB derivative cap and existing experiment retention/holds. Failed Node
results project only known bounded clock/window fields plus this evidence path;
unknown worker payload fields and controller capabilities are never exposed.
The sidecar remains the exact reproduction source.
