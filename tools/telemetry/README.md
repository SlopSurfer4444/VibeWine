# Active telemetry tools

This is the maintained collector/analyzer, derived from the historical
`renderer/wave7/consumer` snapshot. The snapshot remains unchanged. The C++
collector uses the protocol from the exact `components/dxmt` gitlink, not a
second copy of the header. These tools neither install a runtime nor start a game.

## Offline verification

On macOS with Xcode Command Line Tools:

```sh
git submodule update --init components/dxmt
python3 tools/telemetry/check.py
```

The command builds `build/telemetry/cs2trace` and runs synthetic binary and Python
contract tests. Passing these checks does not demonstrate gameplay performance,
actual displayed frames, or that the installed runtime emits these events.

## Capture admission

Use a private session created by `cs2trace new-session ABS_ROOT` with a compatible
producer. A new session alone does not start or bind a producer. Inspect the
producer's trace file to obtain its device/inode identity and remaining capacity.

```text
cs2trace inspect ABS_FILE
cs2trace arm ABS_FILE GENERATION DEV:INODE PLANNED_RECORDS TAIL_RESERVE
cs2trace stop ABS_FILE GENERATION DEV:INODE
cs2trace decode ABS_FILE GENERATION DEV:INODE
```

`arm` requires an explicit positive record budget and nonnegative reserve. Both
are decimal integers. Admission rejects insufficient remaining lifetime capacity
before consuming a generation. The 131072 slots are **not reset between captures**.
The budget is a preflight check, not an automatic stop or a guarantee that a
particular number of seconds will fit. Calibrate event rate, leave room for tail
events, bound the recording, and check loss counters afterwards. If capacity is
exhausted, use a fresh producer/session through its normal lifecycle; never reset
counters under a live producer. `stop` verifies admitted-writer quiescence, not
that all asynchronous native callbacks have completed.

## Interpretation

Strict native-presented cadence requires one valid event each of types 71, 72
and 73 for the same frame identity and callback receipt. Invalid or duplicate
companions cannot be hidden by discarding them before grouping. Only consecutive
identities with consistent configuration and drawable dimensions contribute an
interval; resolution transitions do not establish steady-state performance.
The analyzer accepts optional `--expected-width`, `--expected-height` and
`--expected-revision` filters. Without the requested configuration supplied,
valid observed cadence does not verify the intended game configuration. Check
`quality_limits` and `capture_identity_quality_ok`; valid local intervals do not
certify the entire capture. Raw records remain available for diagnosis. Missing records are not proof of
GPU-dropped frames, and CPU Present-call cadence is not displayed FPS.

Archive lookup (17) and serialization (19) durations use CPU steady nanoseconds.
Event 18 remains unknown. Serialization has a producer-side duration threshold,
so the absence of event 19 does not prove zero serialization work. Native GPU,
CPU steady and native presentation clocks are distinct; do not subtract their
absolute timestamps as if they shared an epoch.

## Next live experiment: observer overhead and FPS

Keep the game build, graphics, render resolution, scene, route, power state and
background load fixed. Compare instrumentation disabled, enabled but unarmed,
and armed, with the **same independent frame observer in all three conditions**.
The instrumented trace cannot measure its own disabled baseline. Warm up first;
record cold-cache results separately. Alternate conditions across repeated runs
and retain run order, thermal state and raw output rather than selecting the best
run. Decode, screenshots and analysis happen outside the timed section.

Report frame-time distributions and long-frame counts, not just average FPS.
Reject or explicitly mark incomplete, overflowed or configuration-changing
captures. Separately measure CPU/GPU activity and cache events to form hypotheses;
none alone proves a bottleneck or an FPS improvement. A controlled before/after
in-game comparison and the overhead baseline are still required. No such live
result is claimed by this directory's offline tests.
