import gzip
import json
import os
from pathlib import Path
import tempfile
import subprocess
import sys
import unittest
from unittest.mock import patch

import session_phases as sp

BOUND = {'pid': 123, 'birth': '1790000000:12'}
STATUS = dict(roles={'game': BOUND}, final=True, rawComplete=True, finalPartialJSON=False)


def encode(rows):
    return b''.join((json.dumps(row) + '\n').encode() for row in rows)


def mark(at, phase):
    return sp.marker_record(BOUND, phase, 'Observed native window and named scene',
                            'controller-observation', dict(mct=at, monotonic=at + 1000, wall=at + 1790000000))


def interval(start, end, count=20, pid=123, layers=1):
    return dict(PID=pid, Timestamp=10000 + end, Layers=[{'Performance Stats': {
        'Start MCT Seconds': start, 'End MCT Seconds': end, 'Total Duration (sec)': end-start,
        'Presented Frame Stats': {'Frame Count': count}, 'Skipped Frame Stats': {'Frame Count': 0}}}
        for _ in range(layers)])


def report(rows=(), markers=(), events=(), **kwargs):
    return sp.analyze(gzip.compress(encode(rows)), encode(events), encode(markers), STATUS, BOUND, **kwargs)


def process(at, value):
    return dict(type='process_snapshot', monotonic=at + 1000, wall=at + 1790000000, role='game',
                memory={'status': 'ok', 'physicalFootprintBytes': value, 'residentBytes': value // 2})


class PhaseTests(unittest.TestCase):
    def test_weighted_whole_and_per_phase_and_unknown(self):
        value = report([interval(0, 1, 10), interval(1, 3, 40), interval(3, 4, 30)],
                       [mark(0, 'menu'), mark(3, 'gameplay'), mark(5, 'exit')])
        self.assertEqual(value['whole']['intervalRateHz'], 20)
        self.assertAlmostEqual(value['phases']['menu']['intervalRateHz'], 50 / 3)
        self.assertEqual(value['phases']['gameplay']['intervalRateHz'], 30)
        self.assertEqual(value['phases']['unknown']['intervalRateHz'], None)
        self.assertTrue(value['complete'])

    def test_boundary_crossing_not_proportionally_invented(self):
        value = report([interval(0, 1), interval(1, 2), interval(2, 3), interval(3, 4)],
                       [mark(1, 'menu'), mark(2.5, 'loading'), mark(3, 'exit')])
        self.assertEqual(value['phases']['menu']['intervals'], 1)
        self.assertEqual(value['phases']['mixed']['intervals'], 1)
        self.assertEqual(value['phases']['unknown']['intervals'], 2)
        self.assertEqual(sp.phase_at(3, 3, [mark(1, 'menu'), mark(3, 'exit')], 'mct'), 'unknown')

    def test_optional_unlabelled_capture_stays_unknown(self):
        value = report([interval(1, 2)], [])
        self.assertEqual(value['phases']['unknown']['intervals'], 1)
        self.assertEqual(value['phases']['gameplay']['intervals'], 0)

    def test_zero_frames_is_observed_but_gap_is_not_zero(self):
        value = report([interval(1, 2, 0), interval(4, 5, 20)])
        self.assertEqual(value['whole']['observedSeconds'], 2)
        self.assertEqual(value['whole']['intervalRateHz'], 10)
        self.assertEqual(value['discards']['coverageGaps'], 1)

    def test_foreign_duplicate_overlapping_and_out_of_order(self):
        value = report([interval(1, 2), interval(1, 2), interval(1.5, 2.5),
                        interval(2, 3, pid=456), interval(2, 3), interval(0, 1)])
        self.assertEqual(value['whole']['intervals'], 2)
        self.assertEqual(value['discards']['duplicateOrOverlapping'], 3)
        self.assertEqual(value['discards']['foreignPID'], 1)

    def test_multilayer_counts_are_explicit_not_displayed_fps(self):
        value = report([interval(1, 2, layers=2)])
        self.assertEqual(value['whole']['presentedFrames'], 40)
        self.assertNotIn('fps', value['whole'])
        self.assertNotIn('onePercentLow', value['whole'])

    def test_unaligned_layers_and_invalid_metrics_fail(self):
        row = interval(1, 2, layers=2)
        row['Layers'][1]['Performance Stats']['End MCT Seconds'] = 2.1
        for candidate in (row, interval(1, 7), interval(1, 2, count=-1)):
            with self.assertRaises(ValueError):
                report([candidate])
        row = interval(1, 2)
        del row['Layers'][0]['Performance Stats']['Presented Frame Stats']
        with self.assertRaises(ValueError):
            report([row])

    def test_wrong_birth_and_pid_are_rejected(self):
        row = mark(1, 'menu')
        for field, bad in [('pid', 456), ('birth', '1790000000:13')]:
            wrong = dict(row, identity=dict(BOUND, **{field: bad}))
            with self.assertRaises(ValueError):
                report([], [wrong])
        with self.assertRaises(ValueError):
            sp.analyze(gzip.compress(b''), b'', b'', STATUS, dict(BOUND, birth='1790000000:13'))

    def test_unknown_label_marker_order_and_duplicates(self):
        with self.assertRaises(ValueError):
            mark(1, 'combat-inferred-from-fps')
        for markers in ([mark(2, 'menu'), mark(1, 'loading')], [mark(1, 'menu'), mark(1, 'loading')]):
            with self.assertRaises(ValueError):
                report([], markers)
        value = report([interval(1, 2)], [mark(1, 'unknown'), mark(2, 'exit')])
        self.assertEqual(value['phases']['unknown']['intervals'], 1)

    def test_truncated_gzip_and_partial_json_require_opt_in(self):
        complete = encode([interval(1, 2)])
        for raw in (gzip.compress(complete)[:-8], gzip.compress(complete + b'{"PID":')):
            with self.assertRaises(ValueError):
                sp.analyze(raw, b'', b'', STATUS, BOUND)
            value = sp.analyze(raw, b'', b'', STATUS, BOUND, allow_partial=True)
            self.assertFalse(value['complete'])
            self.assertEqual(value['whole']['intervals'], 1)

    def test_malformed_stream_no_resynchronization_and_gzip_trailer(self):
        for data in (b'garbage' + encode([interval(1, 2)]), b'{"PID":123,"PID":123}',
                     b'{"PID":NaN}', b'{"x": [}'):
            with self.assertRaises(ValueError):
                sp.analyze(gzip.compress(data), b'', b'', STATUS, BOUND)
        with self.assertRaises(ValueError):
            sp.analyze(gzip.compress(b'') + gzip.compress(b''), b'', b'', STATUS, BOUND)

    def test_partial_event_stream_and_status(self):
        with self.assertRaises(ValueError):
            sp.analyze(gzip.compress(b''), b'{', b'', STATUS, BOUND)
        value = sp.analyze(gzip.compress(b''), b'{', b'', STATUS, BOUND, allow_partial=True)
        self.assertFalse(value['complete'])
        self.assertTrue(value['errors'])
        for field in ('final', 'rawComplete'):
            with self.assertRaises(ValueError):
                sp.analyze(gzip.compress(b''), b'', b'', dict(STATUS, **{field: False}), BOUND)

    def test_process_memory_join_deltas_unavailable_and_no_cross_phase(self):
        value = report([], [mark(1, 'menu'), mark(3, 'gameplay'), mark(5, 'exit')],
                       [process(1, 100), process(2, 150), process(3, 1000),
                        dict(type='process_snapshot', monotonic=1004, wall=1790000004, role='game', memory={'status': 'unavailable'})])
        self.assertEqual(value['phases']['menu']['memory']['delta']['physicalFootprintBytes'], 50)
        self.assertEqual(value['phases']['gameplay']['memory']['delta'], {})
        self.assertEqual(value['discards']['unavailableMemory'], 1)
        self.assertEqual(value['whole']['memory']['delta']['physicalFootprintBytes'], 900)

    def test_host_vm_and_swap_delta_scopes(self):
        vm = lambda free: dict(argv=['/usr/bin/vm_stat'], returncode=0,
            output=f'Mach Virtual Memory Statistics: (page size of 16384 bytes)\nPages free: {free}.\nSwapouts: 4.\n')
        events = [dict(type='system_snapshot', monotonic=1001, wall=1790000001, result=vm(10)),
                  dict(type='system_snapshot', monotonic=1002, wall=1790000002, result=vm(8))]
        value = report([], [mark(0, 'menu'), mark(3, 'exit')], events)
        self.assertEqual(value['phases']['menu']['hostMemory']['parsed']['vm_stat']['delta']['Pages free bytes'], -32768)
        self.assertEqual(sp.host_memory(dict(argv=['/usr/sbin/sysctl', 'vm.swapusage'], returncode=0,
            output='vm.swapusage: total = 1024.00M used = 3.00M free = 1021.00M'))[1]['usedBytes'], 3 * sp.MIB)
        self.assertIsNone(sp.host_memory(dict(argv=['/usr/bin/vm_stat'], error='timeout')))

    def test_malformed_host_receipts_remain_unavailable(self):
        for output in ('total = 1..2M', 'used = ' + '9' * 400 + 'M'):
            receipt = dict(argv=['/usr/sbin/sysctl', 'vm.swapusage'], returncode=0, output=output)
            self.assertIsNone(sp.host_memory(receipt))
            value = report([], events=[dict(type='system_snapshot', monotonic=1001, wall=1790000001, result=receipt)], allow_partial=True)
            self.assertEqual(value['whole']['hostMemory']['unavailable'], 1)
            json.dumps(value, allow_nan=False)

    def test_caps_depth_records_expansion_and_object(self):
        with self.assertRaises(ValueError):
            list(sp.records([b'{"x":"' + b'x' * 100 + b'"}'], max_object=64))
        with self.assertRaises(ValueError):
            list(sp.records([b'{"x":' + b'[' * 65 + b']' * 65 + b'}']))
        with patch.object(sp, 'MAX_RECORDS', 1):
            with self.assertRaises(ValueError):
                list(sp.records([b'{}{}']))
        with patch.object(sp, 'MAX_EXPANDED', 64):
            with self.assertRaises(ValueError):
                list(sp.inflate(gzip.compress(b'x' * 1000), {'allowPartial': True}))

    def test_recurring_phase_memory_never_spans_other_phase(self):
        value = report([], [mark(0, 'menu'), mark(3, 'gameplay'), mark(5, 'menu'), mark(7, 'exit')],
                       [process(1, 100), process(2, 120), process(4, 1000), process(6, 999)])
        self.assertEqual(value['phases']['menu']['memory']['samples'], 3)
        self.assertEqual(value['phases']['menu']['memory']['delta'], {})
        self.assertEqual(value['whole']['memory']['delta']['physicalFootprintBytes'], 899)
        host = [{'wall': 1, 'segment': 0, 'result': dict(argv=['/usr/sbin/sysctl', 'vm.swapusage'], returncode=0, output='used = 1M')},
                {'wall': 6, 'segment': 2, 'result': dict(argv=['/usr/sbin/sysctl', 'vm.swapusage'], returncode=0, output='used = 100M')}]
        self.assertEqual(sp.summarize_host(host, segmented=True)['parsed']['swapusage']['delta'], {})

    def test_marker_monotonic_is_process_local_and_never_joined(self):
        rows = [mark(1, 'menu'), mark(3, 'exit')]
        rows[0]['monotonic'] = .03
        rows[1]['monotonic'] = .01
        value = report([], rows, [process(2, 100)])
        self.assertEqual(value['phases']['menu']['memory']['samples'], 1)

    def test_clock_discontinuity_rejected(self):
        rows = [mark(1, 'menu'), mark(3, 'exit')]
        rows[1]['wall'] += 60
        with self.assertRaises(ValueError):
            report([], rows)
        events = [process(1, 100), process(2, 200)]
        events[1]['wall'] += 60
        with self.assertRaises(ValueError):
            report([], events=events)
        value = report([], events=events, allow_partial=True)
        self.assertFalse(value['complete'])
        self.assertEqual(value['whole']['memory']['samples'], 1)

    def test_nested_malformed_metal_preserves_only_valid_prefix(self):
        for bad in (None, [], 2):
            row = interval(2, 3)
            row['Layers'][0]['Performance Stats'] = bad
            value = report([interval(1, 2), row], allow_partial=True)
            self.assertFalse(value['complete'])
            self.assertEqual(value['whole']['intervals'], 1)
        row = interval(2, 3)
        row['Timestamp'] = 10 ** 500
        value = report([interval(1, 2), row], allow_partial=True)
        self.assertFalse(value['complete'])
        self.assertEqual(value['whole']['intervals'], 1)

    def test_identity_change_event_stops_memory_attribution(self):
        events = [process(1, 100), dict(type='role_identity_changed', role='game', monotonic=1002, wall=1790000002), process(3, 999)]
        with self.assertRaises(ValueError):
            report([], events=events)
        value = report([], events=events, allow_partial=True)
        self.assertEqual(value['whole']['memory']['samples'], 1)
        self.assertFalse(value['complete'])


class FileTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name).resolve()
        os.chmod(self.path, 0o700)
        self.fd = sp.open_directory(self.path)
        self.write('status.json', json.dumps(STATUS).encode())

    def tearDown(self):
        os.close(self.fd)
        self.temp.cleanup()

    def write(self, name, raw):
        (self.path / name).write_bytes(raw)
        os.chmod(self.path / name, 0o600)

    def append(self, at=1, **kwargs):
        return sp.append_marker(self.fd, BOUND, kwargs.pop('phase', 'menu'), 'Observed title menu',
            kwargs.pop('source', 'controller-observation'),
            get_identity=kwargs.pop('get_identity', lambda pid: BOUND),
            get_clock=lambda: dict(mct=at, monotonic=at + 1000, wall=at + 1790000000), **kwargs)

    def test_append_live_binding_and_exit_absence(self):
        self.append(1)
        with self.assertRaises(ValueError):
            self.append(2, get_identity=lambda pid: dict(BOUND, birth='1790000000:13'))
        self.append(3, phase='exit', source='process-reconciliation', get_identity=lambda pid: None)
        rows = list(sp.records([sp.read_file(self.fd, 'markers.jsonl')]))
        self.assertEqual(len(rows), 2)
        with self.assertRaises(ValueError):
            self.append(4, phase='menu', source='process-reconciliation')

    def test_live_process_changes_during_append(self):
        calls = iter([BOUND, None])
        with self.assertRaises(ValueError):
            self.append(get_identity=lambda pid: next(calls))
        self.assertEqual((self.path / 'markers.jsonl').stat().st_size, 0)

    def test_byte_cap_partial_append_and_hardlink(self):
        self.write('markers.jsonl', b' ' * sp.MIB + b'\n')
        with self.assertRaises(ValueError):
            self.append()
        self.write('markers.jsonl', b'{')
        with self.assertRaises(ValueError):
            self.append()
        (self.path / 'markers.jsonl').unlink()
        os.link(self.path / 'status.json', self.path / 'markers.jsonl')
        with self.assertRaises(ValueError):
            self.append()

    def test_report_cli_exports_fixed_inputs_optional_markers(self):
        self.write('metal.jsonstream.gz', gzip.compress(encode([interval(1, 2)])))
        self.write('events.jsonl', b'')
        result = subprocess.run([sys.executable, str(Path(sp.__file__)), 'report', '--session', str(self.path),
                                 '--pid', str(BOUND['pid']), '--birth', BOUND['birth']], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        value = json.loads(result.stdout)
        self.assertTrue(value['complete'])
        self.assertTrue(value['metricsAvailable'])
        self.assertEqual(value['phases']['unknown']['intervals'], 1)
        self.assertEqual(set(value['sourceSha256']), set(sp.CAPS))

    def test_symlink_traversal_permissions_and_caps(self):
        (self.path / 'markers.jsonl').symlink_to(self.path / 'status.json')
        with self.assertRaises(OSError):
            self.append()
        alias = self.path / 'alias'
        alias.symlink_to(self.path, target_is_directory=True)
        with self.assertRaises(OSError):
            sp.open_directory(alias)
        with self.assertRaises(ValueError):
            sp.open_directory(str(self.path) + '/../' + self.path.name)
        os.chmod(self.path / 'status.json', 0o644)
        with self.assertRaises(ValueError):
            sp.read_file(self.fd, 'status.json')
        self.write('status.json', b'x' * (sp.MIB + 1))
        with self.assertRaises(ValueError):
            sp.read_file(self.fd, 'status.json')


if __name__ == '__main__':
    unittest.main()
