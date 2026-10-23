#!/usr/bin/env python3
"""One bounded local trace/controller interval. No game launch or GUI APIs."""
import fcntl
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import secrets
import signal
import stat
import subprocess
import sys
import threading
import time

spec = importlib.util.spec_from_file_location('virtual_controller', Path(__file__).with_name('virtual-controller.py'))
vc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vc)
MAX_DECODE_BYTES = 12 * 1024 * 1024
MAX_DERIVATIVE_BYTES = 12 * 1024 * 1024


class CaptureError(Exception):
    pass


def require(value, message):
    if not value:
        raise CaptureError(message)


def number(value, low, high, name):
    require(type(value) in (int, float) and math.isfinite(value) and low <= value <= high, f'invalid {name}')
    return value


def execute(request, cancelled=None):
    cancelled = cancelled or threading.Event()
    began = time.monotonic()
    deadline = began + number(request.get('timeoutMs'), 1, 180000, 'timeoutMs') / 1000
    cleanup_ms = number(request.get('cleanupTimeoutMs', 1000), 100, 1000, 'cleanupTimeoutMs')
    duration = number(request.get('durationMs'), 1, 60000, 'durationMs') / 1000
    kind = request.get('kind')
    require(kind in ('stationary', 'route'), 'invalid capture kind')
    for field in ('inputAdmissionId', 'telemetryAdmissionId', 'sceneKey', 'settingsKey'):
        require(isinstance(request.get(field), str) and 0 < len(request[field]) <= 4096, f'missing {field}')
    identity = request.get('identity')
    require(isinstance(identity, dict) and all(isinstance(identity.get(k), str) and identity[k] for k in ('processKey', 'candidateId')), 'exact admitted process generation/candidate required')
    require(request.get('candidateId') == identity['candidateId'], 'candidate identity mismatch')
    token = request.get('controllerToken')
    require(isinstance(token, str) and vc.TOKEN_RE.fullmatch(token), 'controller token admission required')
    pid = vc.integer(request.get('windowsPid'), 1, 0xffffffff, 'Windows PID')
    exe = vc.windows_path(request.get('exe'), 'executable')
    generation = vc.integer(request.get('traceGeneration'), 1, 0xffffffff, 'trace generation')
    planned = vc.integer(request.get('plannedRecords'), 1, 131072, 'record budget')
    reserve = vc.integer(request.get('tailReserve'), 0, 131072, 'tail reserve')
    expiry = vc.integer(request.get('inputExpiryMs'), 1, 1000, 'input expiry')
    tolerance = number(request.get('scheduleToleranceMs', 5), 0, 20, 'scheduleToleranceMs') / 1000
    trace_file = request.get('traceFile')
    trace_identity = request.get('traceIdentity')
    cli = request.get('traceCli')
    require(all(isinstance(v, str) and os.path.isabs(v) for v in (trace_file, cli, request.get('controllerDirectory'), request.get('outputDirectory'))), 'absolute admitted paths required')
    presentation = request.get('presentationWindow')
    presentation_samples = []
    if presentation is not None:
        require(kind == 'stationary' and type(presentation) is dict and set(presentation) ==
                {'collectorSha256', 'measurementId', 'swapchain', 'width', 'height', 'revision'},
                'closed stationary presentation admission required')
        require(isinstance(presentation['collectorSha256'], str) and
                len(presentation['collectorSha256']) == 64 and
                all(c in '0123456789abcdef' for c in presentation['collectorSha256']), 'collector hash required')
        require(hashlib.sha256(Path(cli).read_bytes()).hexdigest() == presentation['collectorSha256'], 'actual presentation collector changed')
        for key in ('swapchain', 'width', 'height'):
            vc.integer(presentation[key], 1, 32768 if key in ('width', 'height') else 0xffffffffffffffff, key)
        vc.integer(presentation['revision'], 0, 0xffffffffffffffff, 'configuration revision')
        import uuid
        require(type(presentation['measurementId']) is str and str(uuid.UUID(presentation['measurementId'])) == presentation['measurementId'] and
                uuid.UUID(presentation['measurementId']).int != 0,
                'fresh presentation measurement UUID required')
    info = os.stat(trace_file, follow_symlinks=False)
    require(stat.S_ISREG(info.st_mode) and info.st_nlink == 1 and info.st_uid == os.getuid(), 'unsafe trace file')
    require(trace_identity == f'{info.st_dev}:{info.st_ino}', 'trace dev:inode identity mismatch')
    steps = []
    if kind == 'route':
        route = request.get('route')
        require(isinstance(route, dict) and route.get('id') and route.get('sceneKey') == request['sceneKey'] and route.get('settingsKey') == request['settingsKey'] and route.get('durationMs') == request['durationMs'], 'exact preadmitted route required')
        require(isinstance(route.get('steps'), list) and 1 <= len(route['steps']) <= 64, 'finite route steps required')
        previous_end = 0
        for step in route['steps']:
            offset = number(step.get('offsetMs'), previous_end, request['durationMs'], 'route offset')
            hold = vc.integer(step.get('durationMs'), 1, min(500, expiry), 'route hold')
            require(offset + hold <= request['durationMs'], 'route extends beyond fixed interval')
            state = step.get('state')
            require(isinstance(state, dict) and state and all(k in ('lx', 'ly') or v == 0 and k in ('buttons', 'lt', 'rt', 'rx', 'ry') for k, v in state.items()), 'DS3 route allows only admitted left-stick movement')
            vc.encode_snapshot(token, 1, hold, pid=pid, flags=vc.STATE, **state)
            require(any(state.values()), 'neutral route action')
            steps.append((offset / 1000, hold / 1000, state))
            previous_end = offset + hold
    else:
        require(request.get('route') is None, 'stationary interval cannot carry input route')
    output_fd = vc.open_directory(request['outputDirectory'])
    os.close(output_fd)
    result = dict(identity=identity, kind=kind, routeId=request.get('route', {}).get('id') if kind == 'route' else None,
                  valid=False, overflow=False, guiObservations=0, toolRoundTrips=0,
                  inputAdmissionId=request['inputAdmissionId'], telemetryAdmissionId=request['telemetryAdmissionId'],
                  sceneKey=request['sceneKey'], settingsKey=request['settingsKey'],
                  neutralAcknowledged=False, maxInputLeaseMs=0, displayedMetricsStatus='inconclusive',
                  displayedMetricsReason='No independent displayed-frame observer was exercised by this executor.',
                  traceGeneration=generation, traceIdentity=trace_identity, schedule=[],
                  traceStopped=False, producerQuiescenceVerified=False,
                  fixedWindowMetricsStatus='inconclusive-unisolated-trace-window',
                  traceEnvelopeMonotonicBounds={})
    trace_attempted = False
    started = ended = None
    primary = neutral_error = trace_error = None
    decoded_path = None
    cleanup_deadline = None
    def check():
        require(not cancelled.is_set(), 'capture cancelled')
        require(time.monotonic() < deadline, 'capture deadline exhausted')
    def wait_until(target):
        while time.monotonic() < target:
            check()
            cancelled.wait(min(0.01, target - time.monotonic()))
        check()
    def cli_call(verb, *args, cleanup=False):
        if not cleanup:
            check()
        limit = cleanup_deadline if cleanup else deadline
        argv = [cli, verb] if verb == 'clock-sample' else [cli, verb, trace_file, *map(str, args)]
        child = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            while True:
                try:
                    stdout, stderr = child.communicate(timeout=min(0.05, max(0.001, limit - time.monotonic())))
                    break
                except subprocess.TimeoutExpired:
                    require(cleanup or not cancelled.is_set(), 'capture cancelled during trace command')
                    require(time.monotonic() < limit, 'trace command deadline exhausted')
            require(len(stdout) <= MAX_DECODE_BYTES and len(stderr) <= 65536, 'trace CLI output budget exceeded')
            require(child.returncode == 0 or verb == 'decode' and child.returncode == 3, f'trace {verb} failed ({child.returncode}): {stderr.decode(errors="replace")[:2048]}')
            return stdout
        finally:
            if child.poll() is None:
                child.kill()
                child.communicate()
    def one(raw):
        rows = raw.decode('utf-8').splitlines()
        require(len(rows) == 1, 'trace CLI response is not one native JSON object')
        value = json.loads(rows[0])
        require(value.get('identity') == trace_identity, 'trace CLI identity changed')
        return value
    def sample_presentation_clock():
        require(hashlib.sha256(Path(cli).read_bytes()).hexdigest() == presentation['collectorSha256'], 'actual presentation collector changed')
        lower = time.monotonic() * 1000
        raw = cli_call('clock-sample')
        upper = time.monotonic() * 1000
        require(len(raw) <= 8192 and len(raw.splitlines()) == 1, 'bounded native clock sample required')
        envelope = dict(clock_sample=json.loads(raw), collector_sha256=presentation['collectorSha256'],
                        measurement_id=presentation['measurementId'])
        # Import the maintained parser, not a second feedback implementation.
        directory = str(Path(__file__).resolve().parents[1] / 'telemetry')
        if directory not in sys.path:
            sys.path.insert(0, directory)
        from fixed_window_analysis import validate_clock_sample
        validate_clock_sample(envelope, expected_collector_sha256=presentation['collectorSha256'],
                              measurement_id=presentation['measurementId'])
        require(hashlib.sha256(Path(cli).read_bytes()).hexdigest() == presentation['collectorSha256'], 'actual presentation collector changed')
        presentation_samples.append(envelope)
        result.setdefault('presentationClockInvocationBounds', []).append(dict(lowerMonotonicMs=lower, upperMonotonicMs=upper))
    with vc.locked_session(request['controllerDirectory']) as (directory, manifest):
        require(manifest['token'] == token and manifest['exe'] == exe and manifest['pid'] in (0, pid), 'controller session token/executable/PID mismatch')
        require(manifest['seq'] <= vc.MAX_SEQ - 3 * len(steps) - 8, 'controller sequence budget exhausted')
        def command(flags, lease=1, *, cleanup=False, command_deadline=None, publication_deadline=None, lease_end=None, publication_receipt=None, **state):
            if not cleanup:
                check()
            limit = min(cleanup_deadline, time.monotonic() + max(0.001, cleanup_deadline - time.monotonic() - 0.3)) if cleanup else min(deadline, command_deadline or deadline)
            timeout = max(1, min(500, math.floor((limit - time.monotonic()) * 1000)))
            options = {}
            if publication_deadline is not None: options['publication_not_after'] = publication_deadline
            if lease_end is not None: options['lease_end'] = lease_end
            if publication_receipt is not None: options['publication_receipt'] = publication_receipt
            receipt, published = vc._command_locked(directory, manifest, flags, lease, timeout,
                not_after=limit, fresh_lease=(flags != vc.RELEASE), cleanup_on_failure=False, **options, **state)
            require(receipt['receipt']['pid'] == pid, 'fresh receipt came from another Windows PID')
            require(manifest['token'] == token, 'controller token changed')
            return receipt, published
        try:
            inspected = one(cli_call('inspect'))
            require(inspected.get('protocol_version') == 2 and inspected.get('capture') == 0, 'trace already armed or wrong protocol')
            require(inspected.get('dropped') == 0 and inspected.get('remaining_capacity', 0) >= planned + reserve, 'trace overflow/capacity admission failed')
            # Neutral ARM supplies a fresh exact PID receipt before any active input.
            _, arm_started = command(vc.ARM, 1000)
            trace_attempted = True  # a killed arm subprocess may already have armed
            result['traceEnvelopeMonotonicBounds']['armInvocationStartMs'] = time.monotonic() * 1000
            armed = one(cli_call('arm', generation, trace_identity, planned, reserve))
            require(armed.get('operation') == 'arm' and armed.get('generation') == generation and armed.get('capture') == generation, 'trace arm receipt mismatch')
            result['traceEnvelopeMonotonicBounds']['armReceiptMs'] = time.monotonic() * 1000
            if presentation is not None:
                # Arm first and retain pre-roll so native frame identities can
                # span the clock-defined window. No GUI/model call is made.
                wait_until(time.monotonic() + 0.25)
                sample_presentation_clock()
            started = time.monotonic()
            require(started + duration < deadline, 'insufficient whole interval deadline')
            for index, (offset, hold, state) in enumerate(steps):
                target, stop = started + offset, started + offset + hold
                if index:
                    wait_until(max(time.monotonic(), target - 0.2))
                    _, arm_started = command(vc.ARM, 1000, command_deadline=min(deadline, target + tolerance))
                wait_until(target)
                now = time.monotonic()
                require(now <= target + tolerance, 'scheduled route step is late; no state published')
                require(now < arm_started + 0.9, 'scheduled route ARM lease expired')
                remaining_ms = math.floor((stop - now) * 1000)
                require(remaining_ms >= 1, 'route hold already elapsed; no state published')
                lease = min(math.floor(hold * 1000), remaining_ms, expiry)
                result['maxInputLeaseMs'] = max(result['maxInputLeaseMs'], lease)
                publication_bounds = {}
                receipt, publication = command(vc.STATE, lease, command_deadline=stop,
                    publication_deadline=min(target + tolerance, stop), lease_end=stop, publication_receipt=publication_bounds, **state)
                require(target <= publication_bounds['beforeReplace'] <= publication_bounds['afterReplace'] <= target + tolerance, 'actual STATE replacement exceeded schedule tolerance')
                actual_packet = vc.SNAPSHOT.unpack(vc.read_file(directory, vc.STATE_FILE, limit=72, private=True))
                require(actual_packet[2] == receipt['receipt']['seq'], 'STATE snapshot changed before lease inspection')
                lease = actual_packet[3]
                require(time.monotonic() < stop, 'route receipt consumed its entire fixed hold')
                result['schedule'].append(dict(index=index, plannedOffsetMs=offset * 1000,
                    publicationOffsetMs=(publication_bounds['afterReplace'] - started) * 1000,
                    publicationBoundsMs=dict(lower=(publication_bounds['beforeReplace'] - started) * 1000, upper=(publication_bounds['afterReplace'] - started) * 1000), leaseMs=lease,
                    receiptSequence=receipt['receipt']['seq']))
                wait_until(stop)
                command(vc.RELEASE, 1)
            wait_until(started + duration)
            ended = time.monotonic()
            if kind == 'route':
                require(ended <= started + duration + tolerance, 'fixed route end deadline missed')
            if presentation is not None:
                # Later Presents drain callbacks beyond the fixed CA end. These
                # tails are excluded by the maintained native-window analyzer.
                wait_until(time.monotonic() + 0.25)
                sample_presentation_clock()
        except BaseException as exc:
            primary = str(exc) or type(exc).__name__
        finally:
            cleanup_deadline = time.monotonic() + cleanup_ms / 1000
            # Cleanup has its own finite budget and ignores cancellation, while
            # reader expiry remains independent if this process is killed.
            try:
                if manifest['pid']:
                    command(vc.RELEASE, 1, cleanup=True)
                    result['neutralAcknowledged'] = True
                else:
                    raise CaptureError('neutral ARM has no known PID for RELEASE')
            except BaseException as exc:
                neutral_error = str(exc) or type(exc).__name__
            if trace_attempted:
                try:
                    result['traceEnvelopeMonotonicBounds']['stopInvocationStartMs'] = time.monotonic() * 1000
                    stopped = one(cli_call('stop', generation, trace_identity, cleanup=True))
                    require(stopped.get('operation') == 'stop' and stopped.get('generation') == generation and stopped.get('capture') == 0 and stopped.get('producer_quiescence_verified') is True and stopped.get('structurally_complete') is True and stopped.get('admitted_writers') == 0, 'trace stop/quiescence unverified')
                    result['traceEnvelopeMonotonicBounds']['stopReceiptMs'] = time.monotonic() * 1000
                    result['traceStopped'] = result['producerQuiescenceVerified'] = True
                except BaseException as exc:
                    trace_error = str(exc) or type(exc).__name__
    if started is not None:
        result['startedMonotonicMs'] = started * 1000
        result['endedMonotonicMs'] = (ended or time.monotonic()) * 1000
        result['durationMonotonicMs'] = ((ended or started) - started) * 1000
    if not any((primary, neutral_error, trace_error)):
        try:
            raw = cli_call('decode', generation, trace_identity)
            rows = [json.loads(line) for line in raw.decode('utf-8').splitlines()]
            require(rows and rows[-1].get('kind') == 'summary', 'native decode summary missing')
            summary = rows[-1]
            result['overflow'] = bool(summary.get('dropped_before') or summary.get('dropped_after') or summary.get('uncommitted_reserved'))
            require(summary.get('identity') == trace_identity and summary.get('generation') == generation and summary.get('protocol_version') == 2, 'decode generation/identity mismatch')
            require(summary.get('events', 0) > 0 and not result['overflow'] and summary.get('inconclusive_window') is False and summary.get('counters_changed') is False and summary.get('structurally_complete') is True and summary.get('producer_quiescence_verified') is True and summary.get('boundary_rejected_total') == 0, 'trace capture incomplete/overflow/boundary rejection')
            name = f'capture-{generation}-{secrets.token_hex(8)}.ndjson'
            presentation_name = name.removesuffix('.ndjson') + '.presentation.json'
            presentation_raw = None
            if presentation is not None:
                # Preserve exact uint64 clock values before a JavaScript JSON
                # reader can round them on a long-uptime host. This sidecar is
                # the reproducible source for accepted and rejected windows.
                presentation_raw = (json.dumps(dict(schema=1, generation=generation, traceIdentity=trace_identity,
                    capture=name, admission=presentation, windowSeconds=duration, clockSamples=presentation_samples),
                    allow_nan=False) + '\n').encode('utf-8')
                require(len(presentation_raw) <= 32768, 'presentation evidence exceeds byte cap')
            outdir = vc.open_directory(request['outputDirectory'])
            output_lock = None
            try:
                output_lock = os.open('.timed-output.lock', os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600, dir_fd=outdir)
                vc.check_file(output_lock, '.timed-output.lock', private=True)
                fcntl.flock(output_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                entries = os.listdir(outdir)
                require(len(entries) <= 64, 'output directory item budget exceeded')
                used = 0
                for entry in entries:
                    if entry.startswith('capture-') and entry.endswith(('.ndjson', '.presentation.json')):
                        info = os.stat(entry, dir_fd=outdir, follow_symlinks=False)
                        require(stat.S_ISREG(info.st_mode) and info.st_nlink == 1 and info.st_uid == os.getuid(), 'unsafe existing capture derivative')
                        used += info.st_size
                require(used + len(raw) + len(presentation_raw or b'') <= MAX_DERIVATIVE_BYTES, 'capture derivative byte cap exhausted; retain held evidence and stop collection')
                fd = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600, dir_fd=outdir)
                with os.fdopen(fd, 'wb') as handle:
                    handle.write(raw)
                    handle.flush()
                    os.fsync(handle.fileno())
                if presentation_raw is not None:
                    fd = os.open(presentation_name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600, dir_fd=outdir)
                    with os.fdopen(fd, 'wb') as handle:
                        handle.write(presentation_raw)
                        handle.flush()
                        os.fsync(handle.fileno())
                    result['presentationEvidence'] = os.path.join(request['outputDirectory'], presentation_name)
            finally:
                if output_lock is not None:
                    os.close(output_lock)
                os.close(outdir)
            decoded_path = os.path.join(request['outputDirectory'], name)
            result['traceSummary'] = summary
            if presentation is not None:
                from fixed_window_analysis import analyze_fixed_window
                require(len(presentation_samples) == 2, 'paired native presentation samples missing')
                observed = analyze_fixed_window(rows, *presentation_samples, window_seconds=duration,
                    expected_swapchain=presentation['swapchain'],
                    expected_dimensions=dict(width=presentation['width'], height=presentation['height']),
                    expected_revision=presentation['revision'], expected_collector_sha256=presentation['collectorSha256'],
                    measurement_id=presentation['measurementId'], scenario='stationary')
                result['nativePresentationWindow'] = observed
                result['presentationClockSamples'] = presentation_samples
                require(observed['accepted'], 'native presentation window rejected: ' + ','.join(observed['rejection_reasons']))
                result['displayedMetricsStatus'] = 'accepted-native-presentation-window'
                result['displayedMetricsReason'] = 'Actual drawable feedback in an exact stationary CoreAnimation window; physical scanout and observer overhead remain unverified.'
                result['fixedWindowMetricsStatus'] = 'accepted-stationary-native-clock-window'
            result['valid'] = True
        except BaseException as exc:
            primary = str(exc) or type(exc).__name__
    result.update(status='complete' if result['valid'] else 'failed', error=primary,
                  neutralError=neutral_error, traceStopError=trace_error,
                  evidence=decoded_path or trace_file)
    return result


def main():
    cancelled = threading.Event()
    def stop(_number, _frame):
        cancelled.set()
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    print(json.dumps(dict(event='ready', childReadyMonotonicMs=time.monotonic() * 1000)), flush=True)
    try:
        raw = sys.stdin.buffer.readline(65537)
        require(len(raw) <= 65536 and raw.endswith(b'\n'), 'bounded one-line request required')
        result = execute(json.loads(raw), cancelled)
    except BaseException as exc:
        result = dict(status='failed', error=str(exc) or type(exc).__name__, neutralAcknowledged=False, traceStopped=False)
    print(json.dumps(dict(event='result', **result), allow_nan=False), flush=True)
    return 0 if result['status'] == 'complete' else 1


if __name__ == '__main__':
    raise SystemExit(main())
