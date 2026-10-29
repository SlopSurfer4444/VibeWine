import copy
import hashlib
import os
from pathlib import Path
import struct
import tempfile
import unittest

import scene_capture as sc
import session_phases as sp

ROLE = dict(pid=123, birth='1793237514:328431')
IDENTITY = '17:23'


def binary(count=2, generation=1, rejected=0):
    raw = bytearray(sc.BYTES)
    struct.pack_into('<7Q', raw, 0, sc.MAGIC, 2, sc.CAPACITY, count, generation << 32, 0, rejected)
    for i in range(count):
        struct.pack_into('<QQQIIIIQQQ', raw, 4096 + 64 * i,
                         10 + i, 11 + i, 33, 95 if i == 0 else 98, 1, 5, generation, 12, 13, i + 1)
    return bytes(raw)


def marker(at, phase='gameplay', role=ROLE):
    return sp.marker_record(role, phase, 'Observed loaded scene: Cemetery bonfire, stationary camera',
                            'user-observation', dict(mct=at, monotonic=at, wall=at))


def config():
    return dict(schema=1, game=ROLE, traceIdentity=IDENTITY, notBeforeWall=100,
                controller=dict(path='/tool', sha256='a' * 64), sourceBinding={'test': 'bound offline fake'},
                policy=dict(profile='combined-bounded', waitSeconds=5, windowSeconds=2,
                            pollMilliseconds=250, markerMaxAgeSeconds=2, plannedRecords=100,
                            tailReserve=10, stopReserved=90, detailLimit=20))


class ProofTests(unittest.TestCase):
    def proof(self, a, b=None, identities=(IDENTITY, IDENTITY)):
        rows = iter(zip((a, a if b is None else b), identities))
        return sc.stopped_prefix(Path('/held/events-1.bin'), IDENTITY, 1, lambda *_: next(rows))

    def test_late_closed_header_growth_old_hash_fails_new_proof_passes(self):
        before, after = binary(rejected=1), binary(rejected=20)
        # Exact old wrapper condition; the observed bug is the whole-file hash gate.
        self.assertNotEqual(hashlib.sha256(before).hexdigest(), hashlib.sha256(after).hexdigest())
        proof = self.proof(before, after)
        self.assertEqual((proof['publishedRecords'], proof['boundaryRejectedAfter']), (2, 20))
        self.assertEqual(proof['secondCopyBytes'], 0)
        self.assertEqual(proof['immutableSha256'], self.proof(before)['immutableSha256'])

    def test_every_immutable_region_is_observed(self):
        raw = binary()
        for offset in (0, 8, 16, 24, 32, 40, 56, 4095, 4096, 4135, 4152, 4224, sc.BYTES - 1):
            with self.subTest(offset=offset):
                changed = bytearray(raw)
                changed[offset] ^= 1
                with self.assertRaises(ValueError):
                    self.proof(raw, bytes(changed))

    def test_payload_change_with_identical_commit_markers_rejected(self):
        a = binary()
        b = bytearray(a)
        struct.pack_into('<Q', b, 4096 + 16, 999)
        with self.assertRaisesRegex(ValueError, 'immutable'):
            self.proof(a, bytes(b))

    def test_malformed_stable_binary_rejected(self):
        mutations = [(0, '<Q', 0), (8, '<Q', 1), (16, '<Q', 4), (24, '<Q', sc.CAPACITY + 1),
                     (32, '<Q', 0), (32, '<Q', 2 << 32), (32, '<Q', (1 << 32) | (1 << 30)),
                     (32, '<Q', (1 << 32) | (2 << 30)), (32, '<Q', (1 << 32) | (3 << 30)),
                     (32, '<Q', (1 << 32) | 1), (40, '<Q', 1),
                     (4096 + 36, '<I', 2), (4096 + 56, '<Q', 0), (4096 + 56, '<Q', 2),
                     (4096 + 128 + 56, '<Q', 3)]
        for offset, fmt, value in mutations:
            with self.subTest(offset=offset, value=value):
                raw = bytearray(binary())
                struct.pack_into(fmt, raw, offset, value)
                with self.assertRaises(ValueError):
                    self.proof(bytes(raw))

    def test_boundary_regression_and_identity_rejected(self):
        with self.assertRaisesRegex(ValueError, 'decreased'):
            self.proof(binary(rejected=2), binary(rejected=1))
        for identities in ((IDENTITY, '17:24'), ('18:23', IDENTITY)):
            with self.assertRaisesRegex(ValueError, 'inode'):
                self.proof(binary(), identities=identities)

    def test_fresh_requires_zero_generation_counter_and_no_tail_commit(self):
        empty = binary(0, 0)
        self.assertEqual(sc.fresh_source('/held', IDENTITY, lambda *_: (empty, IDENTITY))['reserved'], 0)
        for raw in (binary(0, 1), binary(1), binary(0, 0, 1)):
            with self.assertRaises(ValueError):
                sc.fresh_source('/held', IDENTITY, lambda *_: (raw, IDENTITY))

    def test_consumption_revalidates_without_whole_header_hash_gate(self):
        proof = self.proof(binary())
        current = binary(rejected=44)
        self.assertEqual(sc.verify_reference(proof, lambda *_: (current, IDENTITY))['boundaryRejectedAfter'], 44)
        changed = bytearray(current)
        changed[4100] ^= 4
        with self.assertRaises(ValueError):
            sc.verify_reference(proof, lambda *_: (bytes(changed), IDENTITY))

    def test_real_offline_file_identity_symlink_hardlink_and_size(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td).resolve() / 'events-1.bin'
            path.write_bytes(binary())
            path.chmod(0o600)
            raw, identity = sc.owned_read(path, sc.BYTES)
            self.assertEqual(raw, binary())
            sc.stopped_prefix(path, identity, 1)
            link = Path(td) / 'link'
            link.symlink_to(path)
            with self.assertRaises(ValueError):
                sc.owned_read(link, sc.BYTES)
            link.unlink()
            os.link(path, link)
            with self.assertRaises(ValueError):
                sc.owned_read(path, sc.BYTES)
            link.unlink()
            path.write_bytes(b'bad')
            with self.assertRaises(ValueError):
                sc.stopped_prefix(path, identity, 1)


