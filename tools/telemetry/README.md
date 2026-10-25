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

## Opt-in compiler/wait coverage profile

Set `DXMT_CS2_TRACE_PROFILE=compile-waits` **before starting the producer** for
a short discriminating compiler experiment. The selection is parsed once and
fixed for that producer's lifetime. An empty/unset value preserves the existing
full stream and emits no new profile marker. Every other nonempty value,
except the supported `presentation-waits` profile described below, including
`full`, case changes or surrounding whitespace, fails closed: ordinary
records are suppressed and event 95 reports `invalid-fail-closed`. Restart through
the normal producer lifecycle to change profiles; rearming does not change it.

`compile-waits` keeps only types **3, 4, 5, 17, 19, 80–89, 93–94 and 104–107**. These are
pipeline readiness waits, shader compilation/cache-hit indication, PSO creation,
archive lookup/serialization, detailed scheduling/dependencies/readiness,
persistence admission/locking/write timing and checked cache get/init. Types
80–89/93–94/104–107 still require an explicit valid `DXMT_CS2_TRACE_DETAIL_LIMIT` and share
its existing cap; excluded events do not consume that budget. Existing 100 us
duration censoring on 3/5/19 remains. Cache get timing begins after acquiring its
reader mutex. Additive event 104 separately measures acquisition of that mutex;
event 93 keeps its existing checked SQLite status, call-only timing, and
post-lock generation admission. A capture change during acquisition can leave
the checked get standalone; a stale wait is never relabeled into a later capture.

Optional compiler stages use the existing event layout and `cpu_steady_ns`
intervals: **104** cache reader mutex acquisition (object=cache birth serial,
p0=parent work serial or zero when unavailable/noncompiler, p1=read operation
serial shared with event 93; flags=1 acquired or 3 lock exception), **105** AIR
conversion, **106** Metal `newLibrary`, and **107** Metal `newFunction`. Stages
105–107 use object=nonzero parent work serial, p0=nonzero fresh operation serial,
p1=route (0 cold, 1 cache; 105 permits only 0), and flags=1 success, 2 returned
failure, or 3 exception. A successful reader acquisition without event 93 can
mean a later exception or missing boundary evidence; it is not a cache miss.

`analyze.py` exposes `compiler_stage_attribution`. Exact cache/read identities
join 104 to 93; task joins additionally require a valid observed run, matching
thread, and contained chronology. Missing stages and missing joins stay unknown.
Cache failures may fall back to cold conversion within the same work item.
Stage context from a noncurrent generation is rejected before new timestamps or
operation serials, including before profile metadata work. A race after that
check still passes through the existing fail-closed publication gate. Budget
exhaustion bounds records; later armed attempts may still read clocks/allocate
operation serials before publication is refused.
These CPU wall intervals include preemption/blocking and do not measure CPU busy
cost, backend queue residence, a summed critical path, or FPS. Legacy captures
without 104–107 report unknown compiler-stage coverage.

The profile excludes every other kind: **1/2** legacy CPU Present cadence,
**6–16** frame/encode/drawable scopes, **20–25** chunk/GPU/frame joins,
**30–33** other synchronization/reason probes, **40–49** API/teardown scopes
(including the high-rate Map event 42), **61/62** configuration,
**70–78** native presentation feedback and **90–92** completion/finish probes.
Unknown/future kinds are excluded. This is compiler attribution coverage; it
cannot supply displayed/gameplay FPS or all sources of waiting. Retain an
independent frame observer and explicit phase labels when comparing hitches.

