"""Producer-supported unavailable feedback is a gap, never presented proof."""
import copy
import math
import struct
import unittest
from test_analyze import event
from test_combined_profile import combined_events, report
from test_strict_feedback import feedback


def bits(value):
    return struct.unpack('<Q', struct.pack('<d', value))[0]


def unavailable(es):
    next(e for e in es if e['type'] == 71).update(flags=1, padding1=0)
    return es


def close_records(status=0, pending=3, abandoned=0, discarded=0, accounting_status=0):
    return [event(75, 4_000_000, obj=11, seq=pending, frame=abandoned, flags=status),
            event(78, 4_000_000, obj=11, seq=2, frame=discarded, flags=accounting_status)]


class PartialNativeObservations(unittest.TestCase):
    def assert_shared_admitted_native_incomplete(self, result):
        self.assertTrue(result['producer_profile']['policy_valid'], result['producer_profile'])
        self.assertTrue(result['producer_profile']['capture_usable'], result['producer_profile'])
        self.assertFalse(result['presentation_capture']['capture_usable'])
        self.assertTrue(result['compiler_stage_attribution']['capture_usable'])
        self.assertTrue(result['helper_compile_scopes']['capture_usable'])
        self.assertFalse(result['presentation_capture']['observed_contradictions'])

    def test_exact_zero_is_retained_without_global_contradiction_or_presented_proof(self):
        es = unavailable(combined_events())
        result = report(es)
        self.assert_shared_admitted_native_incomplete(result)
        self.assertIn('unavailable_native_presented_time', result['presentation_capture']['failures'])
        self.assertNotIn('invalid_native_presented_timestamp_or_domain_flags', result['rejections'])
        raw = result['wave7']['raw_native_feedback_records']
        zero = next(e for e in raw if e['type'] == 71 and e['padding1'] == 0)
        self.assertEqual(zero['flags'], 1)
        self.assertEqual(len(raw), 6)
        frames = result['wave7']['native_presented_frames']
        self.assertFalse(frames[0]['strict_accepted'])
        self.assertIn('presented_time_unavailable', frames[0]['strict_rejection_reasons'])
        self.assertEqual(len(result['wave7']['unavailable_native_presented_times']), 1)

    def test_unavailable_frame_never_bridges_consecutive_native_cadence(self):
        es = combined_events()
        # Keep frame101 positive, make102 unavailable, append103/104 positive.
        next(e for e in es if e['type'] == 71 and e['padding0'] == 102).update(flags=1, padding1=0)
        es += feedback(103, 10.032) + feedback(104, 10.048)
        result = report(es)
        cadence = result['wave7']['native_presented_cadence'][0]
        self.assertEqual(cadence['missing_identity_gaps'], 1)
        self.assertEqual(cadence['intervals']['samples'], 1)
        self.assertAlmostEqual(cadence['intervals']['median_ms'], 16)
        self.assertFalse(result['presentation_capture']['capture_usable'])
        self.assertEqual(cadence['strict_accepted_frames'], 3)

    def test_validity_domain_reserved_flags_and_bad_values_remain_contradictions(self):
        for flags, payload in ((257, 0), (1, bits(10.)), (2, 0), (0x201, 0),
                               (1, bits(-1.)), (1, bits(float('nan'))),
                               (1, bits(float('inf'))), (1, bits(-0.))):
            with self.subTest(flags=flags, payload=payload):
                es = combined_events()
                next(e for e in es if e['type'] == 71).update(flags=flags, padding1=payload)
                result = report(es)
                self.assertFalse(result['producer_profile']['capture_usable'])
                self.assertIn('invalid_native_presented_timestamp_or_domain_flags',
                              result['presentation_capture']['observed_contradictions'])

    def test_unavailable_payload_does_not_conceal_malformed_companions_or_identity(self):
        for kind, change in ((71, dict(object=0)), (71, dict(padding0=0)),
                             (71, dict(thread=0)), (71, dict(end=999)),
                             (72, dict(start=999, end=999)), (72, dict(flags=1)),
                             (72, dict(padding1=0)), (73, dict(padding1=0))):
            with self.subTest(kind=kind, change=change):
                es = unavailable(combined_events())
                next(e for e in es if e['type'] == kind).update(change)
                result = report(es)
                self.assertFalse(result['producer_profile']['capture_usable'])
                self.assertTrue(result['presentation_capture']['observed_contradictions'])
        for kind in (71, 72, 73):
            es = unavailable(combined_events())
            es.append(copy.deepcopy(next(e for e in es if e['type'] == kind)))
            result = report(es)
            self.assertFalse(result['producer_profile']['capture_usable'])
            self.assertIn('duplicate_native_feedback_frame_type',
                          result['presentation_capture']['observed_contradictions'])

    def test_successful_pending_close_is_native_coverage_gap(self):
        result = report(combined_events() + close_records())
        self.assert_shared_admitted_native_incomplete(result)
        self.assertIn('native_close_pending_handlers', result['presentation_capture']['failures'])
        self.assertEqual(result['wave7']['native_session_closes'][0]['live_handler_tickets_at_close'], 3)

    def test_close_errors_losses_duplicates_and_counter_regressions_stay_rejected(self):
        cases = [close_records(status=2), close_records(abandoned=1),
                 close_records(discarded=1), close_records(accounting_status=2),
                 close_records() + [copy.deepcopy(close_records()[0])],
                 close_records() + [copy.deepcopy(close_records()[1])]]
        # Full seven-counter snapshots with accepted regressing at a later receipt.
        cases.append([event(74, at, obj=11, seq=2, frame=(accepted if index == 0 else 0), flags=index)
                      for at, accepted in ((5_000_000, 10), (6_000_000, 9)) for index in range(7)])
        for extra in cases:
            with self.subTest(extra=extra):
                result = report(combined_events() + extra)
                self.assertFalse(result['producer_profile']['capture_usable'])
                self.assertTrue(result['presentation_capture']['observed_contradictions'])
                self.assertFalse(result['helper_compile_scopes']['capture_usable'])


if __name__ == '__main__':
    unittest.main()
