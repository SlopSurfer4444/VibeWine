import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, writeFile, rm, readFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { performance } from 'node:perf_hooks';
import { setTimeout as delay } from 'node:timers/promises';
import { createTimedExecutor } from './timed-executor.mjs';

// This is a real child process using only Node stdlib. It substitutes for the
// Python worker protocol; these checks neither launch a game nor send input.
const worker = `
import { performance } from 'node:perf_hooks';
import { writeFileSync } from 'node:fs';
const ready = performance.now();
process.stdout.write(JSON.stringify({event:'ready',childReadyMonotonicMs:ready})+'\\n');
let data='';
for await (const chunk of process.stdin) data+=chunk;
const request=JSON.parse(data);
writeFileSync(request.outputDirectory+'/request.json', JSON.stringify(request));
writeFileSync(request.outputDirectory+'/pid', String(process.pid));
const mode=request.fixtureMode;
if(mode==='hang' || mode==='ignore-term') {
  if(mode==='ignore-term') process.on('SIGTERM',()=>{});
  setInterval(()=>{},1000);
} else if(mode==='bad-json') process.stdout.write('not json\\n');
else if(mode==='stdout-limit') process.stdout.write('x'.repeat(128*1024));
else if(mode==='stderr-limit') process.stderr.write('x'.repeat(128*1024));
else if(mode==='dead') process.exit(7);
else if(mode==='eof') process.exit(0);
else {
  const start=performance.now(), end=start+1;
  const result={event:'result',status:mode.startsWith('failed')?'failed':'complete',
    traceGeneration:request.traceGeneration,
    kind:request.kind,identity:request.identity,routeId:request.route?.id,
    valid:true,overflow:false,guiObservations:0,toolRoundTrips:0,
    startedMonotonicMs:start,endedMonotonicMs:end,durationMonotonicMs:1,
    neutralAcknowledged:true,maxInputLeaseMs:0,inputAdmissionId:request.inputAdmissionId,
    telemetryAdmissionId:request.telemetryAdmissionId,sceneKey:request.sceneKey,
    settingsKey:request.settingsKey,evidence:'fixture: проверено',displayedMetricsStatus:'inconclusive'};
  if(mode.startsWith('failed-')) Object.assign(result, {
    error:'fixture decode failed',neutralError:null,traceStopError:null,
    neutralAcknowledged:mode==='failed-cleanup',traceStopped:true,producerQuiescenceVerified:true,
    traceIdentity:'1:2',rawRequest:'must never be returned',traceSummary:{unapproved:'field'}});
  if(mode==='secret') result.evidence=request.controllerToken;
  if(mode==='bad-clock') result.durationMonotonicMs=5;
  process.stdout.write(JSON.stringify(result)+'\\n');
  if(mode==='extra-record') process.stdout.write('{}\\n');
  if(mode==='result-then-fail') process.exitCode=1;
}
`;

async function fixture(t, mode = 'ok') {
  const directory = await mkdtemp(join(tmpdir(), 'timed-executor-test-'));
  t.after(() => rm(directory, { recursive: true, force: true }));
  const workerPath = join(directory, 'worker.mjs');
  await writeFile(workerPath, worker);
  const admission = { controllerToken: 'fixture-secret-8763', windowsPid: 44,
    traceGeneration: 11,
    outputDirectory: directory, fixtureMode: mode };
  const executor = createTimedExecutor({ python: process.execPath, workerPath, admission });
  const args = { deadline: performance.now() + 3000, kind: 'stationary', durationMs: 1,
    identity: { processKey: 'fixture-process', candidateId: 'fixture-candidate' },
    inputAdmissionId: 'fixture-input', telemetryAdmissionId: 'fixture-telemetry',
    sceneKey: 'fixture-scene', settingsKey: 'fixture-settings' };
  return { executor, args, admission, directory };
}

test('real process handshake yields honest monotonic mapping bounds and immutable admission', async t => {
  const f = await fixture(t);
  f.admission.windowsPid = 99;
  const result = await f.executor.run({ ...f.args, controllerToken: 'override', windowsPid: 77,
    signal: new AbortController().signal });
  const request = JSON.parse(await readFile(join(f.directory, 'request.json'), 'utf8'));
  assert.equal(request.windowsPid, 44);
  assert.equal(request.controllerToken, 'fixture-secret-8763');
  assert.ok(!('signal' in request) && !('deadline' in request));
  assert.ok(request.timeoutMs > 0 && request.timeoutMs < 3000);
  assert.equal(result.clock, 'anchored-child-monotonic');
  assert.ok(!('startedAt' in result) && !('endedAt' in result));
  const map = result.clockMapping;
  assert.equal(map.offsetLowerMs, map.parentBeforeSpawn - map.childReadyMonotonicMs);
  assert.equal(map.offsetUpperMs, map.parentReadyReceived - map.childReadyMonotonicMs);
  assert.ok(map.offsetLowerMs <= map.offsetUpperMs);
  assert.equal(result.startedAtLower, result.startedMonotonicMs + map.offsetLowerMs);
  assert.equal(result.endedAtUpper, result.endedMonotonicMs + map.offsetUpperMs);
  assert.ok(result.startedAtLower <= result.startedAtUpper);
  assert.equal(result.durationMonotonicMs, 1);
  assert.equal(result.displayedMetricsStatus, 'inconclusive');
  assert.match(result.evidence, /проверено/);
  assert.ok(!JSON.stringify(result).includes('fixture-secret'));
});

