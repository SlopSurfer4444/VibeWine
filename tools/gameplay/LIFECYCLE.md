# Finite lifecycle adapter

`lifecycle.mjs` implements an explicitly admitted workflow over existing CUA,
process reconciliation, process-local input and telemetry capabilities. It has
no executable launcher, image model, telemetry collector or game-specific key
script hidden inside it. CPU contract tests are not live game evidence.

The finite order is: reconcile; launch or explicitly adopt; bind the exact native
window; classify each menu/notice; load offline; observe a safe movement and its
response; fixed neutral warmup; stationary capture; separately admit and execute
a finite route; verify returned world/scene/settings and neutral; return through
the game's normal menu to title; choose visible desktop Quit; verify game process
absence; launch a fresh generation and verify title/menu. The relaunched game is
left running as an explicit obligation. A second complete cycle requires another
runner and explicit `adoptProcessKey` (or first closing that game normally).

## API and required live glue

Import `createLifecycle` from `lifecycle.mjs` and `ds3Policy` from
`profiles/ds3/lifecycle-policy.mjs`. Construct once, then await
`runner.run({signal: abortController.signal})`. A construction/configuration error
throws before any capability is called. A runtime failure returns
`{status:'failed', failedPhase, error, cleanup, uncertainOperation, events,
captures, identity, obligation}`. Success returns `status:'complete'`, two capture
receipts, the new identity and the remaining running-game obligation. Neither
status asserts a numerical FPS gain or two full gameplay cycles.

The constructor requires these concrete adapters. Every callback receives
`{signal, deadline, phase}`, with a monotonic `performance.now()` deadline. It must
bound its own work and stop on abort. The runner races deadlines but cannot stop
an uncooperative external process/tool. A cancelled/timed-out callback leaves
possible effects UNKNOWN: never retry it or launch another copy until reconciled.

- `lifecycle.reconcile(context)` returns `{state:'absent'}` only after checking the
  exact target executable and all relevant process generations; otherwise return
  `{state:'running',identity}` for exactly one target. Throw on multiple/unknown
  processes, wrong prefix, source mismatch or unavailable process information.
  Identity is `{processKey,candidateId}`. `processKey` must bind PID **and birth
  generation**, not merely a PID or name; `candidateId` binds the admitted matched
  runtime and loaded modules. Steam's existence is independent of game absence.
- `lifecycle.launch(context)` uses the admitted ordinary launch route and returns
  `{identity}`. It must reconcile its own uncertain dispatch before returning;
  it must not replace an existing game, modify settings/saves or close Steam.
  For each fresh generation prepare a new private controller session/token and
  provider binding; never rebind the old virtual-controller manifest to a new PID.
  The stable `inputAdmissionId` identifies admitted source/capability, while the
  adapter resolves its fresh generation-specific session through `identity`.
- `lifecycle.bind({...context,identity})` returns `{identity,nativeWindow:true,
  windowId,app,dimensions:{width,height}}` only after matching the actual native
  window to that process generation and actual screenshot coordinate dimensions.
  `app` is a supported, already-bound CUA App. AX discovery alone is insufficient.
  No focus loop or automatic cross-Space activation is supplied.
- `visual.classify({...context,goal,observation,previous,identity,windowId})`
  reads the actual screenshot and returns a bounded plain JSON classification
  (at most 16 KiB). `observation` is the unmodified screenshot/AX result from
  `createSession`. Required fields are `state` and nonempty `evidence` referencing
  the actual observation. Unsafe states fail closed. A world requires
  `{state:'world',offline:true,hud:true,safe:true,sceneKey,settingsKey}`. The
  response after a safe movement must add `actionVerified:true`; route end must
  add `routeReturned:true`. Normal game exit must visibly reach
  `{state:'title',normalExitVerified:true}` after at least one normal-exit action.
  Desktop selection/confirmation can take several observed actions under
  `prepare-desktop-quit`; only the final `next.intent:'quit-desktop'` uses
  the terminal action. Bounded process reconciliation then waits for asynchronous
  shutdown under one phase deadline before launching again.
- For a needed visible action the classifier supplies
  `next:{intent,evidence,action}`. DS3 policy admits explicit intents from its
  source, rejects online Continue and restricts the initial safe movement to one
  WASD pulse at most 250 ms. An unverified response stops movement, even if another
  pulse is proposed; it never infers success from key delivery. Loading
  waits are bounded and each action consumes a fresh observation token.
- `visual.admitRoute({...context,route,classification,identity,stationary})`
  independently inspects the current post-stationary image and known route and
  returns `{admitted:true,routeId:route.id,evidence}`. The exact saved location,
  safe corridor, camera, endpoints and timing must have been established live.
  No default DS3 route or assumption that reverse input restores position exists.
- `input.neutral({...context,identity,admissionId})` publishes and verifies a
  fresh neutral state through the admitted process-local input provider. Return
  `{identity,admissionId,neutral:true,acknowledged:true,evidence}` only on a fresh
  matching poll receipt. Historical `status` is not a live-neutral proof.
