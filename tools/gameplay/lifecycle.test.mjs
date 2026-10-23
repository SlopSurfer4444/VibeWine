import test from 'node:test';
import assert from 'node:assert/strict';
import { createLifecycle } from './lifecycle.mjs';
import { ds3Policy } from '../../profiles/ds3/lifecycle-policy.mjs';
import { createSession } from './session.mjs';

function fixture(change = () => {}) {
  let now = 0, running = null, generation = 0, index = 0;
  const calls = [], abort = new AbortController();
  const world = extra => ({ state: 'world', offline: true, hud: true, safe: true,
    sceneKey: 'safe-scene', settingsKey: 'fixed-settings', evidence: 'fixture image', ...extra });
  const next = intent => ({ intent, evidence: 'fixture visible control', action: { type: 'key', key: intent === 'safe-action' ? 'w' : 'e', durationMs: 50 } });
  const images = [
    { state: 'menu', offline: true, next: next('continue-offline') }, world(),
    world({ next: next('safe-action') }), world({ actionVerified: true }), world(), world(),
    world({ routeReturned: true }), world({ next: next('open-game-menu') }),
    { state: 'exit-menu', next: next('select-quit-to-title') },
    { state: 'title', normalExitVerified: true },
    { state: 'title', next: next('quit-desktop') }, { state: 'title' },
  ].map(c => ({ evidence: 'fixture image', ...c }));
  const app = {
    async getScreenshot() { calls.push('screenshot'); return 'fixture pixels'; },
    async getAXState() { calls.push('AX'); return 'fixture tree'; },
    async pressKey() { calls.push('key'); if (index === 11) running = null; },
  };
  const options = {
    clock: () => now,
    lifecycle: {
      async reconcile() { calls.push('reconcile'); return running ? { state: 'running', identity: running } : { state: 'absent' }; },
      async launch() { calls.push('launch'); running = { processKey: `pid:${++generation}:start`, candidateId: 'matched' }; return { identity: running }; },
      async bind({ identity }) { calls.push('bind'); return { app, identity, nativeWindow: true, windowId: 'native-42', dimensions: { width: 100, height: 100 } }; },
    },
    visual: {
      async classify() { calls.push('classify'); return images[index++]; },
      async admitRoute() { calls.push('admit-route'); return { admitted: true, routeId: 'out-back', evidence: 'fixture inspected safe corridor' }; },
    },
    policy: ds3Policy,
    input: { async neutral({ identity, admissionId }) { calls.push('neutral'); return { identity, admissionId, neutral: true, acknowledged: true, evidence: 'fixture receipt' }; } },
    timed: { async run(args) {
      calls.push(`timed:${args.kind}`);
      const startedAt = now; now += args.durationMs;
      return { ...args, signal: undefined, identity: args.identity, startedAt, endedAt: now,
        routeId: args.route?.id, neutralAcknowledged: true, maxInputLeaseMs: 1, valid: true, overflow: false, guiObservations: 0, toolRoundTrips: 0, clock: 'performance.now', evidence: 'fixture bounded telemetry' };
    } },
    config: {
      candidateId: 'matched', warmupMs: 1, stationaryMs: 2, phaseTimeoutMs: 100,
      totalTimeoutMs: 1000, cleanupTimeoutMs: 10, maxVisualSteps: 5,
      inputAdmissionId: 'producer-source-bound', telemetryAdmissionId: 'trace-source-bound', inputExpiryMs: 250,
      route: { id: 'out-back', sceneKey: 'safe-scene', settingsKey: 'fixed-settings', durationMs: 4,
        steps: [{ offsetMs: 0, durationMs: 1, state: { ly: 10000 } }, { offsetMs: 2, durationMs: 1, state: { ly: -10000 } }] },
    },
  };
  const f = { options, calls, images, abort, app, setRunning: v => { running = v; }, now: () => now };
  change(f);
  f.runner = createLifecycle(options);
  f.run = () => f.runner.run({ signal: abort.signal });
  return f;
}

