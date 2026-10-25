"""Additive visibility decoder semantics; historical traces keep their meaning."""
import os, subprocess, tempfile, unittest
from pathlib import Path
import tests_cli
import test_trace_profile as profile_tests
from test_trace_profile import marker
from analyze import analyze
from test_analyze import event
from visibility_analysis import visibility_analysis
GOOD=dict(structurally_complete=True,producer_quiescence_verified=True,counters_changed=False,inconclusive_window=False,producer_profile_policy_valid=True,dropped_before=0,dropped_after=0,uncommitted_reserved=0,boundary_rejected_total=0)
def sample(status=0,flags=43,age=1,stamp=100):
    es=[event(100,stamp,obj=11,seq=9,frame=1,flags=status),event(101,stamp,obj=11,seq=99,frame=age,flags=flags),event(102,stamp,obj=11,seq=123,frame=2),event(103,stamp,obj=11,seq=2 if flags&32 else 0,frame=4)]
    for i,e in enumerate(es):e.update(index=i,generation=7,clock_domain='cpu_steady_ns')
    return es
def cached_samples():
    return sample(age=1,stamp=100)+sample(age=100,stamp=200)+sample(status=4,age=1_000_000_001,stamp=300)
def snapshot_conflicts():
    # Each mutation remains valid within its own group: only the repeated
    # immutable native identity reveals the contradiction.
    for kind,changes in ((1,dict(flags=35)),(1,dict(padding0=100)),
                         (2,dict(padding0=124)),(2,dict(padding1=3)),
                         (3,dict(padding0=8194))):
        repeated=sample(age=100,stamp=200);repeated[kind].update(changes)
        yield sample()+repeated
class VisibilityTests(unittest.TestCase):
    def test_fresh_and_legacy(self):
        s=visibility_analysis(sample(),GOOD);self.assertEqual(s['visible_on_active_space_sample_count'],1);self.assertIsNone(s['displayed_fps']);self.assertFalse(s['continuous_visibility_verified']);self.assertTrue(s['visibility_evidence_usable'])
        s=visibility_analysis([],GOOD);self.assertEqual(s['coverage'],'not_observed_legacy_or_disabled');self.assertEqual(s['fresh_sample_count'],0);self.assertFalse(s['visibility_evidence_usable'])
    def test_visible_off_space_and_hidden_are_not_visible_evidence(self):
        for flag in (35,43|4,43|16,43|64,43|128):
            s=visibility_analysis(sample(flags=flag),GOOD);self.assertEqual(s['visible_on_active_space_sample_count'],0);self.assertTrue(s['sample_fields_valid'])
    def test_unknown_stale_unsupported_and_expired_are_fail_closed(self):
        for status in (1,2,3,4):
            s=visibility_analysis(sample(status=status),GOOD);self.assertEqual(s['fresh_sample_count'],0);self.assertEqual(s['visible_on_active_space_sample_count'],0)
        for age in (1_000_000_000,2**64-1):
            self.assertFalse(visibility_analysis(sample(age=age),GOOD)['sample_fields_valid'])
    def test_partial_duplicate_malformed_and_capture_loss(self):
        for es in (sample()[:-1],sample()+[sample()[0]]):self.assertFalse(visibility_analysis(es,GOOD)['sample_fields_valid'])
        for kind,changes in ((0,dict(object=0)),(1,dict(flags=256)),(1,dict(padding0=0)),(2,dict(flags=1)),(3,dict(padding0=0)),(0,dict(end=101))):
            es=sample();es[kind].update(changes);self.assertFalse(visibility_analysis(es,GOOD)['sample_fields_valid'])
        for key in ('dropped_before','uncommitted_reserved','boundary_rejected_total'):
            bad=dict(GOOD,**{key:1});s=visibility_analysis(sample(),bad);self.assertFalse(s['capture_integrity_ok']);self.assertFalse(s['sample_fields_valid']);self.assertFalse(s['visibility_evidence_usable'])
    def test_unverified_or_changed_capture_is_not_usable(self):
        for field in ('counters_changed','inconclusive_window','producer_profile_policy_valid'):
            for value in (None,not GOOD[field]):
                bad=dict(GOOD,**{field:value});self.assertFalse(visibility_analysis(sample(),bad)['visibility_evidence_usable'])
    def test_cached_snapshot_allows_age_status_and_configuration_changes(self):
        es=cached_samples();es[-1]['padding1']=5
        result=visibility_analysis(es,GOOD)
        self.assertTrue(result['sample_fields_valid'],result)
        self.assertTrue(result['visibility_evidence_usable'])
        self.assertEqual(len(result['samples']),3)
        self.assertEqual([s['status'] for s in result['samples']],['fresh','fresh','stale'])
    def test_conflicting_repeated_snapshot_payload_is_not_usable(self):
        for es in snapshot_conflicts():
            result=visibility_analysis(es,GOOD)
            self.assertIn('conflicting_visibility_snapshot_identity',result['failures'])
            self.assertFalse(result['sample_fields_valid']);self.assertFalse(result['visibility_evidence_usable'])
    def test_distinct_snapshot_sequences_may_change_visibility(self):
        es=sample()+sample(flags=35,stamp=200)
        es[4]['padding1']=2;es[5]['padding0']=100
        self.assertTrue(visibility_analysis(es,GOOD)['sample_fields_valid'])
    def test_absent_host_id_is_unknown_identity(self):
        es=sample();es[2]['padding0']=0;s=visibility_analysis(es,GOOD);self.assertTrue(s['sample_fields_valid']);self.assertFalse(s['samples'][0]['host_known']);self.assertEqual(s['visible_on_active_space_sample_count'],0)