class Fake:
    def __init__(self):
        self.at = 100.
        self.calls, self.outputs = [], {}
        self.scene = lambda at: [marker(100, 'menu')] if at < 101 else [marker(100, 'menu'), marker(101)]
        self.process_identity = lambda pid: ROLE
        self.inspect = lambda: dict(identity=IDENTITY, capture=1, next_reserved=2, dropped=0)
        self.arm_error = self.stop_error = False
        self.count = 2

    def monotonic(self):
        return self.at

    wall = monotonic

    def sleep(self, seconds):
        self.at += seconds

    def markers(self):
        return self.scene(self.at)

    def check_launch(self, p):
        self.calls.append(('check_launch', p['profile']))

    def emit(self, name, value):
        if name in self.outputs:
            raise ValueError('existing receipt')
        self.outputs[name] = value

    def verify_trace_held(self, role):
        self.calls.append(('mapped', self.at))

    def fresh(self):
        self.calls.append(('fresh', self.at))
        return {'fresh': True}

    def proof(self):
        self.calls.append(('proof', self.at))
        return {'publishedRecords': self.count}

    def command(self, command, *args):
        self.calls.append((command, self.at, args))
        if command == 'inspect':
            return self.inspect()
        if command == 'arm':
            if self.arm_error:
                raise ValueError('arm response failed after possible side effect')
            return dict(identity=IDENTITY, generation=1, capture=1)
        if command == 'stop':
            if self.stop_error:
                raise ValueError('stop inconclusive')
            return dict(identity=IDENTITY, generation=1, capture=0, producer_quiescence_verified=True,
                        structurally_complete=True, admitted_writers=0)
        raise AssertionError(command)