for (const [mode, code] of [
  ['bad-json', 'invalid-json'], ['stdout-limit', 'output-limit'], ['stderr-limit', 'output-limit'],
  ['dead', 'unexpected-worker-exit'], ['eof', 'unexpected-worker-exit'], ['failed', 'worker-failed'],
  ['secret', 'secret-in-result'], ['bad-clock', 'invalid-clock'], ['extra-record', 'invalid-result'],
  ['result-then-fail', 'unexpected-worker-exit'],
]) test(`rejects ${mode} without reporting successful cleanup or leaking capability`, async t => {
  const f = await fixture(t, mode);
  await assert.rejects(f.executor.run(f.args), error => {
    assert.equal(error.code, code);
    assert.equal(error.cleanupStatus, 'unknown');
    assert.ok(!String(error).includes('fixture-secret'));
    return true;
  });
});

test('deadline terminates a stalled worker', async t => {
  const f = await fixture(t, 'hang');
  const before = performance.now();
  await assert.rejects(f.executor.run({ ...f.args, deadline: before + 120 }), error => error.code === 'deadline-exhausted');
  assert.ok(performance.now() - before < 1900);
});

test('cancellation escalates ignored SIGTERM within 1500ms and remains unknown', async t => {
  const f = await fixture(t, 'ignore-term');
  const abort = new AbortController();
  const running = f.executor.run({ ...f.args, deadline: performance.now() + 5000, signal: abort.signal });
  let pid;
  for (let n = 0; n < 100 && !pid; n++) {
    try { pid = Number(await readFile(join(f.directory, 'pid'), 'utf8')); } catch { await delay(10); }
  }
  assert.ok(pid, 'fixture must reach request before cancellation');
  const cancelledAt = performance.now();
  abort.abort();
  await assert.rejects(running, error => error.code === 'cancelled' && error.cleanupStatus === 'unknown');
  assert.ok(performance.now() - cancelledAt < 1800);
  await delay(50);
  assert.throws(() => process.kill(pid, 0), { code: 'ESRCH' });
});

test('pre-cancelled and expired requests do not spawn a worker', async t => {
  const f = await fixture(t);
  const abort = new AbortController(); abort.abort();
  await assert.rejects(f.executor.run({ ...f.args, signal: abort.signal }), { code: 'cancelled-before-spawn' });
  await assert.rejects(f.executor.run({ ...f.args, deadline: performance.now() - 1 }), { code: 'deadline-before-spawn' });
  await assert.rejects(readFile(join(f.directory, 'pid')), { code: 'ENOENT' });
});

test('missing executable fails with a bounded sanitized error', async () => {
  const executor = createTimedExecutor({ python: '/nonexistent/python-fixture', workerPath: '/unused', admission: { traceGeneration: 1 } });
  await assert.rejects(executor.run({ deadline: performance.now() + 1000 }), { code: 'spawn-error' });
});

test('deadline also covers workers that never send ready', async t => {
  const f = await fixture(t);
  await writeFile(join(f.directory, 'worker.mjs'), 'setInterval(() => {}, 1000);');
  await assert.rejects(f.executor.run({ ...f.args, deadline: performance.now() + 120 }), { code: 'deadline-exhausted' });
});

test('result without ready cannot be admitted as a capture', async t => {
  const f = await fixture(t);
  await writeFile(join(f.directory, 'worker.mjs'), `process.stdout.write('{"event":"result","status":"complete"}\\n');`);
  await assert.rejects(f.executor.run(f.args), { code: 'invalid-ready' });
});

test('two captures consume separate immutable generations', async t => {
  const f = await fixture(t);
  f.admission.traceGeneration = 99;
  const first = await f.executor.run({ ...f.args, traceGeneration: 77 });
  const second = await f.executor.run({ ...f.args, traceGeneration: 77 });
  assert.equal(first.traceGeneration, 11);
  assert.equal(second.traceGeneration, 12);
  const request = JSON.parse(await readFile(join(f.directory, 'request.json'), 'utf8'));
  assert.equal(request.traceGeneration, 12);
});

test('failed capture generation is burned before the next run', async t => {
  const f = await fixture(t, 'bad-json');
  await assert.rejects(f.executor.run(f.args), { code: 'invalid-json' });
  const request = JSON.parse(await readFile(join(f.directory, 'request.json'), 'utf8'));
  assert.equal(request.traceGeneration, 11);
  await writeFile(join(f.directory, 'worker.mjs'), worker.replace('const mode=request.fixtureMode;', "const mode='ok';"));
  const result = await f.executor.run(f.args);
  assert.equal(result.traceGeneration, 12);
});

