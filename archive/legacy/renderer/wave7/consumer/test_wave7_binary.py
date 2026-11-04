"""Actual v2 on-disk event records -> native C++ decoder -> Python analysis.

Constructed offline fixtures, not observed live presentation or producer races.
"""
import json
import math
import os
import struct
import unittest
from analyze import analyze
import tests_cli
EVENT=tests_cli.EVENT

def bits(value):return struct.unpack('<Q',struct.pack('<d',value))[0]
def dims(w,h):return w<<32|h
def event(t,detail0=0,detail1=0,flags=0,time=100,gen=1,obj=11,thread=2,end=None):
    return (time,time if end is None else end,obj,t,thread,flags,gen,detail0,detail1)
def feedback(frame=1,seconds=10.0,revision=4,width=1280,height=720,time=100):
    return [event(71,frame,bits(seconds),0x101,time),event(72,frame,revision,time=time),event(73,frame,dims(width,height),time=time)]

class Wave7Binary(tests_cli.CLITests):
    def decode_events(self,events,expected=0,holes=0,dropped=0,boundary=0):
        self.arm()
        with self.path.open('r+b') as handle:
            for index,row in enumerate(events):os.pwrite(handle.fileno(),EVENT.pack(*row,index+1),4096+index*64)
        self.patch(24,len(events)+holes)
        self.patch(40,dropped,boundary)
        self.stop()
        result=self.call('decode',self.path,1,self.identity)
        self.assertEqual(result.returncode,expected,result.stderr)
        rows=self.rows(result)
        return rows,analyze(rows)

    def test_host_bits_geometry_revision_and_display_cadence(self):
        es=feedback(1,100000.0)+feedback(2,100000.016,7,1920,1080,time=110)
        rows,a=self.decode_events(es)
        decoded=rows[0];f=a['wave7']['native_presented_frames'];c=a['wave7']['native_presented_cadence'][0]
        self.assertEqual(decoded['clock_domain'],'cpu_steady_ns')
        self.assertEqual(decoded['native_clock_domain_name'],'ca_host_seconds')
        self.assertEqual(decoded['native_time_bits'],bits(100000.0));self.assertIsNone(decoded['duration_ns'])
        self.assertTrue(decoded['native_presented_time_valid']);self.assertEqual(f[1]['configuration_revision'],7)
        self.assertEqual(f[1]['actual_drawable_dimensions'],{'width':1920,'height':1080})
        self.assertAlmostEqual(c['intervals']['median_ms'],16,places=5)
        self.assertAlmostEqual(c['observed_interval_rate_hz'],62.5,places=4)
        self.assertIsNone(c['display_latency_ms']);self.assertFalse(a['cpu_present_streams'])

    def test_invalid_double_records_retained_without_presented_proof(self):
        es=[]
        for i,value in enumerate([-1.0,0.0,float('nan'),float('inf'),-float('inf')]):
            validbit=0x100 if math.isfinite(value) and value>0 else 0
            es.append(event(71,i+1,bits(value),1|validbit,time=100+i))
        rows,a=self.decode_events(es)
        self.assertTrue(all(not r['native_presented_time_valid'] for r in rows[:-1]))
        self.assertIsNone(rows[2]['native_presented_seconds'])
        self.assertFalse(a['wave7']['native_presented_cadence'])
        self.assertEqual(a['rejections']['invalid_native_presented_timestamp_or_domain_flags'],5)

    def test_flag_domain_and_validity_disagreement(self):
        es=[event(71,i+1,bits(10.0),flags) for i,flags in enumerate([1,0x102,0x301])]
        rows,a=self.decode_events(es)
        self.assertFalse(a['wave7']['native_presented_cadence'])
        self.assertTrue(all(not r['native_presented_time_valid'] for r in rows[:-1]))

    def test_callback_reordering_keeps_frame_order(self):
        _,a=self.decode_events(feedback(2,10.02,time=100)+feedback(1,10.0,time=200))
        c=a['wave7']['native_presented_cadence'][0]
        self.assertEqual(c['stream_state'],'observed');self.assertAlmostEqual(c['intervals']['median_ms'],20)

    def test_time_reversal_and_equal_times_reject_stream(self):
        # Both independently bad streams are rejected, never timestamp-sorted into cadence.
        es=feedback(1,10.0)+feedback(2,9.0,time=200)
        es += [tuple(list(r[:2])+[12]+list(r[3:])) for r in feedback(1,20.0,time=300)+feedback(2,20.0,time=400)]
        _,a=self.decode_events(es)
        self.assertTrue(all(c['intervals'] is None for c in a['wave7']['native_presented_cadence']))
        self.assertEqual(a['rejections']['nonmonotonic_native_presentation_stream'],2)

    def test_duplicates_partial_and_identity_gaps(self):
        es=feedback(1)+feedback(2,10.01,time=200)+[feedback(2,10.01,time=200)[0]]+feedback(3,10.02,time=300)[:1]
        _,a=self.decode_events(es);fs=a['wave7']['native_presented_frames']
        self.assertEqual(fs[1]['join_state'],'duplicate_identity')
        self.assertEqual(fs[2]['join_state'],'incomplete_capture_or_boundary')
        self.assertIsNone(fs[2]['configuration_revision'])
        c=a['wave7']['native_presented_cadence'][0];self.assertEqual(c['missing_identity_gaps'],1);self.assertIsNone(c['intervals'])

    def test_receipt_disagreement_prevents_join(self):
        es=feedback();es[2]=event(73,1,dims(1280,720),time=200)
        _,a=self.decode_events(es)
        self.assertEqual(a['wave7']['native_presented_frames'][0]['join_state'],'receipt_disagreement')
        self.assertFalse(a['wave7']['native_presented_cadence'])

    def test_stale_generation_filtered_and_capture_loss_separate(self):
        es=feedback()+[event(71,2,bits(10.1),0x101,gen=2)]
        _,a=self.decode_events(es,expected=3,holes=1,dropped=2,boundary=3)
        loss=a['wave7']['pe_capture_loss_evidence']
        self.assertEqual(loss['wrong_generation'],1);self.assertEqual(loss['uncommitted_reserved'],1)
        self.assertEqual(loss['dropped_after'],2);self.assertEqual(loss['boundary_rejected_total'],3)
        self.assertEqual(len(a['wave7']['native_presented_frames']),1)

    def test_pending_tickets_native_overflow_rejection_and_close(self):
        es=[]
        for time,values in [(100,[8,8,0,1,2,8,0]),(200,[10,10,1,2,4,9,0])]:
            es += [event(74,99,value,i,time) for i,value in enumerate(values)]
        es += [event(75,9,1,7,time=300),event(76,11,4,4,time=310)]
        _,a=self.decode_events(es)
        w=a['wave7'];last=w['native_counter_samples'][-1]
        self.assertEqual(last['values']['pending_live_handler_tickets'],9)
        self.assertEqual(last['cumulative_deltas']['overflow'],1);self.assertNotIn('pending_live_handler_tickets',last['cumulative_deltas'])
        self.assertEqual(w['native_session_closes'][0]['live_handler_tickets_at_close'],9)
        self.assertFalse(w['native_presented_frames']);self.assertEqual(w['pe_capture_loss_evidence']['dropped_after'],0)

    def test_duplicate_counter_and_regression_no_bad_delta(self):
        es=[event(74,99,i,i,time=100) for i in range(7)]
        es += [event(74,99,0,i,time=200) for i in range(7)]
        es += [event(74,99,0,0,time=300),event(74,99,0,0,time=300)]
        _,a=self.decode_events(es)
        self.assertIsNone(a['wave7']['native_counter_samples'][1]['cumulative_deltas'])
        self.assertEqual(a['rejections']['native_cumulative_counter_regression'],1)
        self.assertFalse(a['wave7']['native_counter_samples'][2]['complete'])

    def test_resize_chronology_samples_and_checked_query(self):
        es=[event(61,dims(800,600),dims(800,600),44,time=110),event(62,dims(5,2),dims(900,700),44,time=120),event(61,dims(1280,720),dims(1280,720),44|0x10000,time=180),event(62,dims(5,2),0,44|0x30000,time=190),event(44,dims(1280,720),dims(2,0),0,time=100,end=200)]
        _,a=self.decode_events(es);w=a['wave7'];api=w['resize_api_chronology'][0]
        self.assertEqual(api['snapshot_completeness'],'four_unique_samples');self.assertEqual(api['requested_dimensions'],{'width':1280,'height':720})
        self.assertTrue(all(s['api_event_index']==4 for s in w['resize_snapshots']))
        self.assertFalse(w['resize_snapshots'][-1]['client_query_valid'])
        self.assertEqual(w['resize_snapshots'][0]['committed_descriptor'],{'width':800,'height':600})

    def test_nested_resize_and_snapshot_duplicates_are_ambiguous(self):
        es=[event(61,dims(800,600),0,45,time=150),event(45,0,0,time=100,end=200),event(45,0,0,time=140,end=160)]
        _,a=self.decode_events(es);s=a['wave7']['resize_snapshots'][0]
        self.assertIsNone(s['api_event_index']);self.assertEqual(s['api_join_state'],'missing_or_ambiguous_api')

    def test_unknown_events_old_v2_and_noninstant_feedback(self):
        _,a=self.decode_events([event(99),event(71,1,bits(10.0),0x101,end=101)])
        self.assertFalse(a['wave7']['native_presented_frames']);self.assertEqual(a['rejections']['invalid_wave7_identity_or_noninstant'],1)

    def test_checked_apply_and_close_discard_domains(self):
        rows,a=self.decode_events([event(77,40,0,4),event(78,99,3,0,time=110),event(78,100,0,2,time=120)])
        self.assertTrue(all(e['clock_domain']=='cpu_steady_ns' and e['duration_ns'] is None for e in rows[:-1]))
        self.assertEqual(a['wave7']['native_configuration_apply_failures'][0]['status_name'],'FAILED')
        self.assertEqual(a['wave7']['native_configuration_apply_failures'][0]['configuration_revision'],40)
        counted,unknown=a['wave7']['native_close_queued_discard_accounting']
        self.assertEqual(counted['queued_discarded'],3);self.assertTrue(counted['accounting_verified'])
        self.assertIsNone(unknown['queued_discarded']);self.assertFalse(unknown['accounting_verified'])

    def test_bad_close_accounting_identity_rejected(self):
        _,a=self.decode_events([event(78,0,10,0)])
        self.assertFalse(a['wave7']['native_close_queued_discard_accounting'])
        self.assertEqual(a['rejections']['invalid_feedback_close_accounting_session'],1)

def load_tests(loader,tests,pattern):
    # Legacy CLI tests run once in tests_cli; inherit helpers, not duplicate checks.
    return unittest.TestSuite(Wave7Binary(name) for name in sorted(Wave7Binary.__dict__) if name.startswith('test_'))

if __name__=='__main__':unittest.main()