class CaptureTests(unittest.TestCase):
    def run_capture(self, backend=None, cfg=None):
        backend = backend or Fake()
        result = sc.Capture(backend, cfg or config()).run()
        return result, backend

    def test_actual_engine_waits_for_observed_scene_then_one_arm_stop_proof(self):
        result, backend = self.run_capture()
        self.assertEqual(result['phase'], 'stopped_reference_saved')
        self.assertEqual(result['trigger']['wall'], 101)
        self.assertEqual(result['elapsedSeconds'], 2)
        commands = [row for row in backend.calls if row[0] in ('arm', 'stop')]
        self.assertEqual([row[0] for row in commands], ['arm', 'stop'])
        self.assertEqual(commands[0][1], 101)
        self.assertNotIn('FPS', result['coverage'])

    def test_title_loading_unknown_and_stale_markers_never_arm(self):
        for phase in ('menu', 'loading', 'unknown', 'exit', 'gameplay'):
            with self.subTest(phase=phase):
                backend = Fake()
                backend.scene = lambda at: [marker(90 if phase == 'gameplay' else 100, phase)]
                result, _ = self.run_capture(backend)
                self.assertEqual(result['phase'], 'failed')
                self.assertFalse(result['armAttempted'])
                self.assertNotIn('stop', [r[0] for r in backend.calls])

    def test_wrong_identity_and_nonvisual_phase_provenance_never_arm(self):
        for change in ({'identity': dict(pid=124, birth=ROLE['birth'])}, {'provenance': 'process-reconciliation'}):
            backend = Fake()
            row = marker(100)
            row.update(change)
            backend.scene = lambda at: [row]
            result, _ = self.run_capture(backend)
            self.assertFalse(result['armAttempted'])

    def test_reordered_future_duplicate_and_prelaunch_markers_never_arm(self):
        cases = [[marker(100), marker(99)], [marker(100), marker(100)], [marker(101)]]
        for rows in cases:
            backend = Fake()
            backend.scene = lambda at: rows
            if rows[0]['wall'] > 100:
                backend.process_identity = lambda pid: ROLE if backend.at < 100.5 else None
            result, _ = self.run_capture(backend)
            self.assertFalse(result['armAttempted'])
        backend = Fake()
        backend.scene = lambda at: [marker(100)]
        cfg = config()
        cfg['notBeforeWall'] = 101
        result, _ = self.run_capture(backend, cfg)
        self.assertFalse(result['armAttempted'])

    def test_phase_transition_process_end_and_capacity_stop(self):
        for reason in ('scene_boundary_or_marker_change', 'game_birth_ended', 'sampled_reserved_limit'):
            with self.subTest(reason=reason):
                backend = Fake()
                if reason == 'scene_boundary_or_marker_change':
                    backend.scene = lambda at: [marker(100)] if at < 101 else [marker(100), marker(101, 'loading')]
                elif reason == 'game_birth_ended':
                    backend.scene = lambda at: [marker(100)]
                    backend.process_identity = lambda pid: ROLE if backend.at < 101 else None
                else:
                    backend.inspect = lambda: dict(identity=IDENTITY, capture=1, next_reserved=90, dropped=0)
                result, _ = self.run_capture(backend)
                self.assertEqual(result['stopReason'], reason)
                self.assertEqual(len([c for c in backend.calls if c[0] == 'stop']), 1)

    def test_possible_arm_side_effect_gets_single_stop_no_retry(self):
        backend = Fake()
        backend.arm_error = True
        result, _ = self.run_capture(backend)
        self.assertEqual(result['phase'], 'failed')
        self.assertFalse(result['armed'])
        self.assertEqual([c[0] for c in backend.calls if c[0] in ('arm', 'stop')], ['arm', 'stop'])

    def test_slow_mapping_or_phase_change_before_arm_never_arms(self):
        for changed in (False, True):
            backend = Fake()
            backend.scene = lambda at: [marker(100)]
            def fresh():
                if changed:
                    backend.scene = lambda at: [marker(100), marker(101, 'loading')]
                else:
                    backend.at += 3
                return {'fresh': True}
            backend.fresh = fresh
            result, _ = self.run_capture(backend)
            self.assertFalse(result['armAttempted'])

    def test_unknown_stop_preserved_no_proof_or_rearm(self):
        backend = Fake()
        backend.stop_error = True
        result, _ = self.run_capture(backend)
        self.assertEqual(result['armed'], 'unknown')
        self.assertEqual(len([c for c in backend.calls if c[0] == 'stop']), 1)
        self.assertNotIn('proof', [c[0] for c in backend.calls])

    def test_graceful_keyboard_interrupt_stops_owned_capture_once(self):
        backend = Fake()
        backend.scene = lambda at: [marker(100)]
        def interrupt(seconds):
            raise KeyboardInterrupt('operator cancelled')
        backend.sleep = interrupt
        result, _ = self.run_capture(backend)
        self.assertEqual(result['phase'], 'failed')
        self.assertFalse(result['armed'])
        self.assertEqual([c[0] for c in backend.calls if c[0] in ('arm', 'stop')], ['arm', 'stop'])

    def test_loss_empty_data_or_binding_failure_cannot_pass(self):
        for field, value in (('identity', '17:24'), ('capture', 0), ('dropped', 1), ('next_reserved', sc.CAPACITY + 1)):
            backend = Fake()
            progress = backend.inspect()
            progress[field] = value
            backend.inspect = lambda: progress
            result, _ = self.run_capture(backend)
            self.assertEqual(result['phase'], 'failed')
            self.assertEqual(len([c for c in backend.calls if c[0] == 'stop']), 1)
        backend = Fake()
        backend.count = 0
        result, _ = self.run_capture(backend)
        self.assertEqual(result['phase'], 'failed')

    def test_policy_invalid_and_booleans_rejected(self):
        for key, value in (('profile', 'full'), ('windowSeconds', 601), ('pollMilliseconds', 0),
                           ('detailLimit', True), ('tailReserve', 0), ('stopReserved', 101), ('plannedRecords', sc.CAPACITY)):
            with self.subTest(key=key):
                cfg = config()
                cfg['policy'][key] = value
                with self.assertRaises(ValueError):
                    sc.Capture(Fake(), cfg)
        cfg = config()
        cfg['policy']['profile'] = 'stall-waits'
        self.assertEqual(self.run_capture(cfg=cfg)[0]['phase'], 'stopped_reference_saved')


