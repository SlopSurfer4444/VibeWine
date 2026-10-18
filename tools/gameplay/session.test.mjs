import test from 'node:test';
import assert from 'node:assert/strict';
import { createSession } from './session.mjs';

const bounds = { width: 640, height: 480 };
function fixture(overrides = {}) {
  const calls = [];
  const app = {
    async getAXState(options) { calls.push(['ax', options]); return { text: 'unclassified' }; },
    async getScreenshot(options) { calls.push(['screenshot', options]); return { image: calls.length }; },
    async pressKey(...args) { calls.push(['key', ...args]); },
    async click(...args) { calls.push(['click', ...args]); },
    async performSecondaryAction(...args) { calls.push(['secondary', ...args]); },
    ...overrides,
  };
  return { session: createSession(app), calls };
}

test('one action returns fresh AX and screenshot and consumes token', async () => {
  const { session, calls } = fixture();
  const first = await session.observe(bounds);
  const second = await session.act(first.token, { type: 'key', key: 'w', durationMs: 1000 }, bounds);
  assert.notEqual(second.token, first.token);
  assert.notEqual(second.screenshot, first.screenshot);
  assert.deepEqual(calls.slice(2), [['key', 'w', { durationMs: 1000 }], ['screenshot', { emit: false }], ['ax', { disableDiffing: true }]]);
  await assert.rejects(session.act(first.token, { type: 'key', key: 'w' }, bounds), /Stale/);
  assert.equal(session.blocked, true);
  await assert.rejects(session.observe(bounds), /Session blocked/);
  assert.equal(calls.length, 5);
});

test('input error still observes then permanently blocks', async () => {
  const { session, calls } = fixture({ async pressKey() { calls.push('failed-key'); throw Error('input'); } });
  const first = await session.observe(bounds);
  await assert.rejects(session.act(first.token, { type: 'key', key: 'w' }, bounds), error => {
    assert.equal(error.cause.message, 'input');
    assert.ok(error.observation.screenshot);
    return true;
  });
  assert.deepEqual(calls.slice(2), ['failed-key', ['screenshot', { emit: false }], ['ax', { disableDiffing: true }]]);
  await assert.rejects(session.act(first.token, { type: 'key', key: 'w' }, bounds), /Session blocked/);
});

test('observation failure blocks without retry or input', async () => {
  let attempts = 0;
  const { session, calls } = fixture({ async getScreenshot() { attempts++; throw Error('capture'); } });
  await assert.rejects(session.observe(bounds), /capture/);
  await assert.rejects(session.observe(bounds), /Session blocked/);
  assert.equal(attempts, 1);
  assert.deepEqual(calls, []);
});

test('rejects unsafe keys, holds and coordinates before dispatch', async () => {
  for (const action of [
    { type: 'key', key: 'Space' }, { type: 'key', key: 'ArrowUp' },
    { type: 'key', key: 'Enter' }, { type: 'key', key: 'w', durationMs: 1001 },
    { type: 'key', key: 'w', durationMs: 0 }, { type: 'key', key: 'w', durationMs: NaN },
    { type: 'click', x: -1, y: 0 }, { type: 'click', x: 640, y: 0 },
    { type: 'click', x: 0, y: 480 }, { type: 'click', x: NaN, y: 0 },
  ]) {
    const { session, calls } = fixture();
    const first = await session.observe(bounds);
    await assert.rejects(session.act(first.token, action, bounds));
    assert.equal(session.blocked, true);
    assert.equal(calls.length, 2);
  }
});

test('click and secondary dispatch through bound App and refresh observation', async () => {
  const { session, calls } = fixture();
  let observation = await session.observe(bounds);
  observation = await session.act(observation.token, { type: 'click', x: 639, y: 479 }, bounds);
  await session.act(observation.token, { type: 'secondary', index: 2, action: 'Press' }, bounds);
  assert.deepEqual(calls[2], ['click', [639, 479]]);
  assert.deepEqual(calls[5], ['secondary', 2, 'Press']);
});

test('requires explicit valid dimensions before input', async () => {
  const { session, calls } = fixture();
  const first = await session.observe(bounds);
  await assert.rejects(session.act(first.token, { type: 'key', key: 'w' }), /dimensions/);
  assert.equal(calls.length, 2);
});

test('post-input AX failure blocks all later input', async () => {
  let axCalls = 0;
  const { session, calls } = fixture({ async getAXState(options) {
    calls.push(['ax', options]);
    if (++axCalls === 2) throw Error('AX failed');
    return { text: 'unclassified' };
  } });
  const first = await session.observe(bounds);
  await assert.rejects(session.act(first.token, { type: 'key', key: 'Up' }, bounds), /AX failed/);
  assert.deepEqual(calls.slice(2), [['key', 'Up', { durationMs: 100 }],
    ['screenshot', { emit: false }], ['ax', { disableDiffing: true }]]);
  await assert.rejects(session.act(first.token, { type: 'key', key: 'Up' }, bounds), /Session blocked/);
  assert.equal(calls.length, 5);
});