One event **95** identifies each observed generation of a nondefault producer,
even when its first event attempts are filtered. It uses the existing v2 event
layout and real writer admission/quiescence protocol. Object=1 is the profile
metadata schema; flags=1 means `compile-waits`, flags=2 means invalid/fail-closed,
flags=3 means `presentation-waits`;
p0 is the configured detailed record limit, p1 is the lifetime capacity 131072.
Start=end is a CPU steady-clock instant; it is not a duration or performance
metric. This single marker is outside the detailed record budget and consumes
one ordinary lifetime slot per generation. The decoder names it explicitly;
invalid, malformed, duplicate or coverage-contaminated metadata returns decode
status 3 and makes profile acceptance fail. Historical generations are excluded
before validating the current generation. A valid metadata-only capture supplies
no selected-event evidence and is marked unusable by analysis. A stream without
95 remains `unspecified-full-or-legacy`, never a claim that full coverage was
independently verified.

The profile reduces event volume from excluded probes; it does **not** reset the
append-only file, increase 131072 slots, guarantee 30–60 seconds (or ten minutes),
remove all instrumentation overhead, or establish a performance improvement.
Keep capture admission/tail reserves and loss checks. Events 3/4/5/17/19 and the
profile marker are outside the detailed cap and still consume lifetime capacity.
Use the existing private-session owner, byte caps, TTL and active/recovery holds
below; this extension starts no additional producer or raw-data store.

Focused offline regression:

```sh
python3 tools/telemetry/test_trace_profile.py
python3 tools/telemetry/test_compiler_attribution.py
```

It compiles the exact production producer header with the existing portable
Windows mapping seam, checks actual `record`/`detailRecord` and scope emitters,
and exercises the identity-bound native binary decoder. It launches no game.

## Opt-in presentation/wait coverage profile

`DXMT_CS2_TRACE_PROFILE=presentation-waits` is explicit process-lifetime opt-in;
metadata95 flags=3 uses the existing schema/version2. Invalid=2 stays reserved.
The full/default and compile-waits meanings are unchanged. This extends the
existing producer/controller/analyzer rather than introducing another collector,
format, daemon, scheduler or native bridge. Runtime selection and the installed
launcher are unchanged. See [the short capture recipe](PRESENTATION_WAITS.md).

The exact allowlist is **1/2, 6–16, 20–25, 33, 40, 44–49, 61/62, 70–78,
89–92 and 96–99**. Event95 is emitted separately once per admitted generation.
It excludes high-rate draw/Map/GetData/Flush API streams (41–43), compiler,
archive/cache/task probes (3–5/17/19/80–88/93–94), and unknown future kinds.
90–92 still require `DXMT_CS2_TRACE_DETAIL_LIMIT` and share its unchanged
32768-record maximum. Excluded probes do not spend that budget. 89 retains cap
and stale-work failure semantics; profile-only asynchronous presentation scopes
also emit a stale-work89 on an active-generation mismatch, outside the optional
detail budget so disabling optional finish probes cannot conceal stale work.
All of these records still spend the fixed 131072 lifetime slots.

| Type | Profile-only meaning and identity payload |
|---|---|
| 96 | Uncensored queue frame-latency fence wait. Object queue birth serial; p0 presented **queue** frame; p1 awaited fence frame (`p0+1-flags`); flags configured max latency. Queue frame0 is valid. |
| 97 | Uncensored swapchain admission wait, only when that fence is actually waited. Object swapchain birth serial; p0 local presentation count; p1 awaited count (`p0-flags`); flags configured latency. This local counter is never assumed equal to a queue or native frame. |
| 98 | Uncensored drawable acquisition CPU interval. Object swapchain birth serial; p0 native presenter frame; p1 immutable configuration revision; flags zero. Captured zero generation stays unarmed; stale generation cannot be upgraded. |
| 99 | Instant explicit frame binding. Object swapchain birth serial; p0 native presenter frame; p1 queue frame; flags zero. Unique same-receipt25 supplies queue birth serial. Native and queue counter values can differ. |

96–98 start/end are CPU steady nanoseconds; 99 is a CPU steady instant and its
decoded duration is null. 20–23 remain CPU-steady queue publication/encode/
commit/completion observations; 24 remains native GPU nanoseconds. Native71
contains separately tagged CoreAnimation host seconds in p1, with 72 revision
and 73 drawable dimensions. No epoch subtraction is allowed. Legacy1/2 and6–16
retain pointers/local counters and 100 us censoring; they cannot supply the
new exact joins or prove missing costs zero.

