"""Offline complete-triplet acceptance; no live presentation or FPS claim."""
import copy
import struct
import unittest
from analyze import analyze
from test_analyze import event, rows


def feedback(frame,seconds,revision=4,width=1280,height=720):
    t=frame*100
    return [event(71,t,seq=frame,frame=struct.unpack('<Q',struct.pack('<d',seconds))[0],flags=0x101),
            event(72,t,seq=frame,frame=revision),event(73,t,seq=frame,frame=(width<<32)|height)]


class StrictFeedbackTests(unittest.TestCase):
    def check_bad_middle(self,change):
        middle=feedback(2,10.016);change(middle)
        es=feedback(1,10.0)+middle+feedback(3,10.032)
        result=analyze(rows(es));w=result['wave7']
        self.assertFalse(w['native_presented_frames'][1]['strict_accepted'])
        self.assertIsNone(w['native_presented_cadence'][0]['intervals'])
        self.assertEqual(len(w['raw_native_feedback_records']),len(es))
        return result

    def test_complete_neighbors(self):
        w=analyze(rows(feedback(1,10.0)+feedback(2,10.016)))['wave7']
        self.assertTrue(all(f['strict_accepted'] for f in w['native_presented_frames']))
        self.assertAlmostEqual(w['native_presented_cadence'][0]['intervals']['median_ms'],16)
        self.assertFalse(w['strict_acceptance']['expected_configuration_supplied'])

    def test_missing_companion(self):self.check_bad_middle(lambda es:es.pop(1))
    def test_duplicate_companion(self):self.check_bad_middle(lambda es:es.append(copy.deepcopy(es[2])))
    def test_bad_companion(self):self.check_bad_middle(lambda es:es[2].update(flags=1))
    def test_bad_duplicate_companion(self):self.check_bad_middle(lambda es:es.append(dict(es[2],flags=1)))
    def test_noninstant_duplicate(self):self.check_bad_middle(lambda es:es.append(dict(es[2],end=201)))
    def test_receipt_mismatch(self):self.check_bad_middle(lambda es:es[2].update(start=201,end=201))
    def test_noninstant_companion(self):self.check_bad_middle(lambda es:es[2].update(end=201))
    def test_invalid_dimensions(self):self.check_bad_middle(lambda es:es[2].update(padding1=0))
    def test_bad_timestamp(self):
        for value in [0.0,float('nan'),float('inf'),-1.0]:
            with self.subTest(value=value):
                self.check_bad_middle(lambda es:es[0].update(padding1=struct.unpack('<Q',struct.pack('<d',value))[0]))
    def test_bad_timestamp_flags(self):self.check_bad_middle(lambda es:es[0].update(flags=0x301))

    def test_timestamp_only_is_observation_not_cadence(self):
        w=analyze(rows([feedback(1,10)[0],feedback(2,10.016)[0]]))['wave7']
        self.assertEqual(len(w['native_presented_frames']),2)
        self.assertTrue(all(f['presented_time_valid'] for f in w['native_presented_frames']))
        self.assertEqual(w['native_presented_cadence'],[])

    def test_finite_timestamp_overflow_interval_is_not_exported(self):
        a=analyze(rows(feedback(1,10.0)+feedback(2,1e308)))
        self.assertEqual(a['rejections']['nonfinite_native_presentation_interval'],1)
        self.assertIsNone(a['wave7']['native_presented_cadence'][0]['intervals'])

    def test_configuration_transition_excluded(self):
        for kw in [{'revision':5},{'width':1920,'height':1080}]:
            with self.subTest(kw=kw):
                w=analyze(rows(feedback(1,10)+feedback(2,10.016,**kw)+feedback(3,10.032,**kw)))['wave7']
                c=w['native_presented_cadence'][0]
                self.assertEqual(c['intervals']['samples'],1)
                self.assertEqual(c['configuration_transition_intervals_excluded'],1)

    def test_expected_configuration_filters(self):
        es=feedback(1,10)+feedback(2,10.016)+feedback(3,10.032,revision=5)
        w=analyze(rows(es),expected_dimensions={'width':1280,'height':720},expected_revision=4)['wave7']
        self.assertTrue(w['strict_acceptance']['expected_configuration_supplied'])
        self.assertEqual(w['native_presented_cadence'][0]['intervals']['samples'],1)
        self.assertIn('unexpected_configuration_revision',w['native_presented_frames'][2]['strict_rejection_reasons'])
        w=analyze(rows(es),expected_dimensions={'width':1920,'height':1080})['wave7']
        self.assertEqual(w['native_presented_cadence'],[])
        for kwargs in [{'expected_dimensions':{'width':0,'height':720}},{'expected_dimensions':{'width':True,'height':720}},{'expected_revision':-1}]:
            with self.assertRaises(ValueError):analyze(rows(es),**kwargs)

    def test_unknown_identity_is_global_quality_failure(self):
        bad=dict(feedback(3,10.032)[2],object=0)
        a=analyze(rows(feedback(1,10)+feedback(2,10.016)+[bad]))
        self.assertFalse(a['wave7']['strict_acceptance']['capture_identity_quality_ok'])
        self.assertIn('unidentified_native_feedback_records',a['quality_limits'])
        self.assertEqual(len(a['wave7']['raw_native_feedback_records']),7)
        self.assertEqual(a['wave7']['native_presented_cadence'][0]['intervals']['samples'],1)

    def test_cache_known_clocks_only(self):
        analyze(rows([event(17,10,20,flags=1),event(19,100,200)]))
        with self.assertRaises(ValueError):analyze(rows([event(18,10,20)]))
        unknown=event(18,10,20);unknown['clock_domain']='unknown';analyze(rows([unknown]))

if __name__=='__main__':unittest.main()