class VisibilityBinaryTests(unittest.TestCase):
    fixture=profile_tests.TraceProfileTests.fixture
    append=profile_tests.TraceProfileTests.append
    decode=profile_tests.TraceProfileTests.decode
    @classmethod
    def setUpClass(cls):
        cls.previous_controller=tests_cli.CONTROLLER
        cls.scratch=tempfile.TemporaryDirectory(prefix='visibility-decoder-');out=Path(cls.scratch.name)
        here=Path(__file__).resolve().parent
        component=Path(os.environ.get('DXMT_VISIBILITY_TEST_COMPONENT',here.parents[1]/'components/dxmt'))
        for name in ('trace_controller.cpp','trace_core.hpp','probe_semantics.hpp'):
            text=(here/name).read_text()
            if name=='trace_core.hpp':text=text.replace('../../components/dxmt/src/d3d11/cs2_trace_protocol.hpp',str(component/'src/d3d11/cs2_trace_protocol.hpp'))
            (out/name).write_text(text)
        binary=out/'controller'
        subprocess.run(['xcrun','clang++','-std=c++20','-O1','-Wall','-Wextra','-Werror',str(out/'trace_controller.cpp'),'-framework','QuartzCore','-o',str(binary)],check=True,timeout=60)
        tests_cli.CONTROLLER=binary
    @classmethod
    def tearDownClass(cls):
        tests_cli.CONTROLLER=cls.previous_controller;cls.scratch.cleanup()
    def test_actual_binary_additive_group_and_partial_rejection(self):
        decoded,result=self.decode([marker(flags=3)]+sample())
        self.assertTrue(decoded[-1]['producer_profile_policy_valid'])
        evidence=result['visibility_observations'];self.assertTrue(evidence['sample_fields_valid'],evidence)
        for e in decoded[1:5]:
            self.assertEqual(e['clock_domain'],'cpu_steady_ns');self.assertIsNone(e['duration_ns']);self.assertTrue(e['visibility_sample_only'])
        for es in (sample()[:-1],sample()+[sample()[0]]):
            decoded,result=self.decode([marker(flags=3)]+es,expected_code=3)
            self.assertFalse(result['visibility_observations']['sample_fields_valid'])
    def test_actual_binary_cached_snapshot_accepts_age_and_expiry(self):
        es=cached_samples();es[-1]['padding1']=5
        decoded,result=self.decode([marker(flags=3)]+es)
        self.assertTrue(decoded[-1]['producer_profile_policy_valid'])
        self.assertTrue(result['visibility_observations']['visibility_evidence_usable'])
    def test_actual_binary_conflicting_snapshot_identity_fails_closed(self):
        for es in snapshot_conflicts():
            decoded,result=self.decode([marker(flags=3)]+es,expected_code=3)
            self.assertFalse(decoded[-1]['producer_profile_policy_valid'])
            evidence=result['visibility_observations']
            self.assertIn('conflicting_visibility_snapshot_identity',evidence['failures'])
            self.assertFalse(evidence['visibility_evidence_usable'])
    def test_actual_binary_old_profile_remains_supported(self):
        decoded,result=self.decode([marker(flags=3)])
        self.assertTrue(decoded[-1]['producer_profile_policy_valid'])
        self.assertEqual(result['visibility_observations']['coverage'],'not_observed_legacy_or_disabled')

if __name__=='__main__':unittest.main()
