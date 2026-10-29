# One bounded scene-triggered trace

`scene_capture.py` is a maintained source adapter for existing `cs2trace` and
`session_phases.py` markers. It sends no input, starts no game, changes no
launcher/environment and creates no daemon. Adopt these source files through
the existing diagnostic package route; private predecessor wrappers are
references, not production dependencies.

The29October recording exhausted its useful detail budget in an early unknown
scene after a fixed60-second delay. This adapter waits for a fresh observed
`gameplay` boundary after launch binding, then attempts one arm of a fresh
producer. Gameplay means the named observed scene, not combat or proven route
completion. A marker is supplied evidence; an action ACK does not prove progress.
Menu/loading/unknown/exit, wrong birth, reordered boundaries, future/expired or
pre-launch markers cannot trigger. Identity and latest marker/age are rechecked
after the potentially slow open-file observation, immediately before arm.

## Adoption and human route

Keep the existing independent whole-session observer ready before the ordinary
authorized launch. For human play preserve the existing `measure-combined-trace`
route and physical gamepad. Do not substitute the agent test XInput route or
`VIBEWINE_XINPUT_TEST_*` environment. This adapter never enables, reads or writes
a virtual controller. Root owns launch policy, installed/loaded module proof and
physical-controller acceptance.

Replace the old fixed-delay operator dispatch with this source adapter; never
run both. Keep launch admission, manifest/build and loaded-module proofs. Launch
the producer unarmed with the exact process-lifetime profile/detail limit.
`combined-bounded` supplies the bounded detailed window. A separate ordinary
fresh launch can use existing `stall-waits` for sparse long waits in a later
scene. A profile change or exhausted lifetime cap requires a normal fresh
producer lifecycle. Never reset/rearm the original or repeat an uncertain start.

After the harness binds native PID/birth and trace path, root creates one new
private config. Fill bindings from that exact launch's verified receipts.
`notBeforeWall` is its launch/bound wall timestamp. Trace filenames contain the
**Windows** PID, which need not equal native macOS PID. The adapter checks that
this native birth holds the file through lsof; that observation does not
distinguish mmap from open fd. Protocol/freshness checks admit the file separately.

```json
{
  "schema": 1,
  "sessionDirectory": "/absolute/private/existing-monitor-session",
  "game": {"pid": 123, "birth": "1793237514:328431"},
  "notBeforeWall": 1793237514.4,
  "traceFile": "/absolute/private/dxmt-trace-session-0123456789abcdef0123456789abcdef/events-32.bin",
  "traceIdentity": "DEVICE:INODE",
  "controller": {"path": "/absolute/adopted/cs2trace", "sha256": "EXACT_64_HEX"},
  "launch": {
    "traceProfileRequested": "combined-bounded",
    "traceDetailRecordLimit": 12000,
    "rendererSha256": "EXACT_64_HEX"
  },
  "sourceBinding": {
    "hubRevision": "EXACT_40_HEX", "wineRevision": "EXACT_40_HEX",
    "dxmtRevision": "EXACT_40_HEX", "buildReceiptSha256": "EXACT_64_HEX",
    "installedManifestSha256": "EXACT_64_HEX", "rendererSha256": "EXACT_64_HEX",
    "loadedModules": {"exact loaded module path": "EXACT_64_HEX"}
  },
  "policy": {
    "profile": "combined-bounded", "detailLimit": 12000,
    "waitSeconds": 600, "windowSeconds": 20, "pollMilliseconds": 250,
    "markerMaxAgeSeconds": 15, "plannedRecords": 96000,
    "tailReserve": 8192, "stopReserved": 90000
  }
}
```

Source binding is retained in the ready receipt; declarations cannot replace
root's independent loaded/installed verification. Pin adopted `scene_capture.py`
and `session_phases.py` hashes in the existing package binding. The actual
controller executable is rehashed before every invocation. Run the foreground
one-shot operator in the existing owned diagnostic lifecycle:

```sh
python3 /absolute/adopted/scene_capture.py /absolute/private/new-scene-config.json
```

It writes `capture-ready.json` before waiting. After actually observing world
load and the named scene, append the current boundary with the existing CLI.
The monitor's status.json must bind the same game. Example:

```sh
python3 /absolute/adopted/session_phases.py mark \
  --session /absolute/private/existing-monitor-session \
  --pid 123 --birth 1793237514:328431 \
  --phase gameplay --provenance controller-observation \
  --evidence 'Observed Cemetery bonfire, stationary camera; native observation receipt REF'
```

Mark loading/menu/unknown when observed, including unobserved transitions. A
changed marker stops capture. Poll and arm/stop command latency can include a
tail after a transition; leave it unclassified rather than claiming exact scene
alignment. No observations inside controlled timing windows are authorized by
this adapter. Stationary comparisons still use maintained fixed-window gates.

## Bounds, closure and evidence