`presentation_capture` independently checks coverage, stop/loss evidence,
profile3, exact unique25/99 counter bindings, unique queue/chunk completion,
96/98 identities and revision-consistent strict71/72/73 feedback. It requires
at least two consecutive native frames for cadence, a supported capability70,
and every captured native/binding/drawable/queue-wait identity to be joined.
Malformed, duplicate, stale, wrong-profile, missing-identity, unsuccessful GPU,
loss or boundary records prevent complete capture acceptance. Partial rows and
raw events remain visible; neither a profile-valid decoder result nor a
nonempty cadence array certifies the capture. 97 and native CLOSE are optional
and their absence remains unknown. Native failures, observed counter loss and
incomplete observed CLOSE accounting fail acceptance. Optional finish90–92 must
join exactly and never prove consumer wake latency.

Use `analyze.py INPUT --output OUTPUT --expected-profile presentation-waits` for
an acceptance command: it writes the report and exits3 for the wrong profile or
incomplete presentation capture. Without `--expected-profile`, legacy CLI exit
behavior is preserved. Profile3 sets `producer_profile.capture_usable=false`
when presentation coverage fails; `policy_valid` continues to describe metadata
and allowlist validity separately. This is offline trace acceptance, not proof
of loaded binaries, actual gameplay, display latency, saturation or FPS gain.

For this presentation experiment, the existing diagnostic/session owner owns
one producer-lifetime mapping and private decoded derivatives. State lifetime
is the producer lifetime with its fixed cap; raw/decoded data have **7-day TTL /
64 MiB combined cap** per experiment (a stricter subset of the general contract
below). Results/minimal failure evidence retain the existing30-day /4 MiB cap.
Active producer mappings, active analysis and unresolved acceptance/recovery
holds are excluded from retirement and need reviewed expiry; admit no further
bulk capture when this would exceed the cap. Cleanup is manual through the
existing session lifecycle after bounded exact-path inventory and preservation
verification; no cleaner or extra collection is installed. No private raw data
is published. Do not reset/rearm to recycle slots or inflate either quota.

```sh
python3 tools/telemetry/test_presentation_profile.py
```

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

## W1 bounded detailed probes (default off)

`DXMT_CS2_TRACE_DETAIL_LIMIT=N` opts a compatible producer into detailed probes,
only while its v2 capture is armed. Empty, zero, malformed or values above 32768
disable them. `N` is a **per-producer, per-generation source cap shared by all
types 80–94 and 104–107**: at most N budgeted detailed records plus one event 89 cap-failure
marker. Stale-generation failure markers consume that N budget. All other
existing events still consume the fixed process-lifetime
131072-slot file. This cap is a count budget, not a promised capture duration or
record rate. Reserve the extra marker and existing events in capture admission;
calibrate N outside the timed experiment. No queue, timer, new file, background
sampler, paths, shader bytecode or user payload is added by these probes.