test('complete finite offline cycle: stationary then admitted route, normal quit, absent process and fresh relaunch', async () => {
  const f = fixture(); const result = await f.run();
  assert.equal(result.status, 'complete', JSON.stringify(result));
  assert.equal(f.calls.filter(c => c === 'launch').length, 2);
  assert.deepEqual(result.captures.map(c => c.kind), ['stationary', 'route']);
  assert.ok(f.calls.indexOf('admit-route') > f.calls.indexOf('timed:stationary'));
  assert.ok(f.calls.indexOf('timed:route') > f.calls.indexOf('admit-route'));
  assert.equal(result.identity.processKey, 'pid:2:start');
  await assert.rejects(f.run(), /single-use/);
});

test('existing unadmitted game cannot be duplicated or controlled', async () => {
  const f = fixture(f => f.setRunning({ candidateId: 'matched', processKey: 'owned-by-user' }));
  const r = await f.run(); assert.equal(r.status, 'failed'); assert.match(r.error, /explicit adoption/);
  assert.ok(!f.calls.includes('launch') && !f.calls.includes('key'));
});

test('a semantic/AX match cannot substitute for a real native window', async () => {
  const f = fixture(f => { f.options.lifecycle.bind = async ({ identity }) => ({ app: f.app, identity, nativeWindow: false, windowId: 'AX-only' }); });
  const r = await f.run(); assert.equal(r.status, 'failed'); assert.match(r.error, /native game window/); assert.ok(!f.calls.includes('key'));
});

test('network/ambiguous/character selector and online Continue stop before input', async () => {
  for (const state of ['network-error', 'unknown', 'character-selector']) {
    const f = fixture(f => { f.images[0].state = state; });
    assert.equal((await f.run()).status, 'failed'); assert.ok(!f.calls.includes('key'));
  }
  const f = fixture(f => { f.images[0].offline = false; });
  assert.match((await f.run()).error, /Offline mode/); assert.ok(!f.calls.includes('key'));
});

test('world alone is not proof of a successful safe action', async () => {
  const f = fixture(f => { f.images[3].actionVerified = false; f.images[3].next = { intent: 'safe-action', evidence: 'repeat attempt', action: { type: 'key', key: 'w', durationMs: 20 } }; });
  const r = await f.run(); assert.equal(r.status, 'failed'); assert.match(r.error, /no repeated movement/); assert.equal(f.calls.filter(c => c === 'key').length, 2); assert.ok(!f.calls.includes('timed:stationary'));
});

test('invalid timed telemetry is rejected and scene/settings drift rejects a capture', async () => {
  for (const bad of [{ overflow: true }, { guiObservations: 1 }, { toolRoundTrips: 1 }, { identity: { processKey: 'other', candidateId: 'matched' } }, { clock: 'wall' }, { startedAt: -1 }]) {
    const f = fixture(f => { const run = f.options.timed.run; f.options.timed.run = async args => ({ ...await run(args), ...bad }); });
    const r = await f.run(); assert.equal(r.status, 'failed', JSON.stringify(bad)); assert.ok(!f.calls.includes('timed:route'));
  }
  const f = fixture(f => { f.images[5].settingsKey = 'changed-resolution'; });
  const changed = await f.run(); assert.match(changed.error, /settings changed/); assert.equal(changed.captures[0].valid, false); assert.ok(!f.calls.includes('timed:route'));
});

test('route requires separate visual admission and visible return', async () => {
  const f = fixture(f => { f.options.visual.admitRoute = async () => ({ admitted: false }); });
  assert.match((await f.run()).error, /not explicitly/); assert.ok(!f.calls.includes('timed:route'));
  const g = fixture(f => { f.images[6].routeReturned = false; });
  const noReturn = await g.run(); assert.match(noReturn.error, /visibly return/); assert.equal(noReturn.captures[1].valid, false); assert.equal(g.calls.filter(c => c === 'launch').length, 1);
});