Maximum marker wait900seconds, capture600seconds, polling50..1000ms. Planned
record admission plus reserve must fit unchanged131072 lifetime slots. Sampled
stop threshold is at most the planned budget. It is **not** a hard quota or a
duration guarantee: a burst during polling/stop may consume reserve. Detail
records share the existing0..32768 cap; its exhaustion may leave later families
absent while ordinary records continue. Long-wait filtering never resets that
cap. Use a short first scene window and measured event volume; the example
20seconds is not a promise that20seconds fit.

Stop triggers: duration, sampled reservation limit, birth end/change or marker
change. Loss/binding/other errors attempt one stop if arm might have occurred.
An uncertain arm response is never retried. An uncertain stop remains
`armed=unknown`, without proof/rearm. Command timeout kills only this operator's
controller/lsof child, never game/Steam/Wine server. Root closes task-owned
observers through their existing owners and reconciles unknown obligations.
Graceful SIGTERM/keyboard interruption also attempts that one stop; forced
process death cannot provide a closure receipt and remains an owner obligation.

`capture-stop.json` requires generation1, zero admitted writers and structural
closure. `capture-proof.json` reads the original twice without a copy. It
requires protocol2 magic/capacity, CLOSED generation1, zero writers/drops,
fixed reserved count <= capacity, every prefix commit=index+1 and generation1,
and no committed tail slots. Before arm, freshness requires generation0, zero
reserved/dropped/boundary counters and no committed slots.

Immutable SHA256 covers `[0,48) + [56,8392704)`, including reserved header and the
entire event area. Only supported protocol2 header `boundaryRejected` `[48,56)`
is excluded. It can rise after CLOSED as late same-generation writes are
rejected; regression is refused. Every other header/payload/commit mutation
invalidates proof. A separate digest covers the complete published prefix.
Whole-file SHA256 is diagnostic at that read, not the immutable reference key.
This repairs the old false `source changed during copy` gate without weakening
payload checks.

Before analysis call `verify_reference(receipt)` from the adopted source module,
then use the pinned controller's existing decode with original path, generation
and identity. Root also revalidates the admitted session/marker identities for
later consumption. The verifier rechecks identity, generation, slots and both
immutable digests; counter growth alone preserves the reference. Existing
profile/decode/native-loss/boundary analysis stays mandatory and unmodified.
Boundary counts may include earlier events: this proof cannot attribute their
time or waive existing loss gates. Empty output fails operator closure.

The task-session lock prevents a duplicate adapter in that session. Native
controller session-marker locking remains per command; the adapter does not
hold that lock across subprocesses. Root must own the exclusive lifecycle and
prohibit other controllers. Normal foreign rearm changes generation and
invalidates proof; the producer has no reset and the native ledger refuses
generation reuse. Two finite reads do not prove absence of malicious reset/ABA,
completion of asynchronous operations or live quiescence beyond the stop proof.

Before game/focus handoff root must inspect **actual freshly decoded records**
and profile/identity evidence from the stopped short scene window. Ready/armed/
header-progress ACKs alone are insufficient. On failure hold the original and
repair. A longer follow-up uses a fresh normal lifecycle; never reset this file.

Coverage is only observed records in the scene window; gaps/families/tails stay
unknown. An independent vendor observer can span the whole session; a20-second
trace does not establish that whole-session FPS. The29October421-interval vendor
run,20.156-second early trace and9.72-second valid native feedback are separate
scopes. A/A then A/B needs equal scene/settings/cache/power/background/observer/
route controls, ordinary exits/relaunches, module proof and the repeatability
analyzer. Measure overhead with the same independent observer in disabled,
enabled-unarmed, light and detailed conditions. This code labels it unmeasured;
no gain, cause, whole-frame percentile or full-session coverage is claimed.

## Retention and cleanup contract

Owner: existing diagnostic session owner; consumer: root integration/analysis.
Exact scope: configured existing monitor session, its one admitted trace-session
original and named capture-*.json receipts. Classes: private raw gameplay/process
telemetry, decoded derivatives, curated evidence. Reuse aggregate raw TTL7days/
64MiB per experiment, decoded12MiB, curated30days/4MiB; no additional quota.
Original is8392704bytes; no second binary copy or full NDJSON is created. At
most seven new receipts each<=64KiB, config and one empty lock fit inside the
existing curated4MiB cap. Markers reuse existing1MiB cap. Root checks aggregate
remaining budget before dispatch and bounds redirected stdout in session logs.

Cleanup stays manual with the existing owner: exact ownership/path/identity/
reference rechecks, coordination with writers, skip symlinks/changing objects,
preserve inputs before retiring them. Active capture, pending analysis, unique
unresolved evidence, recovery and explicit holds exclude retirement. These are
contract bounds, not a TTL cleaner. The adapter deletes nothing and touches no
unrelated session/data/service.

Offline checks (stdlib only, no compiler/controller/game):

```sh
python3 -m unittest discover -s tools/telemetry -p test_scene_capture.py -v
```
