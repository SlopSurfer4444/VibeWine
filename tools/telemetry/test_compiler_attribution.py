"""Compiler scope identities, malformed joins, and actual binary clocks."""
import copy
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import tests_cli
from analyze import analyze
from test_analyze import event, rows
from test_detail_analyze import task
import test_trace_profile as profile_fixture
marker=profile_fixture.marker

def stage(t=105,start=3_100_000,end=3_200_000,op=601,route=0,flags=1,**changes):
    e=event(t,start,end,obj=501,seq=op,frame=route,flags=flags);e.update(changes);return e

def reader(parent=501):
    return [event(104,3_000_000,3_050_000,obj=300,seq=parent,frame=600,flags=1),event(93,3_060_000,3_090_000,obj=300,seq=600,flags=100)]

def attribution(es):return analyze(rows(es))['compiler_stage_attribution']

class CompilerAttributionTests(unittest.TestCase):
    def test_cold_scopes_exact_reader_task_and_no_summed_cost(self):
        r=attribution(task()+reader()+[stage(),stage(106,3_300_000,3_400_000,602),stage(107,3_500_000,3_600_000,603)])
        self.assertTrue(r['capture_usable']);w=r['reader_waits'][0]
        self.assertEqual(w['checked_get_status']['operation_result'],'hit');self.assertEqual(w['observed_cpu_wall_wait_ms'],.05)
        self.assertEqual(w['checked_get_join'],'exact_cache_and_read_operation')
        for s in r['stages']:
            self.assertEqual(s['task_join'],'joined_observed_task_run');self.assertEqual(s['observed_cpu_wall_duration_ms'],.1)
        for field in ('summed_cpu_cost_ms','backend_queue_time_ms','critical_path_ms'):self.assertIsNone(r[field])

    def test_cache_failure_fallback_and_cold_native_failure_semantics(self):
        for es in ([stage(106,3_100_000,3_200_000,601,1,2),stage(105,3_300_000,3_400_000,602),stage(106,3_500_000,3_600_000,603)],
                   [stage(106,3_100_000,3_200_000,601,1),stage(107,3_300_000,3_400_000,602,1,2),stage(105,3_500_000,3_600_000,603)],
                   [stage(106,flags=2),stage(107,3_300_000,3_400_000,602)]):
            self.assertTrue(attribution(task()+es)['capture_usable'])

    def test_cached_result_path_contradictions_rejected(self):
        contradictory = (
            [stage(106,route=1,flags=2),stage(107,3_300_000,3_400_000,602,1)],
            [stage(107,route=1),stage(105,3_300_000,3_400_000,602)],
        )
        for scopes in contradictory:
            with self.subTest(scopes=scopes):
                r=attribution(task()+scopes)
                self.assertFalse(r['capture_usable'])
                self.assertTrue(all(not s['valid'] for s in r['stages']))
                self.assertTrue(all(s['observed_cpu_wall_duration_ms'] is None for s in r['stages']))

    def test_cached_success_missing_function_outcome_remains_unknown(self):
        r=attribution(task()+[stage(106,route=1),stage(105,3_300_000,3_400_000,602)])
        self.assertTrue(r['capture_usable'])
        self.assertEqual([s['route'] for s in r['stages']],['cache','cold'])
        self.assertEqual(r['missing_stages'],'unknown')

    def test_checked_miss_cannot_enter_cached_shader_path(self):
        for code in (101,0,5,6,11,26,356,0xfffffffe):
            for t in (106,107):
                es=reader();es[1]['flags']=code
                r=attribution(task()+es+[stage(t,route=1)])
                self.assertFalse(r['capture_usable']);self.assertFalse(r['stages'][0]['valid'])
        es=reader();es[1]['flags']=101
        self.assertTrue(attribution(task()+es+[stage()])['capture_usable'])
        self.assertTrue(attribution(task()+reader()+[stage(106,route=1)])['capture_usable'])
        # Missing parent/read/status evidence cannot establish a cache miss
        # on this particular work item.
        controls=[reader(0),reader()[:1],reader()[1:]]
        unknown_status=reader();unknown_status[1]['flags']=0xffffffff;controls.append(unknown_status)
        controls[0][1]['flags']=101;controls[2][0]['flags']=101
        for es in controls:self.assertTrue(attribution(task()+es+[stage(106,route=1)])['capture_usable'])

    def test_reader_lock_exception_prevents_following_shader_stages(self):
        for s in (stage(),stage(106),stage(106,route=1),stage(107)):
            es=reader()[:1];es[0]['flags']=3
            r=attribution(es+[s]);self.assertFalse(r['capture_usable']);self.assertFalse(r['stages'][0]['valid'])
        es=reader()[:1];es[0]['flags']=3
        self.assertTrue(attribution(es)['capture_usable'])
        self.assertTrue(attribution(reader()[:1]+[stage(106,route=1)])['capture_usable'])

    def test_exception_and_observed_task_readiness_contradict(self):
        scopes=[stage(t,flags=3) for t in (105,106,107)]
        lock=reader()[:1][0];lock['flags']=3;scopes.append(lock)
        for scope in scopes:
            with self.subTest(scope=scope):
                r=attribution(task()+[scope]);self.assertFalse(r['capture_usable'])
                row=(r['reader_waits'] or r['stages'])[0]
                self.assertEqual(row['task_join'],'contradictory_task_completion')
                self.assertFalse(row['valid'])
                for prefix in ([],task()[:2]):
                    r=attribution(prefix+[scope]);self.assertTrue(r['capture_usable'])
                    row=(r['reader_waits'] or r['stages'])[0]
                    self.assertEqual(row['task_join'],'missing_incomplete_or_invalid_task')
                    self.assertTrue(row['valid'])

    def test_absence_missing_task_and_missing_get_stay_unknown(self):
        r=attribution(task());self.assertEqual(r['coverage'],'unknown');self.assertFalse(r['capture_usable']);self.assertEqual(r['stages'],[])
        for es in ([stage()],task()[1:]+[stage()]):
            r=attribution(es);self.assertEqual(r['stages'][0]['task_join'],'missing_incomplete_or_invalid_task')
        for parent in (0,501):
            w=attribution(task()+reader(parent)[:1])['reader_waits'][0]
            self.assertTrue(w['valid']);self.assertEqual(w['checked_get_join'],'missing_checked_get');self.assertIsNone(w['checked_get_status'])
        es=reader(0)[:1];es[0]['flags']=3;self.assertEqual(attribution(es)['reader_waits'][0]['result'],'exception')

    def test_nonzero_payload_flags_route_and_interval_validation(self):
        for change in ({'object':0},{'padding0':0},{'thread':0},{'start':0},{'end':2_000_000},{'flags':0},{'flags':4},{'padding1':2},{'padding1':1}):
            with self.subTest(change=change):
                r=attribution(task()+[stage(**change)]);self.assertFalse(r['capture_usable']);self.assertIsNone(r['stages'][0]['observed_cpu_wall_duration_ms'])
        for change in ({'object':0},{'padding1':0},{'thread':0},{'flags':2},{'end':2_000_000}):
            es=reader()[:1];es[0].update(change);r=attribution(task()+es)
            self.assertFalse(r['capture_usable']);self.assertIsNone(r['reader_waits'][0]['observed_cpu_wall_wait_ms'])

    def test_duplicate_serial_across_tasks_types_and_reader_wait(self):
        for duplicate in (stage(),stage(106,3_300_000,3_400_000,601),stage(106,3_300_000,3_400_000,601,object=502)):
            r=attribution(task()+[stage(),duplicate]);self.assertFalse(r['capture_usable']);self.assertTrue(all(not s['valid'] for s in r['stages']))
        r=attribution(task()+reader()+[stage(op=600)]);self.assertFalse(r['capture_usable']);self.assertFalse(r['reader_waits'][0]['valid'])

    def test_task_thread_and_chronology_contradictions(self):
        for change in ({'thread':3},{'start':2_000_000},{'end':7_000_000}):
            r=attribution(task()+[stage(**change)]);self.assertFalse(r['capture_usable']);self.assertIsNone(r['stages'][0]['observed_cpu_wall_duration_ms'])
        es=task()+[stage()];es[2]['thread']=3;self.assertFalse(attribution(es)['capture_usable'])
        es=task();es[1]['padding0']=101;r=attribution(es+[stage()]);self.assertFalse(r['capture_usable']);self.assertEqual(r['stages'][0]['task_join'],'missing_incomplete_or_invalid_task')

    def test_reader_thread_chronology_and_ambiguous_operation(self):
        for change in ({'thread':3},{'start':3_040_000},{'padding0':0},{'padding1':1}):
            es=reader();es[1].update(change);r=attribution(task()+es);self.assertFalse(r['capture_usable']);self.assertIsNone(r['reader_waits'][0]['checked_get_status'])
        es=reader();es.append(copy.deepcopy(es[-1]));self.assertFalse(attribution(task()+es)['capture_usable'])
        es=reader();es[0]['flags']=3;self.assertFalse(attribution(task()+es)['capture_usable'])
        es=reader();es[1]['object']=301;r=attribution(task()+es);self.assertTrue(r['capture_usable']);self.assertEqual(r['reader_waits'][0]['checked_get_join'],'missing_checked_get')
        es=reader();es[1]['end']=7_000_000;self.assertFalse(attribution(task()+es)['capture_usable'])

    def test_stage_cannot_precede_read_end_or_reuse_its_serial(self):
        for s in (stage(start=3_080_000),stage(op=600)):
            r=attribution(task()+reader()+[s]);self.assertFalse(r['capture_usable']);self.assertFalse(r['stages'][0]['valid'])
        r=attribution(task()+reader()[1:]+[stage(op=600)])
        self.assertFalse(r['capture_usable']);self.assertFalse(r['stages'][0]['valid'])

    def test_overlap_route_reversal_and_invalid_stage_order(self):
        pairs=((stage(),stage(106,3_150_000,3_300_000,602)),(stage(),stage(106,3_300_000,3_400_000,602,1)),
               (stage(flags=2),stage(106,3_300_000,3_400_000,602)),(stage(106),stage(105,3_300_000,3_400_000,602)),
               (stage(106,route=1,flags=3),stage(105,3_300_000,3_400_000,602)))
        for a,b in pairs:self.assertFalse(attribution(task()+[a,b])['capture_usable'])

    def test_clock_profile_and_loss_independently_checked(self):
        for domain in ('unknown','native_gpu_ns','ca_host_seconds'):
            data=rows([stage()]);data[0]['clock_domain']=domain
            with self.assertRaises(ValueError):analyze(data)
        for key in ('dropped_after','uncommitted_reserved'):
            data=rows([stage()]);data[-1][key]=1;self.assertFalse(analyze(data)['compiler_stage_attribution']['capture_usable'])
        data=rows([stage()]);data[-1]['inconclusive_window']=True;self.assertFalse(analyze(data)['compiler_stage_attribution']['capture_usable'])
        data=rows([stage()]);data[-1]['producer_profile_policy_valid']=False;self.assertFalse(analyze(data)['compiler_stage_attribution']['capture_usable'])
        self.assertTrue(attribution([marker(),stage()])['capture_usable']);self.assertFalse(attribution([marker(flags=3),stage()])['capture_usable'])

class CompilerBinaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.previous_controller=tests_cli.CONTROLLER
        cls.scratch=tempfile.TemporaryDirectory(prefix='compiler-scope-decoder-')
        scratch=Path(cls.scratch.name)
        here=Path(__file__).resolve().parent
        # The optional override exercises exact proposed submodule header bytes
        # without mutating the canonical submodule or its build/index.
        protocol=Path(os.environ.get('DXMT_TRACE_TEST_PROTOCOL',str(here.parents[1]/'components/dxmt/src/d3d11/cs2_trace_protocol.hpp'))).resolve(strict=True)
        for name in ('trace_controller.cpp','trace_core.hpp','probe_semantics.hpp'):
            source=(here/name).read_text()
            if name=='trace_core.hpp':source=source.replace('../../components/dxmt/src/d3d11/cs2_trace_protocol.hpp',str(protocol))
            (scratch/name).write_text(source)
        try:
            subprocess.run(['xcrun','clang++','-std=c++20','-O1','-Wall','-Wextra','-Werror',str(scratch/'trace_controller.cpp'),'-framework','QuartzCore','-o',str(scratch/'cs2trace')],check=True,capture_output=True,text=True,timeout=60)
        except Exception:
            cls.scratch.cleanup();raise
        tests_cli.CONTROLLER=scratch/'cs2trace'

    tearDownClass=classmethod(profile_fixture.TraceProfileTests.tearDownClass.__func__)
    fixture=profile_fixture.TraceProfileTests.fixture
    append=profile_fixture.TraceProfileTests.append
    decode=profile_fixture.TraceProfileTests.decode

    def test_full_compile_profile_binary_clocks_and_no_fps(self):
        for metadata in ([],[marker(padding0=16)]):
            decoded,r=self.decode(metadata+task()+reader()+[stage(),stage(106,3_300_000,3_400_000,602),stage(107,3_500_000,3_600_000,603)])
            scopes=[e for e in decoded[:-1] if 104<=e['type']<=107]
            self.assertEqual([e['clock_domain'] for e in scopes],['cpu_steady_ns']*4)
            self.assertEqual([e['duration_ns'] for e in scopes],[50_000,100_000,100_000,100_000]);self.assertTrue(all(e['compiler_identity_valid'] for e in scopes))
            self.assertTrue(r['compiler_stage_attribution']['capture_usable']);self.assertEqual(r['cpu_present_streams'],[])
            self.assertIsNone(r['queue_chunk_metrics']['gpu_command_buffer_durations'])

    def test_binary_noncompiler_reader_and_lock_exception_without_get(self):
        for flags in (1,3):
            e=reader(0)[0];e['flags']=flags
            decoded,r=self.decode([e]);self.assertEqual(decoded[0]['duration_ns'],50_000)
            w=r['compiler_stage_attribution']['reader_waits'][0]
            self.assertEqual(w['task_join'],'noncompiler_or_unavailable_parent')
            self.assertEqual(w['checked_get_join'],'missing_checked_get');self.assertTrue(w['valid'])

    def test_binary_negative_interval_payload_profile_and_clock(self):
        for change in ({'end':1},{'thread':0},{'object':0},{'padding0':0},{'padding1':1},{'flags':4}):
            decoded,r=self.decode([stage(**change)],3)
            self.assertIsNone(decoded[0]['duration_ns']);self.assertFalse(decoded[0]['compiler_identity_valid']);self.assertFalse(r['compiler_stage_attribution']['capture_usable'])
        for change in ({'flags':2},{'padding1':0},{'object':0},{'thread':0},{'end':1}):
            e=reader(0)[0];e.update(change);decoded,r=self.decode([e],3)
            self.assertIsNone(decoded[0]['duration_ns']);self.assertFalse(r['compiler_stage_attribution']['capture_usable'])
        decoded,r=self.decode([stage(),stage()]);self.assertFalse(r['compiler_stage_attribution']['capture_usable'])
        decoded,r=self.decode([marker(flags=3),stage()],3);self.assertFalse(decoded[-1]['producer_profile_policy_valid']);self.assertFalse(r['compiler_stage_attribution']['capture_usable'])
        decoded,_=self.decode([stage()]);decoded[0]['clock_domain']='native_gpu_ns'
        with self.assertRaises(ValueError):analyze(decoded)

if __name__=='__main__':unittest.main()
