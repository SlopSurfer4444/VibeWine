"""Cross-profile binary policy, shared quota and opaque helper-span semantics."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from analyze import analyze
from test_analyze import event
from test_detail_analyze import task
from test_compiler_attribution import reader, stage
from test_completion_cards import card
from test_presentation_profile import fixture_events, complete_rows
from test_trace_profile import marker as old_marker
import test_trace_profile as profile_fixture


def marker(limit=100, **changes):return old_marker(flags=5, padding0=limit, **changes)
def helper(route=1, owner=501, serial=900, start=3_100_000, end=3_200_000, flags=1, **changes):
    e=event(120,start,end,obj=serial,seq=route,frame=owner,flags=flags);e.update(changes);return e
def report(es, **summary):
    data=complete_rows(es);data[-1].update(summary);return analyze(data)
def combined_events():
    es=fixture_events();es[0]=marker()
    return es+task()+reader()+[stage(),helper(),helper(2,serial=901,start=3_300_000,end=3_400_000)]+card()


class CombinedProfileTests(unittest.TestCase):
    def test_cross_family_joins_preserve_domains(self):
        r=report(combined_events())
        for family in ('producer_profile','presentation_capture','compiler_stage_attribution','helper_compile_scopes'):
            self.assertTrue(r[family]['capture_usable'],r[family])
        self.assertTrue(r['stall_capture']['completion_cards']['capture_usable'])
        h=r['helper_compile_scopes'];self.assertEqual(len(h['tessellation_owner_pairs']),1)
        self.assertTrue(all(e['task_join']=='unavailable_owner_is_not_task_identity' for e in h['spans']))
        self.assertEqual(r['compiler_stage_attribution']['stages'][0]['task_join'],'joined_observed_task_run')
        self.assertEqual(r['presentation_capture']['joined_frames'][0]['native_frame'],101)
        for field in ('summed_cpu_cost_ms','gpu_time_ms','fps'):self.assertIsNone(h[field])

    def test_helper_only_and_ordinary_only_missing_families_unknown(self):
        r=report([marker(),helper()]);self.assertTrue(r['producer_profile']['capture_usable'])
        self.assertTrue(r['helper_compile_scopes']['capture_usable'])
        for family in ('presentation_capture','compiler_stage_attribution','stall_capture'):self.assertEqual(r[family]['coverage'],'unknown')
        self.assertFalse(r['stall_capture']['capture_usable'])
        self.assertEqual(r['helper_compile_scopes']['spans'][0]['owner_join'],'missing_or_ambiguous_partner')
        r=report([marker(0),event(42,200,300)]);self.assertTrue(r['producer_profile']['capture_usable'])
        self.assertEqual(r['helper_compile_scopes']['coverage'],'unknown')
        self.assertIn('unknown',r['helper_compile_scopes']['missing_helpers'])
        self.assertFalse(report([marker(0),helper()])['producer_profile']['policy_valid'])
        self.assertFalse(report([marker()])['producer_profile']['capture_usable'])

    def test_routes_results_zero_wall_and_owner_unknown_valid(self):
        for route in range(1,6):
            for flags in (1,2,3):
                h=report([marker(),helper(route,owner=0,flags=flags,end=3_100_000)])['helper_compile_scopes']
                self.assertTrue(h['capture_usable']);e=h['spans'][0]
                self.assertEqual(e['observed_cpu_wall_duration_ms'],0);self.assertIsNone(e['owner_birth_serial'])
                self.assertEqual(e['owner_join'],'unknown_owner');self.assertFalse(h['owner_groups'])
        h=report([marker()]+task()+[helper(flags=3)])['helper_compile_scopes']
        self.assertTrue(h['capture_usable']);self.assertEqual(h['spans'][0]['result'],'unknown')

    def test_opaque_owners_never_join_tasks_or_other_routes(self):
        h=report([marker()]+task()+[helper(3),helper(4,serial=901),helper(5,serial=902)])['helper_compile_scopes']
        self.assertTrue(h['capture_usable']);self.assertFalse(h['tessellation_owner_pairs']);self.assertEqual(len(h['owner_groups']),1)
        self.assertTrue(all(e['task_join']=='unavailable_owner_is_not_task_identity' for e in h['spans']))
        h=report([marker(),helper(owner=0),helper(2,owner=0,serial=901)])['helper_compile_scopes']
        self.assertFalse(h['tessellation_owner_pairs'])

    def test_missing_ambiguous_foreign_owner_partners_unknown(self):
        for spans in ([helper()],[helper(2)],[helper(),helper(2,owner=502,serial=901)],
                      [helper(),helper(serial=901),helper(2,serial=902)]):
            h=report([marker()]+spans)['helper_compile_scopes']
            self.assertTrue(h['capture_usable']);self.assertFalse(h['tessellation_owner_pairs'])

    def test_pair_thread_chronology_disagreement_rejected(self):
        for change in (dict(thread=3),dict(start=3_199_999)):
            e=helper(2,serial=901,start=3_200_000,end=3_400_000);e.update(change)
            r=report([marker(),helper(),e]);self.assertFalse(r['producer_profile']['capture_usable'])
            self.assertFalse(r['helper_compile_scopes']['fields_valid']);self.assertFalse(r['helper_compile_scopes']['tessellation_owner_pairs'])
        self.assertTrue(report([marker(),helper(),helper(2,serial=901,start=3_200_000)])['helper_compile_scopes']['capture_usable'])

    def test_malformed_fields_duplicate_clock_generation_fail_closed(self):
        for change in (dict(object=0),dict(thread=0),dict(start=0),dict(end=3_000_000),dict(flags=0),dict(flags=4),dict(padding0=0),dict(padding0=6)):
            r=report([marker(),helper(**change)])
            self.assertFalse(r['producer_profile']['policy_valid']);self.assertFalse(r['helper_compile_scopes']['capture_usable'])
            self.assertIsNone(r['helper_compile_scopes']['spans'][0]['observed_cpu_wall_duration_ms'])
        r=report([marker(),helper(),helper(3)]);self.assertFalse(r['producer_profile']['policy_valid'])
        self.assertTrue(all(not e['valid'] for e in r['helper_compile_scopes']['spans']))
        for clock in ('native_gpu_ns','unknown','ca_host_seconds'):
            with self.assertRaises(ValueError):report([marker(),helper(clock_domain=clock)])
        data=complete_rows([marker(),helper()]);data[1]['generation']=8
        with self.assertRaises(ValueError):analyze(data)

    def test_shared_cap_counts_all_combined_detail_families(self):
        details=[event(80,1000,obj=501,flags=1,seq=100,frame=1),event(90,1000,2000,obj=99,seq=1),
                 event(93,1000,2000,obj=300,seq=600,flags=100),stage(),helper()]+card()
        self.assertTrue(report([marker(len(details))]+details)['producer_profile']['policy_valid'])
        self.assertFalse(report([marker(len(details)-1)]+details)['producer_profile']['policy_valid'])
        for t in list(range(80,95))+list(range(104,108))+list(range(111,121)):
            e=helper() if t==120 else event(t,100_000_000,150_000_000)
            self.assertIn('combined_detail_budget_exceeded_or_invalid_cap_marker',report([marker(0),e])['producer_profile']['failures'])

    def test_cap_sentinel_uncharged_but_capture_incomplete(self):
        cap=event(89,4_000_000,obj=0,flags=1,seq=1)
        r=report([marker(1),helper(),cap]);self.assertTrue(r['producer_profile']['policy_valid'])
        self.assertFalse(r['producer_profile']['capture_usable']);self.assertFalse(r['helper_compile_scopes']['capture_usable'])
        self.assertAlmostEqual(r['helper_compile_scopes']['spans'][0]['observed_cpu_wall_duration_ms'],.1)
        for change in (dict(padding0=2),dict(object=1),dict(thread=0),dict(end=4_000_001),dict(padding1=1)):
            self.assertFalse(report([marker(1),helper(),dict(cap,**change)])['producer_profile']['policy_valid'])
        self.assertFalse(report([marker(2),helper(),cap])['producer_profile']['policy_valid'])
        self.assertFalse(report([marker(1),helper(),cap,dict(cap)])['producer_profile']['policy_valid'])

    def test_loss_unknown_counters_boundary_quiescence_incomplete(self):
        for change in (dict(inconclusive_window=True),dict(counters_changed=True),dict(structurally_complete=False),
                       dict(producer_quiescence_verified=False),dict(producer_profile_policy_valid=False),
                       dict(dropped_before=1),dict(dropped_after=1),dict(uncommitted_reserved=1),
                       dict(boundary_rejected_total=1),dict(boundary_rejected_total=None),dict(identity=None)):
            r=report([marker(),helper()],**change)
            self.assertFalse(r['producer_profile']['capture_usable']);self.assertFalse(r['helper_compile_scopes']['capture_usable'])
        data=complete_rows([marker(),helper()]);del data[-1]['dropped_after']
        self.assertFalse(analyze(data)['helper_compile_scopes']['capture_usable'])

    def test_finish_threshold_compiler_and_helper_uncensored(self):
        for t in range(108,120):
            r=report([marker(),event(t,1_000_000_000,1_049_999_999,obj=99,seq=17)])
            self.assertIn('below_stall_duration_threshold',r['producer_profile']['failures'])
        self.assertTrue(report([marker(),event(108,1_000_000_000,1_050_000_000,obj=99,seq=17)])['producer_profile']['policy_valid'])
        r=report([marker()]+task()+[stage(),helper()]);self.assertTrue(r['producer_profile']['capture_usable'])
        self.assertEqual(r['compiler_stage_attribution']['coverage'],'observed_scopes')
        self.assertEqual(r['compiler_stage_attribution']['stages'][0]['observed_cpu_wall_duration_ms'],.1)

    def test_observed_presentation_contradictions_reject_shared_profile(self):
        es=combined_events();next(e for e in es if e['type']==98)['padding1']=5
        r=report(es);self.assertFalse(r['producer_profile']['capture_usable'])
        self.assertIn('drawable_feedback_revision_disagreement',r['presentation_capture']['observed_contradictions'])
        for family in ('compiler_stage_attribution','presentation_capture','helper_compile_scopes'):
            self.assertFalse(r[family]['capture_usable'])
        es=combined_events();next(e for e in es if e['type']==120 and e['padding0']==2)['thread']=3
        r=report(es);self.assertFalse(r['producer_profile']['policy_valid'])
        for family in ('compiler_stage_attribution','presentation_capture','helper_compile_scopes'):
            self.assertFalse(r[family]['capture_usable'])

    def test_partial_native_context_unknown_but_malformed_or_positive_loss_rejected(self):
        for partial in ([event(75,100,obj=11)], [event(74,100,obj=11,seq=1)],
                        [event(72,100,obj=11,seq=1,frame=4)]):
            r=report([marker(),helper()]+partial)
            self.assertTrue(r['producer_profile']['capture_usable'],r['presentation_capture'])
            self.assertFalse(r['presentation_capture']['capture_usable'])
        for malformed in ([event(74,100,obj=11)],
                          [event(74,100,obj=11,seq=1,flags=3,frame=1)],
                          [event(75,100,obj=11),event(75,100,obj=11)],
                          [event(74,100,obj=11,seq=1),event(74,100,obj=11,seq=1)]):
            r=report([marker(),helper()]+malformed)
            self.assertFalse(r['producer_profile']['capture_usable'],r['presentation_capture'])
            self.assertFalse(r['helper_compile_scopes']['capture_usable'])

    def test_legacy_report_shapes_do_not_gain_combined_fields(self):
        for es in ([event(42,100,200)],[old_marker(),stage()],fixture_events(),[old_marker(flags=4)]+card()):
            r=report(es)
            self.assertNotIn('helper_compile_scopes',r)
            self.assertNotIn('coverage',r['presentation_capture'])
            self.assertNotIn('observed_contradictions',r['presentation_capture'])

    def test_old_profiles_reject_helper_expected_cli(self):
        for profile in (None,1,3,4):
            self.assertFalse(report(([old_marker(flags=profile)] if profile else [])+[helper()])['producer_profile']['policy_valid'])
        with tempfile.TemporaryDirectory() as directory:
            source,output=Path(directory)/'in.ndjson',Path(directory)/'out.json'
            for es,code in (([marker(),helper()],0),([marker()],3),([old_marker(),helper()],3)):
                source.write_text(''.join(json.dumps(r)+'\n' for r in complete_rows(es)))
                result=subprocess.run([sys.executable,str(Path(__file__).with_name('analyze.py')),str(source),'--output',str(output),'--expected-profile','combined-bounded'],capture_output=True,text=True,timeout=15)
                self.assertEqual(result.returncode,code,result.stderr);self.assertTrue(output.is_file())


class CombinedBinaryTests(unittest.TestCase):
    setUpClass=classmethod(profile_fixture.TraceProfileTests.setUpClass.__func__)
    tearDownClass=classmethod(profile_fixture.TraceProfileTests.tearDownClass.__func__)
    fixture=profile_fixture.TraceProfileTests.fixture
    append=profile_fixture.TraceProfileTests.append
    decode=profile_fixture.TraceProfileTests.decode

    def test_binary_cross_family_and_helper_schema(self):
        decoded,r=self.decode(combined_events());self.assertTrue(decoded[-1]['producer_profile_policy_valid'])
        self.assertTrue(r['producer_profile']['capture_usable']);self.assertEqual(decoded[0]['producer_profile'],'combined-bounded')
        for e in (e for e in decoded[:-1] if e['type']==120):
            self.assertEqual(e['clock_domain'],'cpu_steady_ns');self.assertTrue(e['helper_identity_valid'])
            self.assertEqual(e['duration_ns'],100_000);self.assertEqual(e['owner_birth_serial'],501)
        self.assertTrue(r['presentation_capture']['capture_usable']);self.assertTrue(r['stall_capture']['completion_cards']['capture_usable'])

    def test_binary_payload_duplicate_and_old_profiles_fail_closed(self):
        for change in (dict(object=0),dict(thread=0),dict(start=0),dict(end=3_000_000),dict(flags=0),dict(flags=4),dict(padding0=0),dict(padding0=6)):
            decoded,r=self.decode([marker(),helper(**change)],3)
            self.assertFalse(decoded[-1]['producer_profile_policy_valid']);self.assertIsNone(decoded[1]['duration_ns'])
            self.assertFalse(r['helper_compile_scopes']['capture_usable'])
        self.decode([marker(),helper(),helper(3)],3)
        for profile in (None,1,3,4):self.decode(([old_marker(flags=profile)] if profile else [])+[helper()],3)

    def test_binary_cap_threshold_and_no_detail(self):
        self.decode([marker(0),event(42,200,300)]);self.decode([marker(0),helper()],3)
        self.decode([marker(1),helper(),helper(3,serial=901)],3)
        decoded,r=self.decode([marker(1),helper(),event(89,4_000_000,obj=0,flags=1,seq=1)])
        self.assertTrue(decoded[-1]['producer_profile_policy_valid']);self.assertFalse(r['helper_compile_scopes']['capture_usable'])
        for t in range(108,120):self.decode([marker(),event(t,1_000_000_000,1_049_999_999,obj=99,seq=17)],3)
        self.decode([marker(),event(108,1_000_000_000,1_050_000_000,obj=99,seq=17)])
        self.decode([marker(),event(42,300,200)],3)
        e=event(121,200,300);e['clock_domain']='unknown';self.decode([marker(),e],3)


if __name__=='__main__':unittest.main()
