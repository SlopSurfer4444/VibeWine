import { spawn } from 'node:child_process';
import { performance } from 'node:perf_hooks';
import { StringDecoder } from 'node:string_decoder';

const OUTPUT_LIMIT = 128 * 1024;
const TERMINATION_GRACE_MS = 1500;

function failure(code) {
  const error = new Error(`Timed executor ${code}; input cleanup remains unknown`);
  error.code = code;
  error.cleanupStatus = 'unknown';
  return error;
}

function failureReceipt(record) {
  const receipt = {};
  const fields = ['status', 'error', 'neutralError', 'traceStopError', 'neutralAcknowledged',
    'traceStopped', 'producerQuiescenceVerified', 'overflow', 'traceGeneration',
    'traceIdentity', 'evidence', 'presentationEvidence', 'displayedMetricsStatus'];
  for (const field of fields) {
    if (Object.hasOwn(record, field) && (record[field] === null || ['string', 'boolean', 'number'].includes(typeof record[field]))) {
      receipt[field] = record[field];
    }
  }
  if (record.identity && typeof record.identity === 'object') {
    receipt.identity = {};
    for (const field of ['processKey', 'candidateId']) {
      if (typeof record.identity[field] === 'string') receipt.identity[field] = record.identity[field];
    }
  }
  const scalars = (value, names) => {
    if (!value || typeof value !== 'object' || Array.isArray(value)) return undefined;
    const result = {};
    for (const name of names) {
      const item = value[name];
      if (item === null || typeof item === 'boolean' || typeof item === 'number' && Number.isFinite(item) ||
          typeof item === 'string' && item.length <= 512) result[name] = item;
    }
    return result;
  };
  // Keep the actual bounded clock observations needed to reproduce a rejected
  // native window. They are diagnostic evidence, never an accepted capture.
  if (Array.isArray(record.presentationClockSamples) && record.presentationClockSamples.length <= 2) {
    receipt.presentationClockSamples = record.presentationClockSamples.map(envelope => ({
      ...scalars(envelope, ['collector_sha256', 'measurement_id']),
      clock_sample: scalars(envelope?.clock_sample, ['kind', 'schema', 'clock_domain', 'clock_api',
        'monotonic_clock_domain', 'monotonic_clock_api', 'sample_id', 'pid', 'ca_lower_seconds', 'ca_upper_seconds',
        'native_monotonic_ns', 'bracket_ns', 'native_monotonic_resolution_ns', 'mach_ticks', 'timebase_numer', 'timebase_denom']),
    }));
  }
  if (Array.isArray(record.presentationClockInvocationBounds) && record.presentationClockInvocationBounds.length <= 2) {
    receipt.presentationClockInvocationBounds = record.presentationClockInvocationBounds.map(value =>
      scalars(value, ['lowerMonotonicMs', 'upperMonotonicMs']));
  }
  const observed = record.nativePresentationWindow;
  if (observed && typeof observed === 'object' && !Array.isArray(observed)) {
    receipt.nativePresentationWindow = scalars(observed, ['schema', 'accepted', 'scenario', 'measurement_id',
      'collector_sha256', 'generation', 'decoder_identity', 'presentation_count', 'presentations_per_fixed_second']);
    if (Array.isArray(observed.rejection_reasons) && observed.rejection_reasons.length <= 128 &&
        observed.rejection_reasons.every(value => typeof value === 'string' && value.length <= 256)) {
      receipt.nativePresentationWindow.rejection_reasons = [...observed.rejection_reasons];
    }
    receipt.nativePresentationWindow.window = scalars(observed.window, ['clock_domain', 'start_seconds',
      'end_seconds', 'duration_seconds', 'begin_uncertainty_ns', 'end_uncertainty_ns']);
    const expected = scalars(observed.expected, ['swapchain', 'revision']);
    if (expected) {
      expected.dimensions = scalars(observed.expected?.dimensions, ['width', 'height']);
      receipt.nativePresentationWindow.expected = expected;
    }
  }
  return receipt;
}