| Type | Meaning and payload |
|---|---|
| 80 / 81 | Task enqueue / run attempt start. Object: immutable task birth serial; p0: scheduler birth serial; p1: sampled runnable depth; flags: worker count. Samples occur under scheduler admission lock. |
| 82 / 83 | Task blocks on / is released by a dependency. Object: task birth serial; p0: dependency task birth serial; p1: scheduler serial; flags zero. A dependency can be observed before its own submission. Missing dependency events stay unknown. |
| 84 | Ready publication edge before SetDone lets consumers retire the task. Object task; p0 scheduler; other payload zero. |
| 85 | Optional persistence admission. Object task; p0 scheduler; p1 charged bytes; flags 0 accepted / 1 rejected. |
| 86 | Optional persistence callable CPU interval. Same identities/bytes; flags 0 returned / 1 C++ exception. A normal return does not establish a successful SQL write. |
| 87 / 88 | Cache writer lock wait / checked native call CPU interval. Object task; p0 scheduler; p1 zero. 87 flags zero; 88 flags raw SQLite status. |
| 89 | Detailed capture failure, object zero. Flags 1: cap exhausted, p0 configured cap; flags 2: stale generation callback, p0 callback generation. These invalidate detailed capture acceptance. |
| 90 | Query readback destruction interval before CPU coherence. Object queue birth serial; p0 chunk sequence; p1 frame; flags zero. |
| 91 / 92 | Chunk reset complete / post-signal finish observation. Same queue/sequence/frame, zero flags. 91 precedes CPU coherence signalling; 92 follows signalling and any enabled idle-retirement sweep. Their interval includes that work and is not pure coherence-signal cost or consumer wake latency. |
| 93 / 94 | Checked cache get / init CPU interval. Object cache birth serial; p0 operation birth serial; p1 zero; flags raw SQLite status. |

All start/end fields in 80–94 use the existing producer CPU steady clock. No
absolute GPU/native presentation timestamps are subtracted. SQLite result codes
retain their full raw value and primary code: ROW=100 (read hit), DONE=101 (read
miss or completed write), BUSY=5, LOCKED=6, CORRUPT=11, NOTADB=26. Reserved flags
0xffffffff mean native result unavailable; 0xfffffffe mean C++ exception. Even a
completed write is not a durability or performance claim. IDs are birth serials,
never raw task pointers. Generation admission and the shared record cap apply to
checked cache probes too. Crossing capture boundaries stays incomplete; stale
tokens cannot silently produce an identity join in a new capture.

`detailed_probes.capture_usable` fails on malformed or ambiguous detailed joins,
source failure markers, decoder overflow/uncommitted evidence
or changing counters. Missing task boundaries and dependency observations retain
unknown values. Readback/coherence timing requires unique 90/91/92 and a unique
accepted 20–23 chunk join with matching frame. Existing capture quality limits
and independent presented-frame admission remain required separately.
The decoder's legacy `wrong_generation` counter counts filtered historical
records in the append-only lifetime file. It is reported separately as
`historical_generation_records_excluded` and does not invalidate a new capture.
A current decoded event with mismatched generation is rejected structurally;
event 89 stale asynchronous work still fails detailed capture acceptance.

Producer/retention contract, before activation: the performance integrator owns
these metadata-only records in the existing private `cs2trace new-session`
directory. Each producer file remains bounded to 8,392,704 bytes (4096 + 131072
× 64); admit at most eight producer files / 67,141,632 bytes per experiment.
Raw captures and decoded derivatives have a 7-day TTL and 80 MiB aggregate cap
per experiment; retain a source-bound summary and minimal failure evidence for
30 days / 4 MiB. No automatic cleanup is installed. The experiment owner must
perform bounded inventory and explicit removal through the existing session
lifecycle after verifying summaries/evidence. Active mappings, pending decoder
work, unresolved acceptance/recovery investigations and explicitly held captures
are excluded; holds require a reviewed expiry and prevent starting more bulk
collection when the cap would be exceeded. This declaration authorizes no
deletion of unrelated or active files. Detailed probes remain unactivated here.

## Exact stationary windows in native presentation time

