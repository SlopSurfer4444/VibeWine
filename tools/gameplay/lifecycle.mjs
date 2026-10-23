import { createSession } from './session.mjs';

const required = (value, message) => { if (!value) throw new Error(message); };
const finite = (n, min, max) => Number.isFinite(n) && n >= min && n <= max;
const text = value => typeof value === 'string' && value.trim().length > 0;
function freeze(value) {
  const copy = structuredClone(value), seen = new WeakSet();
  function seal(item) {
    if (item === null || typeof item !== 'object' || seen.has(item)) return item;
    seen.add(item);
    for (const child of Object.values(item)) seal(child);
    return Object.freeze(item);
  }
  return seal(copy);
}
const key = identity => JSON.stringify([identity?.processKey, identity?.candidateId]);
function identity(value) {
  required(value && text(value.processKey) && text(value.candidateId), 'Exact process generation and candidate identity required');
  return freeze({ processKey: value.processKey, candidateId: value.candidateId });
}
function routePlan(route) {
  required(route && text(route.id) && text(route.sceneKey) && text(route.settingsKey) &&
    Array.isArray(route.steps) && route.steps.length > 0 && route.steps.length <= 64, 'Explicit finite route required');
  required(finite(route.durationMs, 1, 60000), 'Route duration outside budget');
  let end = 0;
  for (const step of route.steps) {
    required(finite(step.offsetMs, end, route.durationMs) && finite(step.durationMs, 1, 500) &&
      step.offsetMs + step.durationMs <= route.durationMs, 'Overlapping or unbounded route step');
    required(step.state && Object.keys(step.state).every(k => ['buttons', 'lt', 'rt', 'lx', 'ly', 'rx', 'ry'].includes(k)), 'Invalid controller state');
    for (const [name, value] of Object.entries(step.state)) {
      const [min, max] = name === 'buttons' ? [0, 65535] : ['lt', 'rt'].includes(name) ? [0, 255] : [-32768, 32767];
      required(Number.isInteger(value) && finite(value, min, max) && (name !== 'buttons' || !(value & 0x0800)), 'Invalid controller value');
    }
    required(Object.values(step.state).some(v => v !== 0), 'Route step must contain an explicit action');
    end = step.offsetMs + step.durationMs;
  }
  return freeze(route);
}

function timedClockValid(receipt, started, ended, durationMs) {
  if (receipt.clock === 'performance.now') {
    return finite(receipt.startedAt, started, ended) && finite(receipt.endedAt, receipt.startedAt + durationMs, ended);
  }
  if (receipt.clock !== 'anchored-child-monotonic') return false;
  const m = receipt.clockMapping;
  const close = (a, b) => Number.isFinite(a) && Number.isFinite(b) && Math.abs(a - b) <= 0.05;
  return m && finite(m.parentBeforeSpawn, started, ended) &&
    finite(m.parentReadyReceived, m.parentBeforeSpawn, ended) &&
    Number.isFinite(m.childReadyMonotonicMs) &&
    close(m.offsetLowerMs, m.parentBeforeSpawn - m.childReadyMonotonicMs) &&
    close(m.offsetUpperMs, m.parentReadyReceived - m.childReadyMonotonicMs) &&
    finite(receipt.startedMonotonicMs, m.childReadyMonotonicMs, Number.MAX_VALUE) &&
    finite(receipt.endedMonotonicMs, receipt.startedMonotonicMs + durationMs, Number.MAX_VALUE) &&
    close(receipt.durationMonotonicMs, receipt.endedMonotonicMs - receipt.startedMonotonicMs) &&
    close(receipt.startedAtLower, receipt.startedMonotonicMs + m.offsetLowerMs) &&
    close(receipt.startedAtUpper, receipt.startedMonotonicMs + m.offsetUpperMs) &&
    close(receipt.endedAtLower, receipt.endedMonotonicMs + m.offsetLowerMs) &&
    close(receipt.endedAtUpper, receipt.endedMonotonicMs + m.offsetUpperMs) &&
    receipt.startedAtLower >= started && receipt.endedAtLower <= ended;
}