test('lost neutral input stops capture; failure cleanup is reported separately', async () => {
  const f = fixture(f => { f.options.input.neutral = async () => ({ acknowledged: false }); });
  const r = await f.run(); assert.equal(r.status, 'failed'); assert.match(r.cleanup, /neutral-unknown/); assert.ok(!f.calls.includes('timed:stationary'));
});

test('cancellation during timed work requests adapter abort, attempts neutral and prevents relaunch', async () => {
  let sawAbort = false;
  const f = fixture(f => { f.options.timed.run = args => new Promise(resolve => {
    args.signal.addEventListener('abort', () => { sawAbort = true; resolve({}); });
    f.abort.abort();
  }); });
  const r = await f.run(); assert.equal(r.status, 'failed'); assert.equal(sawAbort, true);
  assert.equal(r.uncertainOperation, true); assert.equal(r.cleanup, 'neutral-acknowledged');
  assert.equal(f.calls.filter(c => c === 'launch').length, 1);
});

test('deadline bounds an uncooperative adapter and never retries its uncertain launch', async () => {
  const f = fixture(f => { f.options.config.phaseTimeoutMs = 5; f.options.lifecycle.launch = () => new Promise(() => {}); });
  const r = await f.run(); assert.equal(r.status, 'failed'); assert.equal(r.failedPhase, 'launch');
  assert.equal(r.uncertainOperation, true); assert.match(r.error, /Deadline/);
});

test('normal quit must actually remove game before relaunch', async () => {
  const f = fixture(f => { f.app.pressKey = async () => { f.calls.push('key'); }; });
  const r = await f.run(); assert.equal(r.status, 'failed'); assert.match(r.error, /Deadline/);
  assert.equal(f.calls.filter(c => c === 'launch').length, 1);
});

test('route admission validates bounded non-overlapping protocol controls before any adapter call', () => {
  for (const state of [{ buttons: 0x0800 }, { ly: 40000 }, { lt: -1 }, { unknown: 1 }, {}]) {
    let f;
    assert.throws(() => { f = fixture(f => { f.options.config.route.steps[0].state = state; }); }, /controller|explicit action/);
    assert.equal(f, undefined);
  }
  assert.throws(() => fixture(f => { f.options.config.route.steps[1].offsetMs = 0; }), /Overlapping/);
});

test('session final action consumes token and does not screenshot an expected vanished window', async () => {
  let screenshots = 0, inputs = 0;
  const session = createSession({ async getScreenshot() { screenshots++; return 'image'; },
    async getAXState() { return 'tree'; }, async pressKey() { inputs++; } });
  const o = await session.observe({ width: 10, height: 10 });
  assert.deepEqual(await session.finish(o.token, { type: 'key', key: 'Return', durationMs: 10 }), { delivered: true });
  assert.equal(screenshots, 1); assert.equal(inputs, 1); assert.equal(session.blocked, true);
  await assert.rejects(session.observe({ width: 10, height: 10 }), /blocked/);
});

test('session final action preserves invalid input and stale-token guards', async () => {
  for (const action of [{ type: 'key', key: 'F4' }, { type: 'click', x: 11, y: 1 }]) {
    let inputs = 0;
    const s = createSession({ async getScreenshot() { return 'image'; }, async getAXState() { return 'tree'; }, async pressKey() { inputs++; }, async click() { inputs++; } });
    const o = await s.observe({ width: 10, height: 10 }); await assert.rejects(s.finish(o.token, action));
    assert.equal(inputs, 0); assert.equal(s.blocked, true);
  }
});


