"""Offline fake controller polling + real native collector; never launches a game."""
from contextlib import contextmanager, ExitStack
import importlib.util
import json
import os
import signal
import time
from pathlib import Path
import struct
import sys
import subprocess
import tempfile
import threading
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location('timed_executor', Path(__file__).with_name('timed-executor.py'))
te = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(te)
CLI = Path(__file__).resolve().parents[2] / 'build/telemetry/cs2trace'
EVENT = struct.Struct('<QQQIIIIQQQ')


@unittest.skipUnless(CLI.is_file(), 'build/telemetry/cs2trace required')
class TimedExecutorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=os.path.realpath(tempfile.gettempdir()))
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        session = json.loads(subprocess.check_output([str(CLI), 'new-session', str(self.root)]))['session']
        self.trace = Path(session) / 'fixture.bin'
        fd = os.open(self.trace, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        os.ftruncate(fd, 4096 + 131072 * 64)
        os.pwrite(fd, struct.pack('<7Q', 0x4353325452414332, 2, 131072, 0, 0, 0, 0), 0)
        os.close(fd)
        controller = self.root / 'controller'
        conf = te.vc.initialize(controller, r'C:\Games\Example.exe', r'Z:\private\tmp\fixture.bin')
        self.output = self.root / 'output'
        self.output.mkdir(mode=0o700)
        info = self.trace.stat()
        self.request = dict(timeoutMs=2000, durationMs=20, kind='stationary', route=None,
            identity=dict(processKey='fixture:123:start', candidateId='fixture-candidate'), candidateId='fixture-candidate',
            controllerDirectory=str(controller), controllerToken=conf['environment']['VIBEWINE_XINPUT_TEST_TOKEN'],
            windowsPid=123, exe=r'C:\Games\Example.exe', traceFile=str(self.trace),
            traceIdentity=f'{info.st_dev}:{info.st_ino}', traceGeneration=1, plannedRecords=100, tailReserve=10,
            traceCli=str(CLI), outputDirectory=str(self.output), inputExpiryMs=250,
            inputAdmissionId='fixture-input', telemetryAdmissionId='fixture-telemetry',
            sceneKey='fixture-scene', settingsKey='fixture-settings')
        self.polls = 0

    def run_capture(self, *, overflow=False, no_events=False, release_ack=True, cancel_at_route_wait=False, cancel_at_state=False, publication_clock=False, late_state=False, quiescence=False, late_reservation=False, synthetic_presentation=False):
        original_publish = te.vc.publish
        original_popen = subprocess.Popen
        original_save = te.vc.save_manifest
        self.delayed_reservations = []
        def save(directory, manifest):
            original_save(directory, manifest)
            if late_reservation and manifest['seq'] == 2:
                # Advance only after the real sequence reservation is durable.
                # Host scheduling must not trip the earlier route lateness gate.
                self.assertTrue(publication_clock)
                self.delayed_reservations.append(manifest['seq'])
                clock.sleep(0.03)
        class PublicationClock:
            # Cancellation and reservation deadlines use simulated local time.
            # Native collector subprocesses and file publication remain real.
            now = time.monotonic()
            def monotonic(inner): return inner.now
            def sleep(inner, duration): inner.now += duration
        clock = PublicationClock()
        self.published_active_states = []
        class RouteCancellation(threading.Event):
            trace_armed = False
            entered_route_check = False
            def is_set(inner):
                # After the native ARM receipt, execute() sets its interval
                # start and next checks cancellation in the route's wait_until.
                # Gate on that observable boundary rather than racing a timer
                # against the independent STATE publication deadline.
                if cancel_at_route_wait and inner.trace_armed:
                    inner.entered_route_check = True
                    inner.set()
                return super().is_set()
            def wait(inner, timeout=None):
                if publication_clock:
                    clock.sleep(timeout or 0)
                    return inner.is_set()
                return super().wait(timeout)
        cancelled = RouteCancellation()
        self.route_cancellation = cancelled
        def publish(directory, manifest, flags, *args, **kwargs):
            seq = original_publish(directory, manifest, flags, *args, **kwargs)
            if flags == te.vc.RELEASE and not release_ack:
                return seq
            self.polls += 1
            status = {te.vc.ARM: 'READY', te.vc.STATE: 'STATE', te.vc.RELEASE: 'RELEASE'}[flags]
            payload = f"VXIA0001 {manifest['token']} 123 {seq} {self.polls} 1 {status}\n".encode()
            te.vc.atomic_write(directory, te.vc.ACK_FILE, payload)
            if flags == te.vc.STATE:
                # Observe every successful nonneutral publication, including
                # one that might later be overwritten by neutral cleanup.
                packet = te.vc.SNAPSHOT.unpack(te.vc.read_file(directory, te.vc.STATE_FILE, limit=72, private=True))
                self.published_active_states.append(packet)
            if flags == te.vc.STATE and cancel_at_state:
                cancelled.set()
            if flags == te.vc.STATE and late_state:
                # Receipt/publication latency is part of the unchanged 40ms hold.
                import time
                time.sleep(0.05)
            return seq
        trace = self.trace
        native_clock_samples = []
        def publish_synthetic_presentation():
            # This is fabricated protocol input in a private offline fixture,
            # not a callback, GPU, game, or actual displayed-frame observation.
            # Real native samples supply only the clock basis. No analyzer or
            # accepted-status stub is used; stop/decode/analysis remain real.
            with trace.open('rb') as handle:
                reserved = struct.unpack('<Q', os.pread(handle.fileno(), 8, 24))[0]
            self.assertEqual(reserved, 0, 'fixture must not overwrite published records')
            start = native_clock_samples[0]['ca_upper_seconds']
            duration = self.request['durationMs'] / 1000
            admission = self.request['presentationWindow']
            records = []
            def record(t, stamp, *, flags=0, p0=0, p1=0):
                records.append((stamp, stamp, admission['swapchain'], t, 1, flags,
                                self.request['traceGeneration'], p0, p1))
            record(70, 800, p0=1, p1=3)
            for index in range(7):record(74, 900, flags=index, p0=5)
            times = [start - .001, start, start + duration / 4,
                     start + 3 * duration / 4, start + duration,
                     start + duration + .001]
            for frame, seconds in enumerate(times, 10):
                stamp = 1000 + frame * 10
                bits = struct.unpack('<Q', struct.pack('<d', seconds))[0]
                record(71, stamp, flags=0x101, p0=frame, p1=bits)
                record(72, stamp, p0=frame, p1=admission['revision'])
                record(73, stamp, p0=frame, p1=admission['width'] << 32 | admission['height'])
            for index, value in enumerate((len(times), len(times), 0, 0, 0, 0, 0)):
                record(74, 2000, flags=index, p0=5, p1=value)
            with trace.open('r+b') as handle:
                for index, record_fields in enumerate(records):
                    os.pwrite(handle.fileno(), EVENT.pack(*record_fields, index + 1), 4096 + index * 64)
                os.pwrite(handle.fileno(), struct.pack('<Q', len(records)), 24)
        class NativePeer:
            def __init__(self, args, **kwargs):
                self.child = original_popen(args, **kwargs)
                self.args = args
                self.injected = False
            def __getattr__(self, key): return getattr(self.child, key)
            def communicate(self, *args, **kwargs):
                data = self.child.communicate(*args, **kwargs)
                if self.args[1] == 'arm' and self.child.returncode == 0 and not self.injected:
                    self.injected = True
                    with trace.open('r+b') as handle:
                        if not no_events:
                            os.pwrite(handle.fileno(), EVENT.pack(100, 110, 1, 1, 1, 0, 1, 0, 0, 1), 4096)
                            os.pwrite(handle.fileno(), struct.pack('<Q', 1), 24)
                        if overflow: os.pwrite(handle.fileno(), struct.pack('<Q', 1), 40)
                        if quiescence:
                            os.pwrite(handle.fileno(), struct.pack('<Q', (1 << 32) | (1 << 30) | 1), 32)
                    cancelled.trace_armed = True
                if synthetic_presentation and self.args[1] == 'clock-sample' and self.child.returncode == 0 and not self.injected:
                    self.injected = True
                    native_clock_samples.append(json.loads(data[0]))
                    if len(native_clock_samples) == 2:
                        publish_synthetic_presentation()
                return data
        with ExitStack() as stack:
            stack.enter_context(mock.patch.object(te.vc, 'publish', side_effect=publish))
            stack.enter_context(mock.patch.object(te.vc, 'save_manifest', side_effect=save))
            stack.enter_context(mock.patch.object(te.subprocess, 'Popen', side_effect=NativePeer))
            if publication_clock:
                stack.enter_context(mock.patch.object(te, 'time', clock))
                stack.enter_context(mock.patch.object(te.vc, 'time', clock))
            return te.execute(self.request, cancelled)

    def route(self):
        self.request.update(kind='route', durationMs=200,
            route=dict(id='fixture-route', sceneKey='fixture-scene', settingsKey='fixture-settings', durationMs=200,
                       steps=[dict(offsetMs=20, durationMs=40, state=dict(ly=10000)), dict(offsetMs=120, durationMs=40, state=dict(ly=-10000))]))

    @contextmanager
    def actual_file_peers(self):
        """Independent polling fixture for the real spawned worker, no game."""
        stop = threading.Event()
        errors = []
        def peer():
            seen = set()
            generation_seen = set()
            try:
                while not stop.is_set():
                    snapshot = Path(self.request['controllerDirectory']) / te.vc.STATE_FILE
                    if snapshot.exists():
                        packet = te.vc.SNAPSHOT.unpack(snapshot.read_bytes())
                        if packet[2] not in seen:
                            seen.add(packet[2])
                            status = {te.vc.ARM: 'READY', te.vc.STATE: 'STATE', te.vc.RELEASE: 'RELEASE'}[packet[-1]]
                            ack = snapshot.with_name('peer-ack.tmp')
                            ack.write_text(f"VXIA0001 {self.request['controllerToken']} 123 {packet[2]} {len(seen)} 1 {status}\n")
                            os.replace(ack, snapshot.with_name(te.vc.ACK_FILE))
                    with self.trace.open('r+b') as handle:
                        control = struct.unpack('<Q', os.pread(handle.fileno(), 8, 32))[0]
                        generation = control >> 32
                        if control & (1 << 30) and generation not in generation_seen:
                            generation_seen.add(generation)
                            os.pwrite(handle.fileno(), EVENT.pack(100, 110, 1, 1, 1, 0, generation, 0, 0, 1), 4096)
                            os.pwrite(handle.fileno(), struct.pack('<Q', 1), 24)
                    stop.wait(0.002)
            except BaseException as exc:
                errors.append(str(exc))
        thread = threading.Thread(target=peer)
        thread.start()
        try:
            yield
        finally:
            stop.set(); thread.join(timeout=2)
            self.assertFalse(thread.is_alive()); self.assertEqual(errors, [])

    def test_stationary_real_native_arm_stop_decode_and_no_display_claim(self):
        result = self.run_capture()
        self.assertEqual(result['status'], 'complete', result)
        self.assertTrue(result['neutralAcknowledged'] and result['traceStopped'] and result['producerQuiescenceVerified'])
        self.assertGreaterEqual(result['durationMonotonicMs'], 20)
        self.assertEqual(result['displayedMetricsStatus'], 'inconclusive')
        self.assertEqual(result['fixedWindowMetricsStatus'], 'inconclusive-unisolated-trace-window')
        self.assertTrue(Path(result['evidence']).is_file())
        self.assertEqual(result['traceSummary']['generation'], 1)
        self.assertNotIn(self.request['controllerToken'], json.dumps(result))

    def test_finite_route_actual_receipts_and_fixed_endpoints(self):
        self.route()
        # Schedule semantics, not an OS/fsync 5ms service-level guarantee.
        result = self.run_capture(publication_clock=True)
        self.assertEqual(result['status'], 'complete', result)
        self.assertEqual(len(result['schedule']), 2)
        self.assertTrue(all(row['leaseMs'] <= 40 for row in result['schedule']))
        for row, expected in zip(result['schedule'], (20, 120)):
            self.assertAlmostEqual(row['publicationOffsetMs'], expected, places=5)
        self.assertTrue(result['neutralAcknowledged'])
        # The simulated schedule lands exactly on the endpoint; conversion from
        # binary seconds can differ by one floating-point ULP, not elapsed time.
        self.assertAlmostEqual(result['durationMonotonicMs'], 200, places=6)

    def test_preflight_token_pid_and_trace_identity_reject_before_arm(self):
        for field, value in [('controllerToken', 'b' * 32), ('windowsPid', 456), ('traceIdentity', '0:0')]:
            with self.subTest(field=field):
                request = dict(self.request, **{field: value})
                if field == 'windowsPid':
                    with te.vc.locked_session(request['controllerDirectory']) as (directory, manifest):
                        manifest['pid'] = 123
                        te.vc.save_manifest(directory, manifest)
                with self.assertRaises((te.CaptureError, te.vc.ControllerError)):
                    te.execute(request)
        inspected = json.loads(subprocess.check_output([str(CLI), 'inspect', str(self.trace)]))
        self.assertEqual(inspected['capture'], 0)

    def presentation_admission(self):
        import hashlib, uuid
        return dict(collectorSha256=hashlib.sha256(CLI.read_bytes()).hexdigest(), measurementId=str(uuid.uuid4()),
                    swapchain=1, width=640, height=480, revision=1)

    def test_presentation_requires_actual_collector_hash_before_arm_or_input(self):
        self.request['presentationWindow'] = dict(self.presentation_admission(), collectorSha256='0' * 64)
        with self.assertRaisesRegex(te.CaptureError, 'collector changed'):
            self.run_capture()
        self.assertEqual(self.polls, 0)
        inspected = json.loads(subprocess.check_output([str(CLI), 'inspect', str(self.trace)]))
        self.assertEqual(inspected['capture'], 0)

    def test_real_native_clock_and_decode_without_presented_frames_cannot_claim_window(self):
        self.request['presentationWindow'] = self.presentation_admission()
        result = self.run_capture()
        self.assertEqual(result['status'], 'failed')
        self.assertIn('native presentation window rejected', result['error'])
        self.assertTrue(result['neutralAcknowledged'] and result['traceStopped'])
        self.assertEqual(len(result['presentationClockSamples']), 2)
        self.assertFalse(result['nativePresentationWindow']['accepted'])
        self.assertIsNone(result['nativePresentationWindow']['presentation_count'])
        saved = json.loads(Path(result['presentationEvidence']).read_text())
        self.assertEqual(saved['clockSamples'], result['presentationClockSamples'])
        self.assertEqual(saved['admission'], self.request['presentationWindow'])
        self.assertTrue(Path(result['evidence']).is_file())
        self.assertEqual(result['displayedMetricsStatus'], 'inconclusive')

    def test_real_clock_decoder_and_analyzer_accept_explicit_synthetic_window(self):
        self.request['presentationWindow'] = self.presentation_admission()
        self.request['timeoutMs'] = 5000
        result = self.run_capture(no_events=True, synthetic_presentation=True)
        self.assertEqual(result['status'], 'complete', result)
        self.assertTrue(result['neutralAcknowledged'] and result['traceStopped'] and result['producerQuiescenceVerified'])
        self.assertEqual(result['displayedMetricsStatus'], 'accepted-native-presentation-window')
        self.assertEqual(result['fixedWindowMetricsStatus'], 'accepted-stationary-native-clock-window')
        observed = result['nativePresentationWindow']
        self.assertTrue(observed['accepted'], observed['rejection_reasons'])
        self.assertEqual(observed['scenario'], 'stationary')
        self.assertEqual(observed['decoder_identity'], self.request['traceIdentity'])
        self.assertEqual(observed['generation'], self.request['traceGeneration'])
        self.assertEqual(observed['measurement_id'], self.request['presentationWindow']['measurementId'])
        self.assertEqual(observed['collector_sha256'], self.request['presentationWindow']['collectorSha256'])
        self.assertEqual(observed['expected'], dict(swapchain=1, dimensions=dict(width=640, height=480), revision=1))
        self.assertEqual(observed['presentation_count'], 3)
        self.assertEqual(observed['observed_window_frame_ids'], [11, 12, 13])
        self.assertEqual(observed['spanning_frame_ids'], [11, 12, 13, 14])
        self.assertEqual(observed['interior_interval_distribution']['samples'], 2)
        self.assertEqual(observed['presentations_per_fixed_second'], 3 / .020)
        self.assertEqual(len(result['presentationClockSamples']), 2)
        self.assertEqual(len(result['presentationClockInvocationBounds']), 2)
        first, last = [envelope['clock_sample'] for envelope in result['presentationClockSamples']]
        self.assertEqual(first['monotonic_clock_api'], 'mach_absolute_time')
        self.assertEqual(observed['window']['start_seconds'], first['ca_upper_seconds'])
        self.assertGreaterEqual(last['ca_lower_seconds'], observed['window']['end_seconds'])
        decoded = [json.loads(line) for line in Path(result['evidence']).read_text().splitlines()]
        self.assertEqual(decoded[-1]['events'], 33)
        self.assertEqual(sum(e.get('type') == 71 for e in decoded), 6)
        self.assertTrue(all(e['clock_domain'] == 'cpu_steady_ns' for e in decoded[:-1]))
        self.assertNotIn(self.request['controllerToken'], json.dumps(result, allow_nan=False))

    def test_moving_route_cannot_request_stationary_presentation_claim(self):
        self.route()
        self.request['presentationWindow'] = self.presentation_admission()
        with self.assertRaisesRegex(te.CaptureError, 'stationary presentation admission'):
            self.run_capture()
        self.assertEqual(self.polls, 0)

    def test_overflow_and_empty_capture_are_inconclusive_after_stop(self):
        result = self.run_capture(overflow=True)
        self.assertEqual(result['status'], 'failed'); self.assertTrue(result['overflow'])
        self.assertTrue(result['traceStopped'] and result['neutralAcknowledged'])

    def test_missing_events_cannot_be_a_valid_measurement(self):
        result = self.run_capture(no_events=True)
        self.assertEqual(result['status'], 'failed'); self.assertTrue(result['traceStopped'])

    def test_no_neutral_ack_cannot_report_success(self):
        self.request['cleanupTimeoutMs'] = 150
        result = self.run_capture(release_ack=False)
        self.assertEqual(result['status'], 'failed'); self.assertFalse(result['neutralAcknowledged'])
        self.assertIsNotNone(result['neutralError']); self.assertTrue(result['traceStopped'])

    def test_cancellation_during_route_releases_and_stops_trace(self):
        self.route()
        result = self.run_capture(cancel_at_route_wait=True)
        self.assertEqual(result['status'], 'failed'); self.assertIn('cancelled', result['error'])
        self.assertTrue(self.route_cancellation.trace_armed and self.route_cancellation.entered_route_check)
        self.assertEqual(result['schedule'], [])
        self.assertTrue(result['neutralAcknowledged'] and result['traceStopped'])

    def test_active_state_cancellation_with_simulated_publication_clock(self):
        self.route()
        result = self.run_capture(cancel_at_state=True, publication_clock=True)
        self.assertEqual(result['status'], 'failed'); self.assertIn('cancelled', result['error'])
        self.assertEqual(len(self.published_active_states), 1)
        packet = self.published_active_states[0]
        self.assertEqual(packet[-1], te.vc.STATE)
        self.assertEqual(packet[8], 10000)  # actual published left-stick Y
        self.assertEqual(len(result['schedule']), 1)
        self.assertTrue(result['neutralAcknowledged'] and result['traceStopped'] and result['producerQuiescenceVerified'])
        final = te.vc.SNAPSHOT.unpack((Path(self.request['controllerDirectory']) / te.vc.STATE_FILE).read_bytes())
        self.assertEqual(final[-1], te.vc.RELEASE)

    def test_late_ack_does_not_extend_or_repeat_hold(self):
        self.route()
        result = self.run_capture(late_state=True)
        self.assertEqual(result['status'], 'failed'); self.assertTrue(result['neutralAcknowledged'] and result['traceStopped'])
        self.assertLessEqual(len(result['schedule']), 1)

    def test_late_sequence_persistence_cannot_publish_active_state(self):
        self.route()
        result = self.run_capture(late_reservation=True, publication_clock=True)
        self.assertEqual(result['status'], 'failed'); self.assertIn('publication deadline', result['error'])
        self.assertEqual(self.delayed_reservations, [2])
        self.assertEqual(self.published_active_states, [])
        self.assertEqual(result['schedule'], [])
        self.assertTrue(result['neutralAcknowledged'] and result['traceStopped'])
        packet = te.vc.SNAPSHOT.unpack((Path(self.request['controllerDirectory']) / te.vc.STATE_FILE).read_bytes())
        self.assertEqual(packet[-1], te.vc.RELEASE)

    def test_admitted_writer_prevents_quiescence_claim(self):
        result = self.run_capture(quiescence=True)
        self.assertEqual(result['status'], 'failed'); self.assertFalse(result['producerQuiescenceVerified'])
        self.assertIsNotNone(result['traceStopError'])

    def test_real_node_python_native_cli_bridge_with_independent_file_peers(self):
        node = Path('/Users/llmhub/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node')
        if not node.exists(): self.skipTest('bundled Node unavailable')
        self.request['durationMs'] = 100
        script = ("import {createTimedExecutor} from " + json.dumps(Path(__file__).with_name('timed-executor.mjs').as_uri()) + ";"
            "import fs from 'node:fs';"
            "const admission=JSON.parse(fs.readFileSync(0,'utf8'));"
            "const run=createTimedExecutor({python:" + json.dumps(sys.executable) + ",workerPath:" + json.dumps(str(Path(__file__).with_name('timed-executor.py'))) + ",admission});"
            "const result=await run.run({deadline:performance.now()+3000});process.stdout.write(JSON.stringify(result));")
        with self.actual_file_peers():
            process = subprocess.run([str(node), '--input-type=module', '-e', script], input=json.dumps(self.request), text=True, capture_output=True, timeout=5)
        self.assertEqual(process.returncode, 0, process.stderr)
        result = json.loads(process.stdout)
        self.assertEqual(result['clock'], 'anchored-child-monotonic')
        self.assertLessEqual(result['startedAtLower'], result['startedAtUpper'])
        self.assertGreaterEqual(result['durationMonotonicMs'], 100)
        self.assertTrue(result['neutralAcknowledged'] and result['traceStopped'])
        self.assertEqual(result['displayedMetricsStatus'], 'inconclusive')

    def test_sigterm_real_worker_neutralizes_and_stops_before_reporting_failure(self):
        self.request['durationMs'] = 1000
        with self.actual_file_peers():
            child = subprocess.Popen([sys.executable, str(Path(__file__).with_name('timed-executor.py'))], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                self.assertEqual(json.loads(child.stdout.readline())['event'], 'ready')
                child.stdin.write(json.dumps(self.request) + '\n'); child.stdin.flush()
                deadline = time.monotonic() + 2
                while time.monotonic() < deadline:
                    with self.trace.open('rb') as handle:
                        if struct.unpack('<Q', os.pread(handle.fileno(), 8, 32))[0] & (1 << 30): break
                    time.sleep(0.002)
                else: self.fail('worker did not arm native trace')
                child.send_signal(signal.SIGTERM)
                child.stdin.close(); child.stdin = None
                stdout, stderr = child.communicate(timeout=2)
            finally:
                if child.poll() is None: child.kill(); child.communicate()
        result = json.loads(stdout.splitlines()[-1])
        self.assertEqual(child.returncode, 1, stderr)
        self.assertEqual(result['status'], 'failed')
        self.assertTrue(result['neutralAcknowledged'] and result['traceStopped'])
        self.assertIn('cancelled', result['error'])

    def test_derivative_cap_refuses_growth_without_deleting_evidence(self):
        held = self.output / 'capture-held.ndjson'
        with held.open('wb') as handle: handle.truncate(te.MAX_DERIVATIVE_BYTES)
        result = self.run_capture()
        self.assertEqual(result['status'], 'failed'); self.assertIn('byte cap', result['error'])
        self.assertEqual(held.stat().st_size, te.MAX_DERIVATIVE_BYTES)
        self.assertTrue(result['traceStopped'] and result['neutralAcknowledged'])


if __name__ == '__main__': unittest.main()
