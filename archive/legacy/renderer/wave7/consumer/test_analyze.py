import copy,json,unittest
from analyze import analyze

def event(t,start,end=None,obj=11,thread=2,seq=0,frame=0,flags=0):
 return dict(kind='event',index=0,generation=7,type=t,start=start,end=start if end is None else end,object=obj,thread=thread,flags=flags,padding0=seq,padding1=frame,clock_domain='native_gpu_ns'if t==24 else 'cpu_steady_ns')
def rows(events):
 es=copy.deepcopy(events)
 for i,e in enumerate(es):e['index']=i
 return es+[dict(kind='summary',generation=7,identity='1:2',events=len(es),dropped_before=0,dropped_after=0,uncommitted_reserved=0,counters_changed=False,inconclusive_window=False,producer_quiescence_verified=False,live_pid_verified=False,bridge_verified=False)]
def cadence(starts):
 es=[event(2,t,t+1000000)for t in starts]
 return es+[event(1,a,b)for a,b in zip(starts,starts[1:])]
def chunk(seq=1,frame=3):
 return [event(t,1000000000+(t-20)*2000000,obj=99,seq=seq,frame=frame,flags=4 if t==23 else 0)for t in range(20,24)]+[event(24,9000000000000,9000000008000,obj=99,seq=seq,frame=frame,flags=4),event(25,1000000000,obj=11,seq=99,frame=frame)]
class AnalyzerTests(unittest.TestCase):
 def test_empty_no_fps_invention(self):
  a=analyze(rows([]));self.assertEqual(a['cpu_present_streams'],[]);self.assertIsNone(a['queue_chunk_metrics']['gpu_command_buffer_durations'])
 def test_cadence_percentiles_stutter(self):
  a=analyze(rows(cadence([1_000_000,17_000_000,33_000_000,133_000_000])))['cpu_present_streams'][0]['intervals']
  self.assertEqual(a['median_ms'],16);self.assertEqual(a['p95_ms'],100);self.assertEqual(a['over_50ms'],1);self.assertEqual(a['over_100ms'],0)
 def test_gap_not_invented(self):
  es=cadence([1_000_000,17_000_000])+cadence([100_000_000,116_000_000]);a=analyze(rows(es))['cpu_present_streams'][0]
  self.assertEqual(a['segments'],2);self.assertEqual(a['intervals']['samples'],2);self.assertEqual(a['intervals']['max_ms'],16)
 def test_duplicate_stream_rejected(self):
  es=cadence([1_000_000,17_000_000]);es.append(copy.deepcopy(es[-1]));a=analyze(rows(es));self.assertFalse(a['cpu_present_streams']);self.assertEqual(a['rejections']['duplicate_cadence_pair'],1)
 def test_incomplete_present_excluded(self):
  es=cadence([1_000_000,17_000_000]);es.pop(0);self.assertFalse(analyze(rows(es))['cpu_present_streams'])
 def test_gpu_epoch_separate_and_join(self):
  a=analyze(rows(chunk()));c=a['chunks'][0];self.assertEqual(c['gpu_duration_ms'],.008);self.assertEqual(c['publish_to_encode_ms'],2);self.assertEqual(c['swapchain_if_unambiguous'],11)
 def test_duplicate_chunk_rejected(self):
  es=chunk();es.append(copy.deepcopy(es[0]));a=analyze(rows(es));self.assertFalse(a['chunks']);self.assertEqual(a['rejections']['duplicate_chunk_type'],1)
 def test_missing_chunk_not_zero(self):
  es=chunk();es.pop(2);a=analyze(rows(es));self.assertFalse(a['chunks']);self.assertIsNone(a['queue_chunk_metrics']['encode_setup_commit'])
 def test_failed_gpu_not_timing(self):
  es=chunk();es[4]['flags']=5;a=analyze(rows(es));self.assertIsNone(a['chunks'][0]['gpu_duration_ms']);self.assertEqual(a['rejections']['invalid_gpu_interval'],1)
 def test_frame_conflict_rejected(self):
  es=chunk();es[2]['padding1']=4;self.assertFalse(analyze(rows(es))['chunks'])
 def test_multiple_swapchain_no_attribution(self):
  es=chunk();es.append(event(25,1000000000,obj=12,seq=99,frame=3));self.assertIsNone(analyze(rows(es))['chunks'][0]['swapchain_if_unambiguous'])
 def test_clockdomain_mismatch_fail(self):
  r=rows(chunk());r[4]['clock_domain']='cpu_steady_ns'
  with self.assertRaises(ValueError):analyze(r)
 def test_duplicate_index_fails(self):
  r=rows(chunk());r[1]['index']=0
  with self.assertRaises(ValueError):analyze(r)
 def test_drop_and_pending_limits(self):
  r=rows(chunk());r[-1]['dropped_after']=1;r[-1]['uncommitted_reserved']=1;a=analyze(r);self.assertIn('dropped_after',a['quality_limits']);self.assertIn('uncommitted_reserved',a['quality_limits'])
 def test_negative_and_boolean_fails(self):
  for v in [-1,True]:
   r=rows(chunk());r[0]['start']=v
   with self.assertRaises(ValueError):analyze(r)
 def test_cadence_overlapping_calls_rejected(self):
  r=rows(cadence([1_000_000,17_000_000]));r[0]['end']=20_000_000;self.assertFalse(analyze(r)['cpu_present_streams'])
 def test_summary_count_fails(self):
  r=rows(chunk());r[-1]['events']=4
  with self.assertRaises(ValueError):analyze(r)
 def test_failed_completion_not_success_aggregate(self):
  es=chunk();es[3]['flags']=5;a=analyze(rows(es));self.assertIsNone(a['queue_chunk_metrics']['publish_to_encode']);self.assertEqual(a['chunks'][0]['completion_status'],5);self.assertIsNone(a['chunks'][0]['gpu_duration_ms'])
 def test_nonterminal_completion_not_success(self):
  es=chunk();es[3]['flags']=3;a=analyze(rows(es));self.assertEqual(a['queue_chunk_metrics']['successful_completed_chunks'],0)
 def test_invalid_zero_queue_or_sequence(self):
  for key in ['object','padding0']:
   es=chunk()
   for e in es[:5]:e[key]=0
   self.assertFalse(analyze(rows(es))['chunks'])
 def test_invalid_present_join_identity(self):
  for key in ['object','padding0','end']:
   es=chunk();es[-1][key]=0;a=analyze(rows(es));self.assertIsNone(a['chunks'][0]['swapchain_if_unambiguous']);self.assertEqual(a['rejections']['invalid_present_frame_identity'],1)
 def test_present_api_duration_distinct(self):
  a=analyze(rows(cadence([1_000_000,17_000_000])))['cpu_present_streams'][0];self.assertEqual(a['present_cpu_duration']['median_ms'],1);self.assertEqual(a['intervals']['median_ms'],16)
if __name__=='__main__':unittest.main()
