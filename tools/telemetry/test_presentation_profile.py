"""Actual binary decoder plus independent hostile presentation coverage tests."""
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from analyze import analyze
from test_analyze import event, rows
from test_strict_feedback import feedback
import test_trace_profile as profile_tests
from test_trace_profile import marker

HERE = Path(__file__).resolve().parent


def fixture_events():
    es = [marker(flags=3), event(70, 200, obj=11, seq=1, frame=1)]
    for queueframe, nativeframe, seconds in [(7, 101, 10.0), (8, 102, 10.016)]:
        base = 1_000_000_000 + queueframe*20_000_000
        es.extend(event(t, base+(t-20)*2_000_000, obj=99, seq=queueframe+1,
                        frame=queueframe, flags=4 if t==23 else 0) for t in range(20, 24))
        es += [event(24, 9_000_000_000_000+queueframe*1000, 9_000_000_000_500+queueframe*1000,
                     obj=99, seq=queueframe+1, frame=queueframe, flags=4),
               event(25, base+1_000_000, obj=11, seq=99, frame=queueframe),
               event(99, base+1_000_000, obj=11, seq=nativeframe, frame=queueframe),
               event(40, base, base+8_000_000, obj=11),
               event(96, base+7_000_000, base+7_001_000, obj=99, seq=queueframe, frame=queueframe+1-3, flags=3),
               event(98, base+2_000_001, base+2_002_001, obj=11, seq=nativeframe, frame=4)]
        es += feedback(nativeframe, seconds)
    return es


def complete_rows(es=None):
    data = rows(fixture_events() if es is None else es)
    data[-1].update(structurally_complete=True, producer_quiescence_verified=True,
                    boundary_rejected_total=0, producer_profile_policy_valid=True)
    return data