class AdoptionTests(unittest.TestCase):
    """Real owned filesystem wrapper checks; never execute a native command."""
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name).resolve()
        self.root.chmod(0o700)
        self.session = self.root / 'monitor'
        self.session.mkdir(mode=0o700)
        self.trace_dir = self.root / ('dxmt-trace-session-' + '1' * 32)
        self.trace_dir.mkdir(mode=0o700)
        for path, raw in ((self.trace_dir / '.cs2trace-session', sc.MARKER),
                          (self.trace_dir / 'events-32.bin', binary(0, 0)),
                          (self.root / 'controller', b'INERT TEST BYTES NEVER EXECUTED')):
            path.write_bytes(raw)
            path.chmod(0o600)
        self.cfg = config()
        self.cfg.update(sessionDirectory=str(self.session), traceFile=str(self.trace_dir / 'events-32.bin'))
        self.cfg['controller'] = dict(path=str(self.root / 'controller'), sha256=sc.digest((self.root / 'controller').read_bytes()))
        self.cfg['traceIdentity'] = sc.owned_read(Path(self.cfg['traceFile']), sc.BYTES)[1]
        self.cfg['sourceBinding'] = {k: 'a' * 40 for k in ('hubRevision', 'wineRevision', 'dxmtRevision')}
        self.cfg['sourceBinding'].update({k: 'b' * 64 for k in ('buildReceiptSha256', 'installedManifestSha256', 'rendererSha256')})
        self.cfg['sourceBinding']['loadedModules'] = {'actual-loaded-module': 'c' * 64}
        self.cfg['launch'] = dict(traceProfileRequested='combined-bounded', traceDetailRecordLimit=20, rendererSha256='b' * 64)
        self.backend = sc.Native(self.cfg)

    def tearDown(self):
        self.backend.close()
        self.temp.cleanup()

    def test_native_adapter_pins_fresh_session_and_refuses_duplicate_operator(self):
        self.backend.check_launch(self.cfg['policy'])
        self.assertEqual(self.backend.fresh()['reserved'], 0)
        with self.assertRaises(BlockingIOError):
            sc.Native(self.cfg)
        self.backend.emit('capture-ready.json', {'phase': 'test'})
        with self.assertRaises(FileExistsError):
            self.backend.emit('capture-ready.json', {'phase': 'test'})

    def test_launch_binding_profile_detail_and_revision_drift_rejected(self):
        mutations = [lambda c: c['launch'].update(traceProfileRequested='stall-waits'),
                     lambda c: c['launch'].update(traceDetailRecordLimit=True),
                     lambda c: c['launch'].update(rendererSha256='d' * 64),
                     lambda c: c['sourceBinding'].update(dxmtRevision='bad'),
                     lambda c: c['sourceBinding'].update(loadedModules={})]
        for change in mutations:
            cfg = copy.deepcopy(self.cfg)
            change(cfg)
            adapter = sc.Native.__new__(sc.Native)
            adapter.config = cfg
            with self.assertRaises(ValueError):
                adapter.check_launch(cfg['policy'])

    def test_controller_marker_and_directory_replacement_rejected(self):
        (self.root / 'controller').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'pin'):
            self.backend.controller_pin()
        marker_path = self.trace_dir / '.cs2trace-session'
        marker_path.rename(self.trace_dir / '.saved-marker')
        marker_path.write_bytes(sc.MARKER)
        marker_path.chmod(0o600)
        with self.assertRaisesRegex(ValueError, 'marker replaced'):
            self.backend.verify_session()
        old_dir = self.root / 'old'
        self.trace_dir.rename(old_dir)
        self.trace_dir.mkdir(mode=0o700)
        with self.assertRaisesRegex(ValueError, 'session replaced'):
            self.backend.verify_session()


if __name__ == '__main__':
    unittest.main()