- `timed.run({...context,kind,durationMs,identity,windowId,sceneKey,settingsKey,
  inputAdmissionId,telemetryAdmissionId,inputExpiryMs,route})` executes one whole
  interval **locally**, including telemetry start/stop and, for a route, the fixed
  offset schedule plus final neutral acknowledgment. Stationary `route` is null.
  Do not drive the route via repeated agent/tool calls or `session.act`: those
  introduce round trips/screenshots/AX into the timed interval. No CUA App/session
  is passed to this callback. The concrete [local timed executor](TIMED_EXECUTOR.md)
  composes the native collector and controller internals; supply its admitted
  session/trace identities. Source availability does not activate it in a game.
  It must reject late schedule steps instead of
  stretching/renewing them, enforce input expiry and neutral in `finally`, stop
  collection on abort and preserve raw records under the telemetry contract.

The timed receipt requires `{identity,kind,valid:true,overflow:false,
guiObservations:0,toolRoundTrips:0,clock:'performance.now',startedAt,endedAt,
sceneKey,settingsKey,inputAdmissionId,telemetryAdmissionId,
neutralAcknowledged:true,maxInputLeaseMs,evidence}` plus `routeId` for a route.
An in-process adapter uses the runner's monotonic clock. The concrete child
executor instead returns `clock:'anchored-child-monotonic'`, the native child
duration and honestly anchored lower/upper endpoint bounds with an offset range;
the runner validates this alternate schema without inventing an exact epoch. The interval must be at least the fixed
requested duration and fit inside the callback. `maxInputLeaseMs` must not exceed
the admitted expiry. Evidence is a bounded reference (at most 4096 characters),
not embedded raw telemetry. The adapter owns truthful provenance: these checked
receipt fields are not an independent observer or proof of unreported activity.
Capture references remain `valid:false` until post-interval neutral and actual
end-scene verification pass; rejected end-state captures retain their evidence
reference with that invalid status. Scene transitions during a timed interval require the independently admitted
non-GUI observer/telemetry to mark the receipt invalid; endpoint screenshots
alone cannot exclude an intervening invasion, loading, health change or drift.

Required `config` fields: `candidateId`, `inputAdmissionId`,
`telemetryAdmissionId`, `inputExpiryMs` (1..1000), `warmupMs` (1..120000),
`stationaryMs` (1..60000), `phaseTimeoutMs` (1..180000),
`totalTimeoutMs` (1..900000), `cleanupTimeoutMs` (1..5000),
`maxVisualSteps` (1..100), and `route`. Choose a phase deadline larger than the
whole warmup and every timed callback. Optional `adoptProcessKey` permits exactly
one reconciled existing generation; there is no automatic adoption.

`route` is `{id,sceneKey,settingsKey,durationMs,steps}`. A step is
`{offsetMs,durationMs,state}` with non-overlapping monotonic offsets, 1..500 ms
holds, at most the admitted expiry, at most 64 steps and 60 seconds total. State
uses the existing virtual-controller protocol's numeric fields and rejects its
reserved bit. DS3 allows only nonzero left-stick axes; no combat/item buttons or
camera actions. A plan does not prove movement or safe return.

## Quit, failure and retention

`session.finish(token,action)` is a terminal counterpart to `act`, with the same
fresh-token and bounded-action guards. It sends one final observed Quit action
and closes the session permanently, with no screenshot of the now-absent window.
`delivered:true` means only that the CUA call returned. Only subsequent process
reconciliation supplies exit evidence. A rejected/failed finish also closes the
session; there are no retries.

On failure, the runner attempts one independent bounded neutral call if it has a
window binding. It never automatically confirms, exits, kills, restores saves
or relaunches after failure. Cleanup success cannot settle an in-flight timed or
CUA call that ignored abort. Process/window/input/capture state must be reconciled
by the owner before another run. Aborting a runner does not kill any game.

The runner owns only ephemeral in-memory workflow data: at most 8192 small phase
records, two sanitized capture references, and one most recent screenshot/AX
observation and classification. It starts no background producer and writes no
files. Release the runner after handoff; its TTL is the caller's run/handoff
lifetime. Raw screenshots and telemetry remain owned by the caller/telemetry
contract, with its existing record/byte budgets and hold policy. Persist only the
result and necessary evidence through the existing campaign record; do not add
an unbounded lifecycle log. No cleanup of user data is performed.

## Checks and reuse

Run `node --test tools/gameplay/session.test.mjs tools/gameplay/lifecycle.test.mjs`.
They test the finite positive flow and negative identity, native-window, network,
offline, visual action proof, scene drift, admission, telemetry contamination,
neutral, cancellation/deadline, quit/process absence and controller-bound cases.
They do not exercise a game, real classifier, timing accuracy, input delivery,
producer expiry, performance or preservation of the current Space.

The existing CUA session and virtual-controller protocol were reused. The
[SoulsGym input source](https://github.com/amacati/SoulsGym/blob/main/soulsgym/core/game_input.py)
and [environment source](https://github.com/amacati/SoulsGym/blob/main/soulsgym/envs/soulsenv.py)
were inspected: its Windows/X11 input and memory/state-changing environment do
not fit this macOS visual-only lifecycle or its save-preservation boundary. The
existing protocol documentation already compares InputFusion and xinputd. No
external code or dependency was copied; this finite adapter keeps those existing
capability owners and leaves real visual/timed evidence admission explicit.