class PresentationTraceTests(unittest.TestCase):
    fixture = profile_tests.TraceProfileTests.fixture
    append = profile_tests.TraceProfileTests.append
    decode = profile_tests.TraceProfileTests.decode

    @classmethod
    def setUpClass(cls):
        profile_tests.TraceProfileTests.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls):
        profile_tests.TraceProfileTests.tearDownClass.__func__(cls)

    def test_binary_complete_distinct_counter_join_and_clock_domains(self):
        decoded, result = self.decode(fixture_events())
        self.assertTrue(result['presentation_capture']['capture_usable'], result['presentation_capture'])
        self.assertTrue(result['producer_profile']['capture_usable'])
        frames = result['presentation_capture']['joined_frames']
        self.assertEqual([(r['queue_frame'], r['native_frame']) for r in frames], [(7, 101), (8, 102)])
        self.assertEqual(result['presentation_capture']['frame_latency_wait']['samples'], 2)
        self.assertEqual(result['presentation_capture']['drawable_acquire']['median_ms'], .002)
        for e in decoded[:-1]:
            if e['type'] == 99:
                self.assertIsNone(e['duration_ns'])
            if 96 <= e['type'] <= 99:
                self.assertEqual(e['clock_domain'], 'cpu_steady_ns')
                self.assertTrue(e['presentation_identity_valid'])
        self.assertEqual(result['queue_chunk_metrics']['gpu_command_buffer_durations']['samples'], 2)
        self.assertIsNone(result['wave7']['native_presented_cadence'][0]['display_latency_ms'])

    def test_decoder_and_analysis_reject_schema_duplicates_and_excluded_events(self):
        corrupt = []
        for kind in (96, 97, 98, 99):
            e = event(kind, 100, 200, obj=99, seq=8, frame=6, flags=3)
            if kind == 97: e.update(padding1=5)
            if kind == 98: e.update(flags=0, padding1=4)
            if kind == 99: e.update(flags=0, end=100)
            for change in (dict(object=0), dict(thread=0), dict(end=99), dict(flags=0xffffffff)):
                corrupt.append([marker(flags=3), dict(e, **change)])
            corrupt.append([marker(flags=3), e, copy.deepcopy(e)])
        corrupt += [[marker(flags=3), event(t, 100, 200)] for t in (3, 42, 80, 93, 100)]
        for es in corrupt:
            with self.subTest(es=es):
                decoded, result = self.decode(es, 3)
                self.assertFalse(decoded[-1]['producer_profile_policy_valid'])
                self.assertFalse(result['presentation_capture']['capture_usable'])
        # The analyzer receives independently modified raw NDJSON, while the
        # controller receives the same corrupt records as real binary fields.
        for kind, change in [(96,dict(padding1=3)),(96,dict(padding0=0)),
                             (97,dict(padding1=2)),(98,dict(padding0=0)),
                             (98,dict(padding1=0)),(99,dict(padding0=0))]:
            es=fixture_events()+[event(97,100,200,obj=11,seq=8,frame=5,flags=3)]
            next(e for e in es if e['type']==kind).update(change)
            self.decode(es,3)
            self.assertFalse(analyze(complete_rows(es))['presentation_capture']['capture_usable'])

    def test_missing_duplicate_stale_and_unbound_identity_fail_independently(self):
        data = complete_rows()
        self.assertTrue(analyze(data)['presentation_capture']['capture_usable'])
        cases = []
        for kind in (20, 21, 22, 23, 24, 25, 40, 70, 71, 72, 73, 96, 98, 99):
            cases.append([e for e in fixture_events() if e['type'] != kind])
        for kind in (20, 24, 25, 71, 72, 73, 96, 98, 99):
            es = fixture_events(); es.append(copy.deepcopy(next(e for e in es if e['type'] == kind))); cases.append(es)
        for kind, change in [(99, dict(padding0=7)), (99, dict(padding1=1001)), (25, dict(padding0=0)),
                             (98, dict(padding1=5)), (96, dict(object=0)), (71, dict(object=0)),
                             (72, dict(padding1=0)), (23, dict(flags=5))]:
            es = fixture_events(); next(e for e in es if e['type'] == kind).update(change); cases.append(es)
        for es in cases:
            with self.subTest(es=es):
                self.assertFalse(analyze(complete_rows(es))['presentation_capture']['capture_usable'])
        stale = complete_rows(); stale[1]['generation'] = 6
        with self.assertRaises(ValueError): analyze(stale)
        wrong_clock = complete_rows(); next(e for e in wrong_clock if e.get('type')==98)['clock_domain']='native_gpu_ns'
        with self.assertRaises(ValueError): analyze(wrong_clock)

    def test_loss_incomplete_stop_native_failure_and_wrong_profile(self):
        for field, value in [('dropped_after', 1), ('uncommitted_reserved', 1), ('boundary_rejected_total', 1),
                             ('structurally_complete', False), ('producer_quiescence_verified', False),
                             ('counters_changed', True), ('inconclusive_window', True), ('identity', '')]:
            data=complete_rows(); data[-1][field]=value
            self.assertFalse(analyze(data)['presentation_capture']['capture_usable'])
        for flags in (1, 2, 4):
            es=fixture_events();es[0]['flags']=flags
            self.assertFalse(analyze(complete_rows(es))['presentation_capture']['capture_usable'])
        for e in (event(76, 100, obj=11, flags=6, seq=101, frame=4), event(77, 100, obj=11, flags=4, seq=4),
                  event(78, 100, obj=11, seq=1, frame=1)):
            self.assertFalse(analyze(complete_rows(fixture_events()+[e]))['presentation_capture']['capture_usable'])
        _, result=self.decode([marker(flags=3)])
        self.assertFalse(result['producer_profile']['capture_usable'])

    def test_rejected_native_controls_and_failed_present_cannot_hide(self):
        for e in (event(75,100,101,obj=11,flags=4,seq=2,frame=3),
                  event(74,100,obj=11,flags=3,seq=0,frame=7),
                  event(70,100,101,obj=11,seq=1,frame=1)):
            es=fixture_events()+[e]
            result=analyze(complete_rows(es))['presentation_capture']
            self.assertFalse(result['capture_usable'],result)
            self.assertIn('rejected_trace_record',result['failures'])
        for es in ([event(75,100,obj=11)], [event(78,100,obj=11,seq=1)],
                   [event(75,100,obj=11),event(78,101,obj=11,seq=1)],
                   [event(75,100,obj=11),event(78,100,obj=11,seq=1),event(75,100,obj=11)]):
            self.assertFalse(analyze(complete_rows(fixture_events()+es))['presentation_capture']['capture_usable'])
        paired=fixture_events()+[event(75,100,obj=11),event(78,100,obj=11,seq=1)]
        self.assertTrue(analyze(complete_rows(paired))['presentation_capture']['capture_usable'])
        for mutate in ('failed','missing','duplicate'):
            es=fixture_events(); api=next(e for e in es if e['type']==40)
            if mutate=='failed':api['flags']=0x80004005
            elif mutate=='missing':es.remove(api)
            else:es.append(copy.deepcopy(api))
            result=analyze(complete_rows(es))['presentation_capture']
            self.assertFalse(result['capture_usable'],result)

    def test_expected_profile_cli_rejects_legacy_and_partial_capture(self):
        with tempfile.TemporaryDirectory(prefix='presentation-analysis-') as td:
            path=Path(td)/'input.jsonl'; output=Path(td)/'result.json'
            for es, code in [(fixture_events(), 0), ([marker(flags=3)], 3), ([event(42, 100, 200)], 3)]:
                path.write_text(''.join(json.dumps(r)+'\n' for r in complete_rows(es)))
                result=subprocess.run([sys.executable,str(HERE/'analyze.py'),str(path),'--output',str(output),
                                       '--expected-profile','presentation-waits'],capture_output=True,text=True,timeout=15)
                self.assertEqual(result.returncode,code,result.stderr)
                self.assertTrue(output.is_file())

if __name__ == '__main__':
    unittest.main()