test('desktop Quit supports observed selection/confirmation and waits for asynchronous process shutdown', async () => {
  let polls = 0;
  const f = fixture(f => {
    f.options.config.phaseTimeoutMs = 400;
    f.images.splice(10, 1,
      { state: 'title', evidence: 'fixture menu', next: { intent: 'select-desktop-quit', evidence: 'visible quit selection', action: { type: 'key', key: 'q', durationMs: 50 } } },
      { state: 'quit-confirmation', evidence: 'fixture confirmation', next: { intent: 'quit-desktop', evidence: 'visible Yes', action: { type: 'key', key: 'e', durationMs: 50 } } });
    f.app.pressKey = async () => { f.calls.push('key'); };
    const reconcile = f.options.lifecycle.reconcile;
    f.options.lifecycle.reconcile = async ctx => {
      if (ctx.expected === 'absent' && ++polls === 2) f.setRunning(null);
      return reconcile(ctx);
    };
  });
  const r = await f.run(); assert.equal(r.status, 'complete', JSON.stringify(r)); assert.equal(polls, 2);
});

test('DS3 rejects item/combat initial actions and route buttons before execution', async () => {
  const f = fixture(f => { f.images[2].next.action.key = 'e'; });
  const r = await f.run(); assert.equal(r.status, 'failed'); assert.match(r.error, /movement pulse/);
  assert.equal(f.calls.filter(c => c === 'key').length, 1);
  assert.throws(() => fixture(f => { f.options.config.route.steps[0].state = { buttons: 0x1000 }; }), /left-stick/);
  assert.throws(() => fixture(f => { f.options.config.inputExpiryMs = 1; f.options.config.route.steps[0].durationMs = 2; f.options.config.route.steps[1].offsetMs = 3; }), /input expiry/);
});


test('event-loop blocking adapter cannot escape deadline uncertainty reporting', async () => {
  const f = fixture(f => {
    f.options.clock = () => performance.now();
    f.options.config.phaseTimeoutMs = 2;
    f.options.lifecycle.launch = () => {
      const until = performance.now() + 10;
      while (performance.now() < until) {} // hostile CPU-only adapter, no process launch
      return { identity: { processKey: 'late-generation', candidateId: 'matched' } };
    };
  });
  const r = await f.run(); assert.equal(r.status, 'failed'); assert.match(r.error, /exceeded deadline/);
  assert.equal(r.uncertainOperation, true); assert.ok(!f.calls.includes('bind'));
});


test('route admission cannot mutate validated control bounds or payloads', async () => {
  const f = fixture(f => { f.options.visual.admitRoute = async ({ route }) => {
    route.steps[0].durationMs = 50000;
    route.steps[0].state.buttons = 0x1000;
    return { admitted: true, routeId: route.id, evidence: 'mutated fixture' };
  }; });
  const r = await f.run(); assert.equal(r.status, 'failed'); assert.match(r.error, /read only|extensible/);
  assert.ok(!f.calls.includes('timed:route'));
});

test('child monotonic timing is admitted via honest epoch bounds, never relabelled as an exact parent clock', async () => {
  function anchored(f, corrupt = false) {
    const run = f.options.timed.run;
    f.options.timed.run = async args => {
      const parentStart = f.now();
      const result = await run(args);
      return { ...result, clock: 'anchored-child-monotonic', startedAt: undefined, endedAt: undefined,
        startedMonotonicMs: 1000, endedMonotonicMs: 1000 + args.durationMs, durationMonotonicMs: args.durationMs,
        startedAtLower: parentStart, startedAtUpper: parentStart,
        endedAtLower: parentStart + args.durationMs, endedAtUpper: parentStart + args.durationMs,
        clockMapping: { parentBeforeSpawn: parentStart, parentReadyReceived: parentStart, childReadyMonotonicMs: 1000,
          offsetLowerMs: parentStart - 1000, offsetUpperMs: parentStart - 1000 + (corrupt ? 100 : 0) } };
    };
  }
  const accepted = fixture(f => anchored(f));
  assert.equal((await accepted.run()).status, 'complete');
  const corrupt = fixture(f => anchored(f, true));
  assert.match((await corrupt.run()).error, /contaminated/);
});