test('uint32 exhaustion is terminal before another worker spawn', async t => {
  const f = await fixture(t);
  const executor = createTimedExecutor({ python: process.execPath, workerPath: join(f.directory, 'worker.mjs'),
    admission: { ...f.admission, traceGeneration: 0xffffffff } });
  assert.equal((await executor.run(f.args)).traceGeneration, 0xffffffff);
  await assert.rejects(executor.run(f.args), { code: 'trace-generation-exhausted' });
  const request = JSON.parse(await readFile(join(f.directory, 'request.json'), 'utf8'));
  assert.equal(request.traceGeneration, 0xffffffff);
});

for (const mode of ['failed-cleanup', 'failed-uncertain']) {
  test(`preserves sanitized ${mode} evidence while rejecting timed capture`, async t => {
    const f = await fixture(t, mode);
    await assert.rejects(f.executor.run(f.args), error => {
      assert.equal(error.code, 'worker-failed');
      assert.equal(error.cleanupStatus, mode === 'failed-cleanup' ? 'verified' : 'unknown');
      assert.deepEqual(error.workerResult, {
        status: 'failed', error: 'fixture decode failed', neutralError: null, traceStopError: null,
        neutralAcknowledged: mode === 'failed-cleanup', traceStopped: true, producerQuiescenceVerified: true,
        overflow: false, traceGeneration: 11, traceIdentity: '1:2', evidence: 'fixture: проверено',
        displayedMetricsStatus: 'inconclusive', identity: f.args.identity,
      });
      assert.ok(!JSON.stringify(error.workerResult).includes('fixture-secret'));
      assert.ok(!('rawRequest' in error.workerResult) && !('traceSummary' in error.workerResult));
      return true;
    });
  });
}

test('native presentation admission is immutable, stationary only and never supplied by run fields', async t => {
  const f = await fixture(t);
  const presentationWindow = { fixture: 'admitted-clock-and-configuration' };
  const executor = createTimedExecutor({ python: process.execPath, workerPath: join(f.directory, 'worker.mjs'),
    admission: { ...f.admission, presentationWindow } });
  await executor.run({ ...f.args, presentationWindow: { fixture: 'unadmitted' } });
  let request = JSON.parse(await readFile(join(f.directory, 'request.json'), 'utf8'));
  assert.deepEqual(request.presentationWindow, presentationWindow);
  await executor.run({ ...f.args, kind: 'route', presentationWindow: { fixture: 'unadmitted' } });
  request = JSON.parse(await readFile(join(f.directory, 'request.json'), 'utf8'));
  assert.ok(!('presentationWindow' in request));
  await f.executor.run({ ...f.args, presentationWindow });
  request = JSON.parse(await readFile(join(f.directory, 'request.json'), 'utf8'));
  assert.ok(!('presentationWindow' in request));
});

test('rejected native window retains bounded clock evidence without arbitrary worker fields', async t => {
  const f = await fixture(t, 'failed-cleanup');
  const observations = {
    presentationClockSamples: [{collector_sha256:'a'.repeat(64), measurement_id:'measurement', arbitrary:'discard',
      clock_sample:{kind:'clock_sample',schema:1,ca_lower_seconds:10,ca_upper_seconds:10.000001,rawRequest:'discard'}}],
    presentationClockInvocationBounds:[{lowerMonotonicMs:100,upperMonotonicMs:101,rawRequest:'discard'}],
    nativePresentationWindow:{schema:1,accepted:false,scenario:'stationary',generation:11,decoder_identity:'1:2',
      rejection_reasons:['feedback_does_not_span_both_window_boundaries'],presentation_count:null,
      window:{clock_domain:'ca_host_seconds',start_seconds:10,end_seconds:11,duration_seconds:1,rawRequest:'discard'},
      expected:{swapchain:7,revision:2,dimensions:{width:640,height:480,rawRequest:'discard'}},rawRequest:'discard'},
  };
  await writeFile(join(f.directory, 'worker.mjs'), worker.replace("if(mode==='secret')", `Object.assign(result, ${JSON.stringify(observations)});\n  if(mode==='secret')`));
  await assert.rejects(f.executor.run(f.args), error => {
    assert.equal(error.code, 'worker-failed');
    assert.equal(error.cleanupStatus, 'verified');
    const r=error.workerResult;
    assert.equal(r.nativePresentationWindow.accepted, false);
    assert.deepEqual(r.nativePresentationWindow.rejection_reasons, observations.nativePresentationWindow.rejection_reasons);
    assert.equal(r.presentationClockSamples[0].clock_sample.ca_upper_seconds,10.000001);
    assert.deepEqual(r.presentationClockInvocationBounds,[{lowerMonotonicMs:100,upperMonotonicMs:101}]);
    assert.ok(!JSON.stringify(r).includes('discard'));
    return true;
  });
});