// Adapters carry the actual CUA, process, input and telemetry capabilities. This
// module cannot manufacture visual evidence or enforce an uncooperative adapter.
// A timed adapter must execute locally and honor AbortSignal + deadline itself.
export function createLifecycle({ lifecycle, visual, policy, input, timed, config, clock = () => performance.now() }) {
  for (const [object, names] of [[lifecycle, ['reconcile', 'launch', 'bind']], [visual, ['classify', 'admitRoute']],
    [policy, ['decide']], [input, ['neutral']], [timed, ['run']]]) {
    for (const name of names) required(typeof object?.[name] === 'function', `Missing adapter: ${name}`);
  }
  required(config && text(config.candidateId), 'Candidate admission required');
  required(finite(config.warmupMs, 1, 120000) && finite(config.stationaryMs, 1, 60000), 'Fixed warmup/capture budgets required');
  required(finite(config.phaseTimeoutMs, 1, 180000) && finite(config.totalTimeoutMs, 1, 900000) &&
    finite(config.cleanupTimeoutMs, 1, 5000), 'Finite phase, total and cleanup deadlines required');
  required(Number.isInteger(config.maxVisualSteps) && finite(config.maxVisualSteps, 1, 100), 'Finite visual step cap required');
  required(text(config.inputAdmissionId) && text(config.telemetryAdmissionId) && finite(config.inputExpiryMs, 1, 1000), 'Input expiry and telemetry admission required');
  const route = routePlan(config.route);
  required(route.steps.every(step => step.durationMs <= config.inputExpiryMs), 'Route pulse exceeds admitted input expiry');
  if (policy.validateRoute) policy.validateRoute(route);
  const settings = freeze(config);
  let used = false, currentPhase = 'idle', bound, session, timedActive = false;
  let last, lastObservation, actionCount = 0, runSignal, totalDeadline;
  const events = [], captures = [];
  let uncertainOperation = false;
  const same = value => required(key(value) === key(bound.identity), 'Process identity changed');
  const record = (phase, status) => {
    required(events.length < 8192, 'Lifecycle event budget exhausted');
    events.push(Object.freeze({ phase, status, at: clock() }));
  };

  async function invoke(fn, phase, { cleanup = false } = {}) {
    currentPhase = phase;
    const deadline = cleanup ? clock() + settings.cleanupTimeoutMs : Math.min(totalDeadline, clock() + settings.phaseTimeoutMs);
    const controller = new AbortController();
    let timer, onAbort, entered = false;
    try {
      required(cleanup || !runSignal?.aborted, 'Cancelled');
      required(deadline > clock(), 'Deadline exhausted');
      const stop = new Promise((_, reject) => {
        const fail = reason => { controller.abort(reason); reject(new Error(reason)); };
        timer = setTimeout(() => fail('Deadline exhausted; adapter side effects unknown'), Math.max(0, deadline - clock()));
        onAbort = () => fail('Cancelled; adapter side effects unknown');
        if (!cleanup) runSignal?.addEventListener('abort', onAbort, { once: true });
      });
      entered = true;
      const result = await Promise.race([Promise.resolve().then(() => {
        required(!controller.signal.aborted, 'Cancelled before adapter dispatch');
        return fn({ signal: controller.signal, deadline, phase });
      }), stop]);
      required(cleanup || !runSignal?.aborted, 'Cancelled');
      if (clock() > deadline) {
        controller.abort('Adapter exceeded deadline');
        throw new Error('Adapter exceeded deadline; adapter side effects unknown');
      }
      return result;
    } catch (error) {
      if (entered && controller.signal.aborted) uncertainOperation = true;
      throw error;
    } finally {
      clearTimeout(timer);
      if (onAbort) runSignal?.removeEventListener('abort', onAbort);
    }
  }
  async function phase(name, fn) {
    record(name, 'started');
    const result = await invoke(fn, name);
    record(name, 'accepted');
    return result;
  }
  const wait = (ms, context) => new Promise((resolve, reject) => {
    if (context.signal.aborted) return reject(new Error('Cancelled'));
    const timer = setTimeout(done, ms);
    function abort() { clearTimeout(timer); reject(new Error('Cancelled')); }
    function done() { context.signal.removeEventListener('abort', abort); resolve(); }
    context.signal.addEventListener('abort', abort, { once: true });
  });
  async function reconcile(expected) {
    const value = await phase('reconcile', ctx => lifecycle.reconcile(ctx));
    required(value?.state === 'absent' || value?.state === 'running', 'Ambiguous process reconciliation');
    if (value.state === 'running') {
      const actual = identity(value.identity);
      required(actual.candidateId === settings.candidateId, 'Unadmitted runtime candidate');
      if (expected) required(key(actual) === key(expected), 'Unexpected process generation');
    }
    return value;
  }
  async function launchAndBind() {
    const launched = await phase('launch', ctx => lifecycle.launch(ctx));
    const actual = identity(launched?.identity);
    required(actual.candidateId === settings.candidateId, 'Unadmitted launched candidate');
    const state = await reconcile(actual);
    required(state.state === 'running', 'Launch did not yield a live game');
    await bind(actual);
  }
  async function bind(actual) {
    const value = await phase('bind-native-window', ctx => lifecycle.bind({ ...ctx, identity: actual }));
    required(value?.app && value.nativeWindow === true && text(value.windowId), 'True native game window binding required');
    required(key(value.identity) === key(actual), 'Window belongs to another process generation');
    required(finite(value.dimensions?.width, 1, 32768) && finite(value.dimensions?.height, 1, 32768), 'Observed screenshot dimensions required');
    bound = { identity: actual, windowId: value.windowId, dimensions: freeze({ width: value.dimensions.width, height: value.dimensions.height }) };
    // All session access passes this guard. No session/App is passed to timed.run.
    const guardedApp = new Proxy(value.app, { get(target, prop) {
      const member = target[prop];
      return typeof member === 'function' ? (...args) => {
        required(!timedActive, 'CUA is forbidden inside timed intervals');
        return member.apply(target, args);
      } : member;
    } });
    session = createSession(guardedApp, { additionalKeys: policy.additionalKeys ?? [] });
    last = undefined; lastObservation = undefined;
  }
  async function observe(goal, previous = last) {
    lastObservation = await phase(`observe:${goal}`, () => session.observe(bound.dimensions));
    return classify(goal, previous);
  }
  async function classify(goal, previous) {
    const classified = await phase(`classify:${goal}`, ctx => visual.classify({ ...ctx,
      goal, observation: lastObservation, previous, identity: bound.identity, windowId: bound.windowId }));
    required(classified && text(classified.state) && text(classified.evidence) && JSON.stringify(classified).length <= 16384, 'Missing or oversized actual visual classification/evidence');
    required(!['unknown', 'network-error', 'character-selector', 'combat', 'invasion', 'dead'].includes(classified.state) &&
      classified.ambiguous !== true && classified.focusLost !== true && classified.healthChanged !== true, 'Unsafe or ambiguous game state');
    last = freeze(classified);
    return last;
  }
  const world = c => c.state === 'world' && c.offline === true && c.hud === true && c.safe === true && text(c.sceneKey) && text(c.settingsKey);
  const scene = c => required(world(c) && c.sceneKey === route.sceneKey && c.settingsKey === route.settingsKey, 'World/scene/settings changed or offline safety unverified');
  async function navigate(goal, done) {
    await observe(goal);
    const initialActions = actionCount;
    for (let step = 0; step < settings.maxVisualSteps; ++step) {
      if (done(last, actionCount - initialActions)) return last;
      required(goal !== 'safe-action' || actionCount === initialActions, 'Safe action response unverified; no repeated movement');
      const decision = await phase(`decide:${goal}`, ctx => policy.decide({ ...ctx, goal, classification: last, step }));
      required(decision && ['action', 'wait'].includes(decision.type), 'No admitted visible next action');
      if (decision.type === 'wait') {
        required(finite(decision.durationMs, 1, 1000), 'Visual wait outside budget');
        await phase(`wait:${goal}`, ctx => wait(decision.durationMs, ctx));
        await observe(goal);
      } else {
        const previous = last;
        lastObservation = await phase(`action:${goal}`, () => session.act(lastObservation.token, decision.action, bound.dimensions));
        actionCount++;
        await classify(goal, previous);
      }
    }
    // Accept an outcome reached by the last allowed action, without another input.
    required(done(last, actionCount - initialActions), `Visual step cap exhausted: ${goal}`);
    return last;
  }
  async function neutral(cleanup = false) {
    const receipt = await invoke(ctx => input.neutral({ ...ctx, identity: bound.identity,
      admissionId: settings.inputAdmissionId }), cleanup ? 'failure-neutral' : 'neutral', { cleanup });
    same(receipt?.identity);
    required(receipt?.neutral === true && receipt.acknowledged === true && receipt.admissionId === settings.inputAdmissionId && text(receipt.evidence), 'Neutral input unacknowledged');
    return receipt;
  }
  async function measure(kind, durationMs, admittedRoute) {
    scene(last);
    await neutral();
    const started = clock();
    timedActive = true;
    let receipt;
    try {
      receipt = await phase(`timed:${kind}`, ctx => timed.run({ ...ctx, kind, durationMs,
        identity: bound.identity, windowId: bound.windowId, sceneKey: last.sceneKey, settingsKey: last.settingsKey,
        inputAdmissionId: settings.inputAdmissionId, telemetryAdmissionId: settings.telemetryAdmissionId,
        inputExpiryMs: settings.inputExpiryMs, route: admittedRoute ?? null }));
    } finally { timedActive = false; }
    const ended = clock();
    same(receipt?.identity);
    required(receipt?.kind === kind && receipt.valid === true && receipt.overflow === false &&
      receipt.guiObservations === 0 && receipt.toolRoundTrips === 0 && timedClockValid(receipt, started, ended, durationMs) &&
      receipt.neutralAcknowledged === true && finite(receipt.maxInputLeaseMs, 0, settings.inputExpiryMs) &&
      (kind !== 'route' || receipt.routeId === route.id) &&
      receipt.inputAdmissionId === settings.inputAdmissionId && receipt.telemetryAdmissionId === settings.telemetryAdmissionId &&
      receipt.sceneKey === route.sceneKey && receipt.settingsKey === route.settingsKey && text(receipt.evidence) && receipt.evidence.length <= 4096, 'Invalid or contaminated timed capture');
    const captureIndex = captures.length;
    const capture = { kind, durationMs, routeId: receipt.routeId ?? null, valid: false, endStateVerified: false, overflow: false,
      guiObservations: 0, toolRoundTrips: 0, clock: receipt.clock,
      clockMapping: receipt.clockMapping ?? null, durationMonotonicMs: receipt.durationMonotonicMs ?? null,
      startedAtLower: receipt.startedAtLower ?? null, startedAtUpper: receipt.startedAtUpper ?? null,
      endedAtLower: receipt.endedAtLower ?? null, endedAtUpper: receipt.endedAtUpper ?? null,
      displayedMetricsStatus: receipt.displayedMetricsStatus ?? 'inconclusive',
      fixedWindowMetricsStatus: receipt.fixedWindowMetricsStatus ?? 'unknown',
      nativePresentationWindow: receipt.nativePresentationWindow ?? null,
      presentationClockInvocationBounds: receipt.presentationClockInvocationBounds ?? null,
      presentationClockSamples: receipt.presentationClockSamples ?? null,
      presentationEvidence: receipt.presentationEvidence ?? null,
      traceEnvelopeMonotonicBounds: receipt.traceEnvelopeMonotonicBounds ?? null, schedule: receipt.schedule ?? null,
      traceGeneration: receipt.traceGeneration ?? null, traceIdentity: receipt.traceIdentity ?? null, identity: receipt.identity, startedAt: receipt.startedAt, endedAt: receipt.endedAt,
      sceneKey: receipt.sceneKey, settingsKey: receipt.settingsKey, evidence: receipt.evidence,
      telemetryAdmissionId: receipt.telemetryAdmissionId, inputAdmissionId: receipt.inputAdmissionId,
      neutralAcknowledged: receipt.neutralAcknowledged, maxInputLeaseMs: receipt.maxInputLeaseMs };
    captures.push(freeze(capture));
    await neutral();
    await observe(`after-${kind}`);
    scene(last);
    if (kind === 'route') required(last.routeReturned === true, 'Route did not visibly return to admitted end scene');
    captures[captureIndex] = freeze({ ...capture, valid: true, endStateVerified: true });
  }
  return Object.freeze({
    get phase() { return currentPhase; },
    async run({ signal } = {}) {
      required(!used, 'Lifecycle is single-use; reconcile before creating another'); used = true;
      runSignal = signal; totalDeadline = clock() + settings.totalTimeoutMs;
      try {
        const initial = await reconcile();
        if (initial.state === 'running') {
          required(text(settings.adoptProcessKey) && initial.identity.processKey === settings.adoptProcessKey, 'Existing game requires explicit adoption; no duplicate launch');
          await bind(identity(initial.identity));
        } else await launchAndBind();
        await navigate('load-offline', c => world(c));
        scene(last);
        await navigate('safe-action', (c, count) => world(c) && count > 0 && c.actionVerified === true);
        scene(last);
        await neutral();
        await phase('fixed-warmup', ctx => wait(settings.warmupMs, ctx));
        await observe('after-warmup'); scene(last);
        await measure('stationary', settings.stationaryMs);
        const admission = await phase('admit-route', ctx => visual.admitRoute({ ...ctx, route,
          classification: last, identity: bound.identity, stationary: captures[0] }));
        required(admission?.admitted === true && admission.routeId === route.id && text(admission.evidence), 'Finite route not explicitly visually admitted');
        await measure('route', route.durationMs, route);
        await navigate('normal-exit', (c, count) => c.state === 'title' && c.normalExitVerified === true && count > 0);
        // Quit's expected window disappearance is checked by process identity,
        // not treated as a failed post-action CUA screenshot.
        await navigate('prepare-desktop-quit', c => ['title', 'menu', 'quit-confirmation'].includes(c.state) &&
          c.next?.intent === 'quit-desktop');
        const quit = await phase('decide:quit-desktop', ctx => policy.decide({ ...ctx,
          goal: 'quit-desktop', classification: last, step: 0 }));
        required(quit?.type === 'action', 'Visible normal Quit action required');
        await neutral();
        await phase('quit-desktop', () => session.finish(lastObservation.token, quit.action));
        await phase('process-gone', async ctx => {
          for (;;) {
            const state = await lifecycle.reconcile({ ...ctx, expected: 'absent', identity: bound.identity });
            required(state?.state === 'absent' || state?.state === 'running', 'Ambiguous exit reconciliation');
            if (state.state === 'absent') return;
            same(identity(state.identity));
            await wait(100, ctx);
          }
        });
        const previous = bound.identity;
        await launchAndBind();
        required(bound.identity.processKey !== previous.processKey, 'Relaunch reused old process generation');
        await navigate('relaunch-ready', c => ['title', 'menu'].includes(c.state));
        await neutral();
        currentPhase = 'complete'; record('complete', 'accepted');
        return freeze({ status: 'complete', events, captures, identity: bound.identity,
          obligation: 'Relaunched game remains running; explicitly adopt or close through its normal menu.' });
      } catch (error) {
        const failedPhase = currentPhase;
        let cleanup = 'not-bound';
        if (bound) {
          try { await neutral(true); cleanup = 'neutral-acknowledged'; }
          catch (failure) { cleanup = `neutral-unknown: ${failure.message}`; }
        }
        currentPhase = 'failed'; record(failedPhase, 'failed');
        return freeze({ status: 'failed', failedPhase, error: error.message, cleanup, uncertainOperation,
          events, captures, timedFailure: error.workerResult ?? null, identity: bound?.identity ?? null,
          obligation: 'Stop input; reconcile process/window, timed producer and neutral state before another lifecycle. No automatic exit/relaunch on failure.' });
      }
    },
  });
}