`cs2trace clock-sample` emits one bounded JSON observation and exits. It uses the
public [CACurrentMediaTime API](https://developer.apple.com/documentation/quartzcore/cacurrentmediatime())
twice, bracketing a `mach_absolute_time` read. These APIs share the documented
Mach absolute clock. Tick conversion uses the reported Mach timebase and
128-bit intermediate arithmetic. The sample retains raw ticks, converted
nanoseconds, clock/API names, UUID, PID, timebase and the CA bracket width.
Brackets above 100 ms or invalid native clocks fail; analysis defaults to a
stricter 1 ms bracket limit. The collector links QuartzCore (see `check.py`).
It creates no trace mapping, output file, timer or background observer.

The caller must hash the **actual executable it invoked** and bind each returned
sample to the same fresh measurement UUID in a receipt envelope:

```json
{
  "measurement_id": "12345678-1234-4234-9234-123456789abc",
  "collector_sha256": "ACTUAL_64_CHARACTER_LOWERCASE_SHA256",
  "clock_sample": {"kind": "clock_sample", "schema": 1, "...": "native output"}
}
```

The envelope is an execution receipt owned by the caller. A `valid` or `verified`
boolean is not accepted as execution evidence. The analyzer checks the expected
hash, measurement/sample identities, public clock domains/APIs, exact raw tick
conversion, finite bounded times, bracket widths, timebase consistency and clock
progress. It does not establish the executable's identity from JSON alone or
claim a kernel boot identity.

```python
from fixed_window_analysis import analyze_fixed_window
result = analyze_fixed_window(
    decoded_rows, begin_envelope, end_envelope,
    window_seconds=30, expected_swapchain=SWAPCHAIN_BIRTH_SERIAL,
    expected_dimensions={"width": 1280, "height": 720}, expected_revision=REVISION,
    expected_collector_sha256=ACTUAL_COLLECTOR_SHA256,
    measurement_id=MEASUREMENT_UUID, scenario="stationary")
```

The exact half-open CA window is `[begin.ca_upper_seconds,
begin.ca_upper_seconds + window_seconds)`. The independently sampled end clock
must reach its end and be no more than two seconds late by default. Sample the
beginning after trace admission, retain a warm stationary scene, and collect a
small tail after the window so native feedback and counters can span its end.
Sampling overhead and caller/route timing are separate: the native window is
not relabelled as the executor's Python monotonic interval. Moving routes are
rejected until their exact alignment is separately bounded. This helper does
not provide an observer for instrumentation-disabled runs or verify overhead.

Acceptance requires unique valid 71/72/73 triplets and consecutive frame
identities spanning both boundaries, the requested drawable geometry/revision
throughout that spanning interval, supported native presentation capability,
complete native counter snapshots bracketing feedback **receipt timestamps in
the same producer CPU domain**, and no native/PE loss. Missing loss-summary
fields are unknown and rejected. Counter accounting must be internally
consistent, and the terminal completed-callback count must cover the observed
feedback span. Malformed selected native control records and
invalid/unlocalizable feedback cannot be discarded to manufacture an accepted
subset. Known valid configuration changes wholly outside the spanning interval
are reported as excluded tails. Historical generation records retain their
separate exclusion count.

The report separates presentations counted inside the exact window divided by
its duration from distributions of intervals wholly inside it. Intervals crossing
either boundary are excluded and counted; zero presentations with consecutive
feedback spanning the entire window is retained as an observed stall. These are
drawable presentation observations, not physical scanout, full display/frame
latency, or proof of an FPS improvement. No CPU Present rate substitutes for them.

The same analysis is available as a bounded offline CLI (exit 0 accepted, 3
ineligible, 2 malformed input):

```text
python3 tools/telemetry/fixed_window_analysis.py TRACE.ndjson BEGIN.json END.json \
  --output WINDOW.json --window-seconds 30 --swapchain SERIAL \
  --width 1280 --height 720 --revision REVISION \
  --collector-sha256 HASH --measurement-id UUID --scenario stationary
```

Decoded input is capped at 64 MiB and the existing event-count/line limits;
clock envelopes are capped at 8 KiB each. Clock receipts and window reports are
metadata under the existing experiment owner, 7-day raw/80-MiB aggregate and
30-day summary/4-MiB policy above, including active/hold exclusions and manual
cleanup. A window requires two single-shot samples, no continuing producer.
The 131072-slot trace lifetime cap still applies: calibrate event rate before
choosing duration. A long requested window is rejected if its source overflows;
this analyzer does not expand/reset the ring or silently shorten measurement.
