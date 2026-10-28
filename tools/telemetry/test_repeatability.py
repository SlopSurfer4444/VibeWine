"""Synthetic protocol fixtures only; no game or actual repeated-run evidence."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import repeatability_analysis as ra
from test_fixed_window import call, fixture, sample, SAMPLE1, SAMPLE2, HASH


def run(index, condition='A'):
    window = call()
    window['measurement_id'] = f'12345678-1234-4234-9234-{index:012x}'
    window['decoder_identity'] = f'1:{index}'
    return dict(id=f'run-{index}', condition=condition, processKey=f'pid:birth:{index}',
                normalExitEvidence='fixture observed normal exit', relaunchEvidence='fixture new birth/title',
                binding=dict(hubRevision='a'*40, wineRevision='b'*40, dxmtRevision=('c' if condition == 'A' else 'd')*40,
                             buildReceiptSha256='e'*64, installedManifestSha256='f'*64,
                             loadedModules={'d3d11.dll': ('1' if condition == 'A' else '2')*64}),
                controls={k: 'fixture-'+k for k in ra.CONTROLS}, window=window)


def runs():
    return [run(1), run(2), run(3, 'B'), run(4, 'B')]


class ComparisonTests(unittest.TestCase):
    def test_equal_results_with_distinct_candidates_never_claim_improvement(self):
        result = ra.compare(runs())
        self.assertTrue(result['comparable'])
        self.assertEqual(result['medianDeltaBMinusA'], 0)
        self.assertFalse(result['deltaExceedsObservedRepeatSpread'])
        self.assertIn('no-causal', result['attributionStatus'])

    def test_rejected_window_no_subset_or_numerical_verdict(self):
        data = runs()
        data[1]['window'] = call(fixture([100.1, 101, 102.1]))
        data[1]['window']['measurement_id'] = data[1]['id']
        result = ra.compare(data)
        self.assertFalse(result['comparable'])
        self.assertNotIn('medianDeltaBMinusA', result)

    def test_control_candidate_process_and_measurement_drift(self):
        for mutation in ('controls', 'binding', 'process', 'measurement', 'same-candidate'):
            data = runs()
            if mutation == 'controls':
                data[2]['controls']['cacheTreatment'] = 'cold-not-warm'
            elif mutation == 'binding':
                data[1]['binding']['loadedModules']['d3d11.dll'] = '3'*64
            elif mutation == 'process':
                data[1]['processKey'] = data[0]['processKey']
            elif mutation == 'same-candidate':
                for item in data[2:]:
                    item['binding'] = copy.deepcopy(data[0]['binding'])
            else:
                data[1]['window']['measurement_id'] = data[0]['window']['measurement_id']
                with self.assertRaises(ValueError):
                    ra.compare(data)
                continue
            self.assertFalse(ra.compare(data)['comparable'], mutation)

    def test_missing_repeat_and_exit_evidence_fail_closed(self):
        data = runs()
        data[1]['condition'] = 'B'
        self.assertFalse(ra.compare(data)['comparable'])
        data = runs()
        del data[0]['normalExitEvidence']
        with self.assertRaises(ValueError):
            ra.compare(data)

    def test_native_local_serial_is_not_cross_process_settings_identity(self):
        data = runs()
        data[2]['window']['expected']['revision'] += 1
        data[2]['window']['expected']['swapchain'] += 1
        self.assertTrue(ra.compare(data)['comparable'])

    def test_actual_observer_or_capture_replay_drift_rejected(self):
        data = runs()
        data[2]['window']['collector_sha256'] = '9'*64
        self.assertIn('actual_collector_changed', ra.compare(data)['rejection_reasons'])
        data = runs()
        data[2]['window']['decoder_identity'] = data[1]['window']['decoder_identity']
        data[2]['window']['generation'] = data[1]['window']['generation']
        self.assertIn('decoder_identity_and_generation_reused', ra.compare(data)['rejection_reasons'])

    def test_cli_recomputes_original_capture_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            manifest_runs = []
            for index, metadata in enumerate(runs(), 1):
                measurement = metadata['window']['measurement_id']
                refs = {}
                for key, value in [('capture', fixture()), ('begin', sample(99_999_999_950, SAMPLE1)), ('end', sample(102_000_100_050, SAMPLE2))]:
                    path = root / f'{index}-{key}.json'
                    if key == 'capture':
                        value[-1]['identity'] = f'1:{index}'
                        # Real strict parser must reject the second capture;
                        # a supplied accepted window cannot hide native loss.
                        if index == 2:
                            value[-1]['dropped_after'] = 1
                        path.write_text(''.join(json.dumps(v)+'\n' for v in value))
                    else:
                        value['measurement_id'] = measurement
                        path.write_text(json.dumps(value))
                    refs[key] = str(path)
                metadata['inputs'] = refs
                metadata['windowArguments'] = dict(window_seconds=2, expected_swapchain=11,
                    expected_dimensions=dict(width=1280, height=720), expected_revision=4,
                    expected_collector_sha256=HASH, measurement_id=measurement)
                manifest_runs.append(metadata)
            manifest = root / 'manifest.json'
            manifest.write_text(json.dumps(dict(schema=1, runs=manifest_runs)))
            output = root / 'comparison.json'
            command = [sys.executable, str(Path(ra.__file__)), str(manifest), '--output', str(output)]
            first = subprocess.run(command, capture_output=True, text=True, timeout=10)
            self.assertEqual(first.returncode, 3, first.stderr)
            result = json.loads(output.read_text())
            self.assertFalse(result['comparable'])
            self.assertIn('rejected_or_nonstationary_window:run-2', result['rejection_reasons'])
            previous = output.read_bytes()
            second = subprocess.run(command, capture_output=True, text=True, timeout=10)
            self.assertNotEqual(second.returncode, 0)
            self.assertEqual(previous, output.read_bytes())


if __name__ == '__main__':
    unittest.main()
