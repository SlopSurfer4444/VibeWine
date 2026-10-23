"""Identity-bound binary decoder and analyzer policy for additive event 95."""
import copy
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import tests_cli
from analyze import analyze
from test_analyze import event, rows

HERE = Path(__file__).resolve().parent
HUB = HERE.parents[1]


def marker(**changes):
    value = event(95, 100, obj=1, flags=1, seq=3, frame=tests_cli.CAPACITY)
    value.update(changes)
    return value


class TraceProfileTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.previous_controller = tests_cli.CONTROLLER
        cls.scratch = tempfile.TemporaryDirectory(prefix='trace-profile-decoder-')
        binary = Path(cls.scratch.name) / 'cs2trace'
        try:
            subprocess.run(['xcrun', 'clang++', '-std=c++20', '-O1', '-Wall', '-Wextra', '-Werror',
                            str(HERE / 'trace_controller.cpp'), '-framework', 'QuartzCore', '-o', str(binary)],
                           check=True, capture_output=True, text=True, timeout=60)
        except Exception:
            cls.scratch.cleanup()
            raise
        tests_cli.CONTROLLER = binary

    @classmethod
    def tearDownClass(cls):
        tests_cli.CONTROLLER = cls.previous_controller
        cls.scratch.cleanup()

    def fixture(self):
        fixture = tests_cli.CLITests()
        fixture.setUp()
        self.addCleanup(fixture.doCleanups)
        return fixture

    def append(self, fixture, events, generation=7, offset=0):
        with fixture.path.open('r+b') as handle:
            for index, e in enumerate(events, offset):
                data = tests_cli.EVENT.pack(e['start'], e['end'], e['object'], e['type'], e['thread'],
                                           e['flags'], generation, e['padding0'], e['padding1'], index + 1)
                os.pwrite(handle.fileno(), data, 4096 + index * 64)
        fixture.patch(24, offset + len(events))

    def decode(self, events, expected_code=0):
        fixture = self.fixture()
        fixture.arm(7)
        self.append(fixture, events)
        fixture.stop(7)
        result = fixture.call('decode', fixture.path, 7, fixture.identity)
        self.assertEqual(result.returncode, expected_code, result.stdout + result.stderr)
        decoded = fixture.rows(result)
        return decoded, analyze(decoded)

    def test_binary_metadata_clock_instants_and_no_fps(self):
        decoded, result = self.decode([marker(), event(4, 200, 500, flags=1)])
        metadata = decoded[0]
        self.assertEqual(metadata['event_name'], 'Producer trace profile')
        self.assertEqual(metadata['producer_profile'], 'compile-waits')
        self.assertEqual(metadata['profile_schema'], 1)
        self.assertIs(metadata['profile_metadata_valid'], True)
        self.assertIsNone(metadata['duration_ns'])
        self.assertEqual(metadata['clock_domain'], 'cpu_steady_ns')
        self.assertEqual(metadata['detail_limit'], 3)
        self.assertEqual(metadata['lifetime_capacity'], tests_cli.CAPACITY)
        self.assertIs(decoded[-1]['producer_profile_policy_valid'], True)
        self.assertIs(result['producer_profile']['capture_usable'], True)
        self.assertFalse(result['cpu_present_streams'])
        self.assertIsNone(result['queue_chunk_metrics']['gpu_command_buffer_durations'])

    def test_default_legacy_full_still_accepted_and_unclaimed(self):
        decoded, result = self.decode([event(42, 100, 200)])
        self.assertIs(decoded[-1]['producer_profile_policy_valid'], True)
        self.assertEqual(result['producer_profile']['name'], 'unspecified-full-or-legacy')
        self.assertIs(result['producer_profile']['metadata_observed'], False)
        self.assertEqual(result['full_api_boundaries'][0]['type'], 42)

    def test_unknown_profile_fails_closed_nonempty_decode(self):
        decoded, result = self.decode([marker(flags=2)], 3)
        self.assertEqual(decoded[0]['producer_profile'], 'invalid-fail-closed')
        self.assertIs(decoded[-1]['inconclusive_window'], True)
        self.assertIs(decoded[-1]['structurally_complete'], True)
        self.assertIs(result['producer_profile']['capture_usable'], False)
        self.assertIs(result['detailed_probes']['capture_usable'], False)
        self.assertIn('invalid_producer_profile_fail_closed', result['producer_profile']['failures'])

    def test_malformed_duplicate_or_excluded_data_cannot_pass_profile(self):
        invalid = [[marker(**change)] for change in [dict(object=0), dict(flags=0), dict(flags=3),
                   dict(end=101), dict(padding0=32769), dict(padding1=tests_cli.CAPACITY-1)]]
        invalid += [[marker(), marker()], [marker(), event(42, 200, 300)],
                    [marker(), event(90, 200, 300)]]
        for events in invalid:
            with self.subTest(events=events):
                decoded, result = self.decode(events, 3)
                self.assertIs(decoded[-1]['producer_profile_policy_valid'], False)
                self.assertIs(result['producer_profile']['capture_usable'], False)
                self.assertIs(result['detailed_probes']['capture_usable'], False)

    def test_marker_only_is_not_selected_event_evidence(self):
        _, result = self.decode([marker()])
        self.assertIs(result['producer_profile']['policy_valid'], True)
        self.assertIs(result['producer_profile']['capture_usable'], False)
        self.assertEqual(result['producer_profile']['selected_event_count'], 0)

    def test_nonmonotonic_generation_history_and_identity_checked(self):
        fixture = self.fixture()
        fixture.arm(10)
        self.append(fixture, [marker(), event(4, 200, 300)], 10)
        fixture.stop(10)
        fixture.arm(9)
        self.append(fixture, [marker(flags=2)], 9, 2)
        fixture.stop(9)
        wrong = fixture.call('decode', fixture.path, 9, '0:1')
        self.assertEqual(wrong.returncode, 1)
        current = fixture.call('decode', fixture.path, 9, fixture.identity)
        self.assertEqual(current.returncode, 3)
        decoded = fixture.rows(current)
        self.assertEqual(decoded[-1]['wrong_generation'], 2)
        self.assertEqual(len(decoded), 2)
        self.assertEqual(analyze(decoded)['producer_profile']['name'], 'invalid-fail-closed')

    def test_analyzer_independently_rejects_bad_metadata_or_policy(self):
        for data in [rows([marker(flags=2)]), rows([marker(), event(42, 200, 300)]), rows([marker(), marker()])]:
            with self.subTest(data=data):
                self.assertIs(analyze(data)['producer_profile']['capture_usable'], False)
        data = rows([marker(), event(4, 200, 300)])
        data[-1]['producer_profile_policy_valid'] = False
        self.assertIs(analyze(data)['producer_profile']['capture_usable'], False)
        bad = copy.deepcopy(data)
        bad[0]['clock_domain'] = 'unknown'
        with self.assertRaises(ValueError):
            analyze(bad)

    def test_exact_production_producer_regression(self):
        result = subprocess.run([sys.executable, str(HUB / 'components/dxmt/tests/vibewine/trace_profile_test.py')],
                                capture_output=True, text=True, timeout=90)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
