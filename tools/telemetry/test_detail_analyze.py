import copy,unittest
from analyze import analyze
from test_analyze import event,rows,chunk

def task(serial=501,base=1_000_000):
    return [event(80,base,obj=serial,seq=100,frame=2,flags=2),event(81,base+2_000_000,obj=serial,seq=100,frame=1,flags=2),event(84,base+5_000_000,obj=serial,seq=100)]
def persistence():
    return [event(85,7_000_000,obj=501,seq=100,frame=4096),event(86,8_000_000,15_000_000,obj=501,seq=100,frame=4096),event(87,9_000_000,10_000_000,obj=501,seq=100),event(88,10_000_000,14_000_000,obj=501,seq=100,flags=101)]
def finish():
    return [event(90,1_007_000_000,1_008_000_000,obj=99,seq=1,frame=3),event(91,1_009_000_000,obj=99,seq=1,frame=3),event(92,1_011_000_000,obj=99,seq=1,frame=3)]
def detail(es):return analyze(rows(es))['detailed_probes']

class DetailAnalyzerTests(unittest.TestCase):
    def test_actual_payload_shape_and_separate_costs(self):
        d=detail(task()+persistence());t=d['tasks'][0]
        self.assertTrue(d['capture_usable']);self.assertEqual(t['join_state'],'complete_observed_task');self.assertEqual(t['enqueue_to_first_start_ms'],2)
        self.assertEqual(t['run_attempts'][0]['duration_ms'],3)
        self.assertEqual(t['persistence']['queue_wait_ms'],1);self.assertEqual(t['persistence']['callable_duration_ms'],7)
        self.assertEqual(t['persistence']['cache_lock_wait_ms'],1);self.assertEqual(t['persistence']['cache_call_ms'],4)
        self.assertEqual(t['persistence']['cache_write_status']['operation_result'],'write_completed');self.assertEqual(t['persistence']['callable_result'],'returned_result_unknown')
        self.assertEqual(d['runnable_samples'][0]['runnable_depth'],2)
    def test_dependency_wait_and_second_run(self):
        es=task()[:2]+[event(82,4_000_000,obj=501,seq=502,frame=100),event(83,8_000_000,obj=501,seq=502,frame=100),event(81,9_000_000,obj=501,seq=100,flags=3),event(84,12_000_000,obj=501,seq=100)]+task(502,2_000_000)
        d=detail(es);t=d['tasks'][0];self.assertTrue(d['capture_usable']);self.assertEqual(len(t['run_attempts']),2)
        self.assertEqual(t['dependency_waits'][0]['wait_ms'],4);self.assertEqual(t['dependency_waits'][0]['dependency_join'],'observed_ready_task')
    def test_completion_readback_coherence_cpu_intervals(self):
        d=detail(chunk()+finish());r=d['readbacks'][0]
        self.assertEqual(r['completion_to_readback_ms'],1);self.assertEqual(r['readback_publication_ms'],1)
        self.assertEqual(r['completion_to_coherence_observed_ms'],5);self.assertEqual(r['coherence_signal_scope_ms'],2)
    def test_negative_intervals_do_not_become_measurements(self):
        for t in (86,87,88):
            es=task()+persistence();e=next(e for e in es if e['type']==t);e['end']=e['start']-1
            d=detail(es);self.assertFalse(d['capture_usable']);self.assertIsNone(d['tasks'][0]['persistence'])
        es=chunk()+finish();es[-3]['end']=es[-3]['start']-1;d=detail(es);self.assertFalse(d['readbacks']);self.assertFalse(d['capture_usable'])
    def test_duplicate_and_malformed_duplicate_poison_task(self):
        for t in (80,84,85,86,87,88):
            es=task()+persistence();e=copy.deepcopy(next(e for e in es if e['type']==t));e['flags']=999;es.append(e)
            d=detail(es);self.assertFalse(d['capture_usable']);self.assertEqual(d['tasks'][0]['join_state'],'invalid')
    def test_invalid_task_and_dependency_identities(self):
        for field in ('object','padding0'):
            es=task();es[0][field]=0;self.assertFalse(detail(es)['capture_usable'])
        es=task()[:2]+[event(82,4_000_000,obj=501,seq=0,frame=100)];self.assertFalse(detail(es)['capture_usable'])
    def test_task_owner_disagreement(self):
        es=task();es[1]['padding0']=200;self.assertFalse(detail(es)['capture_usable'])
    def test_bad_flags_and_noninstant_markers(self):
        for t in (80,81,82,83,84,85,87):
            es=task()+persistence();e=next((e for e in es if e['type']==t),None)
            if e is None:es.append(event(t,4_000_000,obj=501,seq=502,frame=100));e=es[-1]
            e['flags']=0 if t in (80,81) else 99;self.assertFalse(detail(es)['capture_usable'])
        es=task();es[0]['end']+=1;self.assertFalse(detail(es)['capture_usable'])
    def test_persistence_before_ready_or_rejected_but_executed(self):
        es=task()+persistence();es[3]['start']=es[3]['end']=2_000_000;self.assertFalse(detail(es)['capture_usable'])
        es=task()+persistence();es[3]['flags']=1;self.assertFalse(detail(es)['capture_usable'])
        es=task()+persistence();es[4]['padding1']=3;self.assertFalse(detail(es)['capture_usable'])
    def test_rejected_admission_no_run_does_not_invent_cost(self):
        es=task()+persistence()[:1];es[-1]['flags']=1;p=detail(es)['tasks'][0]['persistence'];self.assertEqual(p['admission'],'rejected');self.assertIsNone(p['callable_duration_ms'])
    def test_missing_tasks_or_dependencies_remain_unknown(self):
        d=detail(task()[1:]);self.assertEqual(d['tasks'][0]['join_state'],'incomplete_capture_or_boundary');self.assertIsNone(d['tasks'][0]['enqueue_to_first_start_ms'])
        es=task()[:2]+[event(82,4_000_000,obj=501,seq=502,frame=100),event(83,8_000_000,obj=501,seq=502,frame=100),event(81,9_000_000,obj=501,seq=100,flags=2),event(84,12_000_000,obj=501,seq=100)]
        self.assertEqual(detail(es)['tasks'][0]['dependency_waits'][0]['dependency_join'],'missing_incomplete_or_invalid_dependency')
    def test_ambiguous_dependency_release(self):
        es=task()[:2]+[event(82,4_000_000,obj=501,seq=502,frame=100),event(83,8_000_000,obj=501,seq=503,frame=100)];self.assertFalse(detail(es)['capture_usable'])
    def test_missing_duplicate_wrong_frame_finish_rejected(self):
        for mutate in ('missing','duplicate','frame','queue','backwards'):
            es=chunk()+finish()
            if mutate=='missing':es.pop()
            elif mutate=='duplicate':es.append(copy.deepcopy(es[-1]))
            elif mutate=='frame':es[-1]['padding1']=4
            elif mutate=='queue':es[-1]['object']=100
            else:es[-1]['start']=es[-1]['end']=1_000_000_000
            d=detail(es);self.assertFalse(d['readbacks']);self.assertFalse(d['capture_usable'])
    def test_cap_stale_generation_and_summary_loss_fail_capture(self):
        for flag in (1,2,999):
            d=detail(task()+[event(89,7_000_000,obj=0,seq=3,flags=flag)]);self.assertFalse(d['capture_usable']);self.assertEqual(len(d['source_failures']),1)
        for key in ('dropped_before','dropped_after','uncommitted_reserved'):
            r=rows(task());r[-1][key]=1;self.assertFalse(analyze(r)['detailed_probes']['capture_usable'])
            r[-1][key]=True
            with self.assertRaises(ValueError):analyze(r)
        r=rows(task());r[-1]['wrong_generation']=3;d=analyze(r)['detailed_probes'];self.assertTrue(d['capture_usable']);self.assertEqual(d['historical_generation_records_excluded'],3)
        r[-1]['wrong_generation']=True
        with self.assertRaises(ValueError):analyze(r)
    def test_wire_types_generation_and_clock_domains(self):
        for field in ('object','start','padding0','generation','flags'):
            for bad in (True,1.5,-1):
                r=rows(task());r[0][field]=bad
                with self.assertRaises(ValueError):analyze(r)
        r=rows(task());r[0]['generation']=8
        with self.assertRaises(ValueError):analyze(r)
        for domain in ('native_gpu_ns','ca_host_seconds','unknown'):
            r=rows(task());r[0]['clock_domain']=domain
            with self.assertRaises(ValueError):analyze(r)

    def test_checked_sqlite_statuses_and_ambiguous_operations(self):
        for t in (93,94):
            for code,name in ((0,'SQLITE_OK'),(100,'SQLITE_ROW'),(101,'SQLITE_DONE'),(5,'SQLITE_BUSY'),(6,'SQLITE_LOCKED'),(11,'SQLITE_CORRUPT'),(26,'SQLITE_NOTADB'),(261,'SQLITE_BUSY'),(0xffffffff,'native_result_unavailable'),(0xfffffffe,'cpp_exception')):
                d=detail([event(t,1_000_000,3_000_000,obj=300,seq=301,flags=code)])
                row=d['checked_cache_operations'][0];self.assertEqual(row['status']['code'],code);self.assertEqual(row['status']['classification'],name);self.assertEqual(row['duration_ms'],2)
            es=[event(t,1,2,obj=300,seq=301),event(t,3,4,obj=300,seq=301,flags=999)]
            self.assertFalse(detail(es)['capture_usable']);self.assertFalse(detail(es)['checked_cache_operations'])
            for key in ('object','padding0'):
                es=[event(t,1,2,obj=300,seq=301)];es[0][key]=0;self.assertFalse(detail(es)['capture_usable'])
        for code,result in ((100,'hit'),(101,'miss'),(5,'failed')):
            self.assertEqual(detail([event(93,1,2,obj=300,seq=301,flags=code)])['checked_cache_operations'][0]['status']['operation_result'],result)
        for code,result in ((101,'write_completed'),(5,'failed'),(0xffffffff,'unknown')):
            es=task()+persistence();es[-1]['flags']=code
            self.assertEqual(detail(es)['tasks'][0]['persistence']['cache_write_status']['operation_result'],result)
        for code in (256,356,357):
            for t in (93,94):self.assertEqual(detail([event(t,1,2,obj=300,seq=301,flags=code)])['checked_cache_operations'][0]['status']['operation_result'],'failed')
            es=task()+persistence();es[-1]['flags']=code;self.assertEqual(detail(es)['tasks'][0]['persistence']['cache_write_status']['operation_result'],'failed')

    def test_missing_start_orphan_release_and_writer_order_rejected(self):
        self.assertFalse(detail([task()[0],task()[2]])['capture_usable'])
        self.assertFalse(detail(task()+[event(83,7_000_000,obj=501,seq=502,frame=100)])['capture_usable'])
        es=task()+persistence();es[-2]['start']=12_000_000;es[-2]['end']=13_000_000
        self.assertFalse(detail(es)['capture_usable'])
        es=task()+persistence();es[-1]['flags']=0xfffffffe;self.assertFalse(detail(es)['capture_usable'])
        es=task()[:2]+[event(82,4_000_000,obj=501,seq=502,frame=100),event(83,8_000_000,obj=501,seq=502,frame=100),event(81,9_000_000,obj=501,seq=100,flags=2),event(84,12_000_000,obj=501,seq=100)]+task(502,20_000_000)
        d=detail(es);self.assertFalse(d['capture_usable']);self.assertEqual(d['tasks'][0]['dependency_waits'][0]['dependency_join'],'contradictory_ready_chronology');self.assertIsNone(d['tasks'][0]['dependency_waits'][0]['wait_ms'])

if __name__=='__main__':unittest.main()