function containsSecret(value, secret) {
  if (!secret) return false;
  const pending = [value];
  while (pending.length) {
    const item = pending.pop();
    if (typeof item === 'string' && item.includes(secret)) return true;
    if (item !== null && typeof item === 'object') {
      for (const [key, child] of Object.entries(item)) {
        if (key.includes(secret)) return true;
        pending.push(child);
      }
    }
  }
  return false;
}

// Each capture gets a fresh worker. The immutable admission wins over caller
// fields. Neither the AbortSignal nor the parent's monotonic deadline crosses
// the process boundary; the worker receives only its remaining time budget.
export function createTimedExecutor({ python, workerPath, admission }) {
  if (typeof python !== 'string' || !python || typeof workerPath !== 'string' || !workerPath ||
      !admission || typeof admission !== 'object' || Array.isArray(admission)) {
    throw new Error('Python executable, worker path and immutable admission required');
  }
  const admitted = JSON.parse(JSON.stringify(admission));
  if (!Number.isInteger(admitted.traceGeneration) || admitted.traceGeneration < 1 || admitted.traceGeneration > 0xffffffff) {
    throw new Error('Initial trace generation must be a nonzero uint32');
  }
  let nextGeneration = admitted.traceGeneration;
  const secret = typeof admitted.controllerToken === 'string' ? admitted.controllerToken : '';
  let busy = false;

  return Object.freeze({
    async run(args) {
      if (busy) throw failure('concurrent-run');
      if (nextGeneration > 0xffffffff) throw failure('trace-generation-exhausted');
      // Burn admission before any attempted run, including validation/spawn
      // failure. Native capture generations are never safe to retry/reuse.
      const traceGeneration = nextGeneration++;
      if (!args || typeof args !== 'object' || !Number.isFinite(args.deadline)) throw failure('invalid-deadline');
      const { signal, deadline, ...runFields } = args;
      if (signal != null && (typeof signal.addEventListener !== 'function' || typeof signal.removeEventListener !== 'function')) {
        throw failure('invalid-signal');
      }
      if (signal?.aborted) throw failure('cancelled-before-spawn');
      if (deadline <= performance.now()) throw failure('deadline-before-spawn');
      // Serialize before launching: bad/cyclic arguments must never start input.
      const requestFields = JSON.parse(JSON.stringify({ ...runFields, ...admitted, traceGeneration }));
      // A native presentation window is separately admitted for a stationary
      // capture. Never infer moving-route clock alignment or accept a caller's
      // unadmitted replacement merely because the field was absent in admission.
      if (requestFields.kind !== 'stationary' || !admitted.presentationWindow) delete requestFields.presentationWindow;
      delete requestFields.signal;
      delete requestFields.deadline;
      busy = true;
      try {
        return await new Promise((resolve, reject) => {
          const parentBeforeSpawn = performance.now();
          let child;
          try { child = spawn(python, [workerPath], { stdio: ['pipe', 'pipe', 'pipe'], windowsHide: true }); }
          catch { reject(failure('spawn-error')); return; }
          let outputBytes = 0, pending = '', mapping, result, stopped, settled = false, closed = false;
          const decoder = new StringDecoder('utf8');
          let terminationTimer;
          const deadlineTimer = setTimeout(() => stop('deadline-exhausted'), Math.max(0, deadline - performance.now()));
          const onAbort = () => stop('cancelled');

          function finish(error, value) {
            if (settled) return;
            settled = true;
            clearTimeout(deadlineTimer);
            clearTimeout(terminationTimer);
            signal?.removeEventListener('abort', onAbort);
            if (error) reject(error); else resolve(value);
          }
          function stop(code, workerResult) {
            if (stopped || settled) return;
            stopped = failure(code);
            if (workerResult) {
              stopped.workerResult = workerResult;
              if (workerResult.neutralAcknowledged === true && workerResult.traceStopped === true &&
                  workerResult.producerQuiescenceVerified === true) {
                stopped.cleanupStatus = 'verified';
                stopped.message = `Timed executor ${code}; worker acknowledged neutral input and trace quiescence`;
              }
            }
            child.stdin.destroy();
            if (closed || !child.pid) { finish(stopped); return; }
            try { child.kill('SIGTERM'); } catch { /* Still unknown. */ }
            terminationTimer = setTimeout(() => {
              try { child.kill('SIGKILL'); } catch { /* Still unknown. */ }
              finish(stopped);
            }, TERMINATION_GRACE_MS);
          }
          function consume(chunk, stdout) {
            if (stopped || settled) return;
            outputBytes += chunk.length;
            if (outputBytes > OUTPUT_LIMIT) { stop('output-limit'); return; }
            if (!stdout) return; // Never log or expose stderr: it may contain a token.
            pending += decoder.write(chunk);
            let newline;
            while (!stopped && (newline = pending.indexOf('\n')) !== -1) {
              const line = pending.slice(0, newline);
              pending = pending.slice(newline + 1);
              let record;
              try { record = JSON.parse(line); } catch { stop('invalid-json'); return; }
              if (!record || typeof record !== 'object' || Array.isArray(record)) { stop('invalid-protocol'); return; }
              if (!mapping) {
                if (record.event !== 'ready' || !Number.isFinite(record.childReadyMonotonicMs) || record.childReadyMonotonicMs < 0) {
                  stop('invalid-ready'); return;
                }
                const parentReadyReceived = performance.now();
                mapping = { parentBeforeSpawn, parentReadyReceived, childReadyMonotonicMs: record.childReadyMonotonicMs,
                  offsetLowerMs: parentBeforeSpawn - record.childReadyMonotonicMs,
                  offsetUpperMs: parentReadyReceived - record.childReadyMonotonicMs };
                const timeoutMs = deadline - performance.now();
                if (timeoutMs <= 0) { stop('deadline-exhausted'); return; }
                child.stdin.end(`${JSON.stringify({ ...requestFields, timeoutMs })}\n`);
              } else {
                if (result || record.event !== 'result' || !['complete', 'failed'].includes(record.status)) {
                  stop('invalid-result'); return;
                }
                // No arbitrary worker text can disclose the controller capability.
                if (Object.hasOwn(record, 'controllerToken') || containsSecret(record, secret)) {
                  stop('secret-in-result'); return;
                }
                if (record.status === 'failed') { stop('worker-failed', failureReceipt(record)); return; }
                if (record.traceGeneration !== traceGeneration) { stop('trace-generation-mismatch'); return; }
                const { startedMonotonicMs: start, endedMonotonicMs: end, durationMonotonicMs: duration } = record;
                if (![start, end, duration].every(Number.isFinite) || start < mapping.childReadyMonotonicMs || end < start ||
                    duration < 0 || Math.abs(end - start - duration) > 0.01) { stop('invalid-clock'); return; }
                const { startedAt: ignoredStart, endedAt: ignoredEnd, ...childResult } = record;
                result = { ...childResult, clock: 'anchored-child-monotonic',
                  startedAtLower: start + mapping.offsetLowerMs, startedAtUpper: start + mapping.offsetUpperMs,
                  endedAtLower: end + mapping.offsetLowerMs, endedAtUpper: end + mapping.offsetUpperMs,
                  clockMapping: mapping };
              }
            }
          }

          child.stdout.on('data', chunk => consume(chunk, true));
          child.stderr.on('data', chunk => consume(chunk, false));
          child.stdin.on('error', () => stop('stdin-error'));
          child.on('error', () => stop('spawn-error'));
          child.on('close', (code, exitSignal) => {
            closed = true;
            if (stopped) { finish(stopped); return; }
            pending += decoder.end();
            if (pending || !mapping || !result || code !== 0 || exitSignal) { finish(failure('unexpected-worker-exit')); return; }
            if (performance.now() > deadline || signal?.aborted) { finish(failure('deadline-or-cancellation')); return; }
            finish(null, result);
          });
          signal?.addEventListener('abort', onAbort, { once: true });
          if (signal?.aborted) onAbort();
        });
      } finally { busy = false; }
    },
  });
}
