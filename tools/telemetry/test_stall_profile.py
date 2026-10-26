"""Independent offline policy checks plus real binary profile4 decoding."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from analyze import analyze
from test_analyze import event, rows
import test_trace_profile as profile_fixture

THRESHOLD=50_000_000
SLOW_TYPES={1,3,5,19}|set(range(6,17))|set(range(30,33))|set(range(40,50))|set(range(104,111))
CONTEXT_TYPES=set(range(80,90))|{93,94}

def marker(**changes):
    return profile_fixture.marker(flags=4,**changes)

def slow(t=42,duration=THRESHOLD,**changes):
    e=event(t,1_000_000_000,1_000_000_000+duration)
    if t in (30,31,32):e['padding0']=99
    if t==30:e['flags']=1
    if 104<=t<=107:
        e['flags']=1
        e['padding1' if t==104 else 'padding0']=17
    if 108<=t<=110:e['padding0']=17
    e.update(changes)
    return e

def complete(events):
    data=rows(events)
    data[-1].update(structurally_complete=True,producer_quiescence_verified=True,
                    boundary_rejected_total=0,producer_profile_policy_valid=True)
    return data

def retirement_chain(*types):
    types=types or (110,108,109)
    return [slow(t,start=1_000_000_000+i*THRESHOLD,end=1_000_000_000+(i+1)*THRESHOLD) for i,t in enumerate(types)]


class StallAnalyzerTests(unittest.TestCase):
    def test_exact_independent_allowlist_threshold_and_no_fps(self):
        for t in sorted(SLOW_TYPES):
            with self.subTest(t=t):
                report=analyze(complete([marker(),slow(t)]))
                self.assertTrue(report['producer_profile']['policy_valid'])
                self.assertTrue(report['stall_capture']['capture_usable'])
                scope=report['stall_capture']['slow_scopes'][0]
                self.assertEqual(scope['observed_cpu_wall_duration_ms'],50)
                self.assertFalse(report['cpu_present_streams'])
                self.assertIsNone(report['queue_chunk_metrics']['gpu_command_buffer_durations'])
                self.assertIsNone(report['stall_capture']['fps'])
                self.assertIsNone(report['stall_capture']['summed_cpu_busy_ms'])
                bad=analyze(complete([marker(),slow(t,THRESHOLD-1)]))
                self.assertFalse(bad['producer_profile']['policy_valid'])
                self.assertFalse(bad['stall_capture']['capture_usable'])
                self.assertFalse(bad['stall_capture']['slow_scopes'])

    def test_every_excluded_kind_rejected_even_if_decoder_claims_valid(self):
        for t in sorted(set(range(1,112))-SLOW_TYPES-CONTEXT_TYPES-{95}):
            e=slow(t)
            if t not in set(range(1,18))|{19,25,30,31,32,33,40,41,42,43,44,45,46,47,48,49,61,62}|set(range(20,24))|set(range(70,79))|set(range(80,111)):
                e['clock_domain']='native_gpu_ns' if t==24 else 'unknown'
            with self.subTest(t=t):
                self.assertFalse(analyze(complete([marker(),e]))['producer_profile']['policy_valid'])

    def test_marker_and_context_only_supply_no_slow_evidence(self):
        for es in ([],[event(93,200,300,obj=55,seq=9,flags=100)],
                   [event(94,200,300,obj=55,seq=9)]):
            report=analyze(complete([marker()]+es))
            self.assertTrue(report['producer_profile']['policy_valid'])
            self.assertFalse(report['producer_profile']['capture_usable'])
            self.assertFalse(report['stall_capture']['capture_usable'])
            self.assertEqual(report['producer_profile']['slow_scope_count'],0)

    def test_reverse_intervals_including_all_context_reject(self):
        for t in sorted(SLOW_TYPES|CONTEXT_TYPES):
            with self.subTest(t=t):
                self.assertFalse(analyze(complete([marker(),slow(),event(t,300,200)]))['producer_profile']['policy_valid'])

    def test_budget_zero_overflow_and_cap_marker_validation(self):
        context=event(93,200,300,obj=55,seq=9,flags=100)
        cap=event(89,400,obj=0,flags=1,seq=1)
        for metadata,es in ((marker(padding0=0),[context]),
                            (marker(padding0=1),[context,dict(context,padding0=10)]),
                            (marker(padding0=0),[cap]),
                            (marker(padding0=2),[context,cap]),
                            (marker(padding0=1),[context,cap,dict(cap)])):
            with self.subTest(metadata=metadata,events=es):
                self.assertFalse(analyze(complete([metadata,slow()]+es))['producer_profile']['policy_valid'])
        # Budget reservation and append publication can race; append order
        # alone cannot prove that a later record was admitted after exhaustion.
        for es in ([context,cap],[cap,context]):
            report=analyze(complete([marker(padding0=1),slow()]+es))
            self.assertTrue(report['producer_profile']['policy_valid'])
            self.assertFalse(report['stall_capture']['capture_usable'])
        report=analyze(complete([marker(padding0=0),slow(105)]))
        self.assertFalse(report['producer_profile']['policy_valid'])

    def test_retirement_payload_and_zero_frame(self):
        for t in (108,109,110):
            self.assertTrue(analyze(complete([marker(),slow(t, padding1=0)]))['stall_capture']['capture_usable'])
            for changes in (dict(object=0),dict(thread=0),dict(start=0),dict(padding0=0),dict(flags=1)):
                with self.subTest(t=t,changes=changes):
                    self.assertFalse(analyze(complete([marker(),slow(t,**changes)]))['producer_profile']['policy_valid'])
            wrong=complete([marker(),slow(t)])
            wrong[1]['clock_domain']='native_gpu_ns'
            with self.assertRaises(ValueError):analyze(wrong)

    def test_retirement_duplicates_and_conflicting_chunk_bindings(self):
        for es in ([slow(108),slow(108)],
                   [slow(108),slow(109,padding1=1)],
                   [slow(108),slow(110,thread=3)]):
            with self.subTest(events=es):
                report=analyze(complete([marker()]+es))
                self.assertFalse(report['producer_profile']['policy_valid'])
                self.assertFalse(report['stall_capture']['capture_usable'])
                self.assertEqual(len(report['stall_capture']['slow_scopes']),2)
        report=analyze(complete([marker()]+retirement_chain()))
        self.assertTrue(report['stall_capture']['capture_usable'])

    def test_retirement_phase_order_overlap_and_missing_filtered_phases(self):
        for es in (retirement_chain(108,110),retirement_chain(109,108),
                   [slow(110),slow(108,start=1_025_000_000,end=1_075_000_000)]):
            with self.subTest(events=es):
                report=analyze(complete([marker()]+es))
                self.assertFalse(report['producer_profile']['policy_valid'])
                self.assertFalse(report['stall_capture']['capture_usable'])
        for es in (retirement_chain(110,109),retirement_chain(108,109),list(reversed(retirement_chain()))):
            with self.subTest(events=es):
                self.assertTrue(analyze(complete([marker()]+es))['stall_capture']['capture_usable'])

    def test_compiler_missing_fast_stages_and_context_are_partial_unknown(self):
        report=analyze(complete([marker(),slow(107)]))
        compiler=report['compiler_stage_attribution']
        self.assertTrue(compiler['capture_usable'])
        self.assertEqual(compiler['coverage'],'partial_threshold_filtered_scopes')
        self.assertEqual(compiler['missing_stages'],'unknown_threshold_filtered_or_unobserved')
        self.assertEqual(compiler['stages'][0]['task_join'],'missing_incomplete_or_invalid_task')
        self.assertIsNone(compiler['critical_path_ms'])
        self.assertEqual(report['stall_capture']['context_coverage'],'partial_capped_observations')

    def test_loss_boundary_and_decoder_disagreement_fail_acceptance(self):
        for changes in (dict(dropped_before=1),dict(dropped_after=1),dict(uncommitted_reserved=1),
                        dict(counters_changed=True),dict(inconclusive_window=True),dict(structurally_complete=False),
                        dict(producer_quiescence_verified=False),dict(producer_profile_policy_valid=False)):
            data=complete([marker(),slow()]);data[-1].update(changes)
            with self.subTest(changes=changes):
                self.assertFalse(analyze(data)['producer_profile']['capture_usable'])
        data=complete([marker(),slow()]);data[-1]['wrong_generation']=9
        self.assertTrue(analyze(data)['stall_capture']['capture_usable'])

    def test_metadata_bounds_and_duplicate_independently_checked(self):
        for change in (dict(object=0),dict(flags=5),dict(end=101),dict(padding0=32769),dict(padding1=1),dict(thread=0),dict(start=0,end=0)):
            data=complete([dict(marker(),**change),slow()])
            with self.subTest(change=change):self.assertFalse(analyze(data)['producer_profile']['policy_valid'])
        self.assertFalse(analyze(complete([marker(),marker(),slow()]))['producer_profile']['policy_valid'])

    def test_compiler_payload_cannot_be_accepted_by_forged_decoder_summary(self):
        for changes in (dict(object=0),dict(thread=0),dict(flags=4),dict(padding0=0),dict(padding1=2)):
            with self.subTest(changes=changes):
                self.assertFalse(analyze(complete([marker(),slow(105,**changes)]))['stall_capture']['capture_usable'])

    def test_expected_profile_cli_writes_report_and_fails_closed(self):
        with tempfile.TemporaryDirectory(prefix='stall-cli-') as tmp:
            source=Path(tmp)/'capture.ndjson';output=Path(tmp)/'analysis.json'
            for es,code in (([marker(),slow()],0),([marker()],3),([profile_fixture.marker(),slow(3)],3),
                            ([marker(),slow(duration=THRESHOLD-1)],3)):
                source.write_text(''.join(json.dumps(r)+'\n' for r in complete(es)))
                result=subprocess.run([sys.executable,str(profile_fixture.HERE/'analyze.py'),str(source),'--output',str(output),'--expected-profile','stall-waits'],capture_output=True,text=True,timeout=15)
                self.assertEqual(result.returncode,code,result.stderr)
                self.assertTrue(output.is_file())


class StallBinaryTests(unittest.TestCase):
    setUpClass=classmethod(profile_fixture.TraceProfileTests.setUpClass.__func__)
    tearDownClass=classmethod(profile_fixture.TraceProfileTests.tearDownClass.__func__)
    fixture=profile_fixture.TraceProfileTests.fixture
    append=profile_fixture.TraceProfileTests.append
    decode=profile_fixture.TraceProfileTests.decode

    def test_binary_all_slow_types_and_inclusive_threshold(self):
        for t in sorted(SLOW_TYPES):
            with self.subTest(t=t):
                decoded,report=self.decode([marker(),slow(t)])
                self.assertEqual(decoded[0]['producer_profile'],'stall-waits')
                self.assertEqual(decoded[1]['clock_domain'],'cpu_steady_ns')
                self.assertEqual(decoded[1]['duration_ns'],THRESHOLD)
                self.assertTrue(report['stall_capture']['capture_usable'])
                decoded,report=self.decode([marker(),slow(t,THRESHOLD-1)],3)
                self.assertFalse(decoded[-1]['producer_profile_policy_valid'])
                self.assertFalse(report['producer_profile']['capture_usable'])

    def test_binary_excluded_and_reverse_context_and_bad_identity(self):
        excluded=(2,4,17,20,21,22,23,24,25,33,61,62,70,71,72,73,74,75,76,77,78,90,91,92,96,97,98,99,100,101,102,103,111)
        invalid=[slow(t) for t in excluded]+[event(t,300,200) for t in CONTEXT_TYPES]
        invalid += [slow(t,**change) for t in (108,109,110) for change in (dict(object=0),dict(thread=0),dict(start=0),dict(padding0=0),dict(flags=1))]
        for bad in invalid:
            with self.subTest(event=bad):
                decoded,report=self.decode([marker(),slow(),bad],3)
                self.assertFalse(decoded[-1]['producer_profile_policy_valid'])
                self.assertFalse(report['producer_profile']['capture_usable'])

    def test_binary_context_only_and_budget_bound(self):
        context=event(93,200,300,obj=55,seq=9,flags=100)
        for es in ([marker()],[marker(),context]):
            decoded,report=self.decode(es)
            self.assertTrue(decoded[-1]['producer_profile_policy_valid'])
            self.assertFalse(report['producer_profile']['capture_usable'])
        for es in ([marker(padding0=0),context,slow()],
                   [marker(padding0=1),context,dict(context,padding0=10),slow()],
                   [marker(padding0=0),slow(105)]):
            decoded,_=self.decode(es,3)
            self.assertFalse(decoded[-1]['producer_profile_policy_valid'])

    def test_binary_marker_identity_and_cap_payload(self):
        for changes in (dict(thread=0),dict(start=0,end=0)):
            self.decode([marker(**changes),slow()],3)
        context=event(93,200,300,obj=55,seq=9,flags=100)
        cap=event(89,400,obj=0,flags=1,seq=1)
        for changes in (dict(object=1),dict(thread=0),dict(start=0,end=0),dict(end=401),dict(padding0=2),dict(padding1=1)):
            with self.subTest(changes=changes):
                self.decode([marker(padding0=1),context,dict(cap,**changes),slow()],3)
                report=analyze(complete([marker(padding0=1),context,dict(cap,**changes),slow()]))
                self.assertFalse(report['producer_profile']['policy_valid'])

    def test_binary_retirement_duplicate_and_conflicting_bindings(self):
        for es in ([slow(108),slow(108)],
                   [slow(108),slow(109,padding1=1)],
                   [slow(108),slow(110,thread=3)]):
            with self.subTest(events=es):
                decoded,report=self.decode([marker()]+es,3)
                self.assertFalse(decoded[-1]['producer_profile_policy_valid'])
                self.assertFalse(report['stall_capture']['capture_usable'])
                self.assertEqual(len(report['stall_capture']['slow_scopes']),2)
        self.decode([marker()]+retirement_chain())

    def test_binary_retirement_phase_order_and_missing_phases(self):
        for es in (retirement_chain(108,110),retirement_chain(109,108),
                   [slow(110),slow(108,start=1_025_000_000,end=1_075_000_000)]):
            with self.subTest(events=es):self.decode([marker()]+es,3)
        for es in (retirement_chain(110,109),retirement_chain(108,109),list(reversed(retirement_chain()))):
            with self.subTest(events=es):self.decode([marker()]+es)


if __name__=='__main__':unittest.main()
