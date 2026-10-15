import copy,unittest
from analyze import analyze,REASONS
from test_analyze import event,rows,chunk

def reason(code=1,seq=1,frame=3,queue=99,start=1_000_000_001):
 return event(33,start,obj=queue,seq=seq,frame=frame,flags=code)
def sync(t=30,seq=1,frame=4,queue=99,flags=1,start=1_200_000_000,end=1_200_001_000):
 return event(t,start,end,obj=seq,seq=queue,frame=frame,flags=flags)
def result(es):return analyze(rows(es))
class SyncAnalyzerTests(unittest.TestCase):
 def test_absent_reason_remains_unattributed(self):
  a=result(chunk());self.assertIsNone(a['chunks'][0]['commit_reason']);self.assertIsNone(a['chunks'][0]['commit_reason_code']);self.assertFalse(a['sync_probes']['commit_reason_records'])
 def test_all_reason_codes_including_unknown(self):
  for code,name in REASONS.items():
   a=result(chunk()+[reason(code)]);self.assertEqual(a['chunks'][0]['commit_reason'],name);self.assertEqual(a['sync_probes']['commit_reason_records'][0]['duration_ms'],None)
 def test_later_reason_marker_has_no_timing_inference(self):
  a=result(chunk()+[reason(start=99_000_000_000)])
  self.assertEqual(a['chunks'][0]['commit_reason'],'Present');self.assertEqual(a['chunks'][0]['publish_to_encode_ms'],2)
 def test_noninstant_reason_rejected(self):
  e=reason();e['end']+=1;a=result(chunk()+[e]);self.assertIsNone(a['chunks'][0]['commit_reason']);self.assertEqual(a['rejections']['noninstant_commit_reason'],1)
 def test_unknown_enum_rejected(self):
  for code in [9,4294967295]:
   a=result(chunk()+[reason(code)]);self.assertIsNone(a['chunks'][0]['commit_reason']);self.assertEqual(a['rejections']['invalid_commit_reason_code'],1)
 def test_duplicate_reason_including_malformed_poison(self):
  for bad in [reason(),reason(9)]:
   a=result(chunk()+[reason(),bad]);self.assertIsNone(a['chunks'][0]['commit_reason']);self.assertEqual(a['rejections']['duplicate_commit_reason'],1)
 def test_reason_frame_mismatch(self):
  a=result(chunk()+[reason(frame=4)]);self.assertIsNone(a['chunks'][0]['commit_reason']);self.assertEqual(a['sync_probes']['commit_reason_records'][0]['chunk_join_state'],'frame_disagreement')
 def test_wrong_queue_reason_not_joined(self):
  a=result(chunk()+[reason(queue=199)]);self.assertIsNone(a['chunks'][0]['commit_reason']);self.assertEqual(a['sync_probes']['commit_reason_records'][0]['chunk_join_state'],'missing_or_rejected_chunk')
 def test_orphan_boundary_reason_retained_without_cost(self):
  a=result([reason()]);self.assertFalse(a['chunks']);self.assertEqual(a['sync_probes']['observed_commit_reason_counts'],{'Present':1});self.assertIsNone(a['sync_probes']['commit_reason_records'][0]['duration_ms'])
 def test_incomplete_chunk_does_not_join_wait_or_reason(self):
  es=chunk();es.pop(2);a=result(es+[reason(),sync()]);self.assertFalse(a['chunks']);self.assertIsNone(a['sync_probes']['cpu_scopes'][0]['awaited_chunk_if_observed'])
 def test_duplicate_chunk_does_not_join_wait_or_reason(self):
  es=chunk();es.append(copy.deepcopy(es[0]));a=result(es+[reason(),sync()]);self.assertFalse(a['chunks']);self.assertIsNone(a['sync_probes']['cpu_scopes'][0]['awaited_chunk_if_observed'])
 def test_wait_frame_separate_from_historical_chunk_frame(self):
  a=result(chunk()+[reason(),sync(),sync(32,flags=0)]);scopes=a['sync_probes']['cpu_scopes'];self.assertEqual(len(scopes),2)
  for s in scopes:self.assertEqual(s['call_frame'],4);self.assertEqual(s['awaited_chunk_if_observed']['frame'],3);self.assertEqual(s['awaited_chunk_if_observed']['commit_reason'],'Present')
 def test_query_pointer_numerically_equal_sequence_never_joined(self):
  a=result(chunk()+[sync(31,flags=0)]);s=a['sync_probes']['cpu_scopes'][0];self.assertEqual(s['async_pointer'],1);self.assertIsNone(s['awaited_chunk_if_observed']);self.assertNotIn('awaited_sequence',s)
 def test_valid_map_query_flags_and_zero_duration(self):
  es=[sync(flags=f,end=1_200_000_000)for f in [1,2,3]]+[sync(31,flags=f)for f in [0,1,2,3,5]]+[sync(32,flags=0)]
  a=result(es);self.assertEqual(len(a['sync_probes']['cpu_scopes']),9);self.assertEqual(a['sync_probes']['cpu_scopes'][0]['observed_cpu_duration_ms'],0)
 def test_invalid_flags_rejected(self):
  for t,flags in [(30,0),(30,4),(31,4),(31,6),(32,1)]:
   a=result([sync(t,flags=flags)]);self.assertFalse(a['sync_probes']['cpu_scopes']);self.assertEqual(a['rejections']['invalid_sync_flags'],1)
 def test_negative_interval_rejected(self):
  a=result([sync(start=20,end=10)]);self.assertFalse(a['sync_probes']['cpu_scopes']);self.assertEqual(a['rejections']['invalid_sync_interval'],1)
 def test_zero_sync_identity_rejected(self):
  for field in ['object','padding0']:
   e=sync();e[field]=0;a=result([e]);self.assertFalse(a['sync_probes']['cpu_scopes']);self.assertEqual(a['rejections']['invalid_sync_identity'],1)
 def test_zero_reason_identity_rejected(self):
  for field in ['object','padding0']:
   e=reason();e[field]=0;a=result([e]);self.assertFalse(a['sync_probes']['commit_reason_records']);self.assertEqual(a['rejections']['invalid_commit_reason_identity'],1)
 def test_frame_zero_valid(self):
  es=chunk(frame=0)+[reason(frame=0),sync(frame=0)];a=result(es);self.assertEqual(a['chunks'][0]['commit_reason'],'Present');self.assertEqual(a['sync_probes']['cpu_scopes'][0]['awaited_chunk_if_observed']['frame'],0)
 def test_duplicate_wait_calls_are_distinct(self):
  a=result([sync(),sync()]);self.assertEqual(len(a['sync_probes']['cpu_scopes']),2);self.assertEqual(a['sync_probes']['cpu_scope_duration_groups'][0]['observed_cpu_duration']['samples'],2);self.assertIsNone(a['sync_probes']['cpu_scope_duration_groups'][0]['summed_cpu_time'])
 def test_failed_chunk_identity_observed_without_gpu_success_claim(self):
  es=chunk();es[3]['flags']=5;a=result(es+[reason(),sync()]);s=a['sync_probes']['cpu_scopes'][0];self.assertEqual(s['awaited_chunk_if_observed']['completion_status'],5);self.assertIsNone(a['queue_chunk_metrics']['publish_to_encode'])
 def test_native_uint32_and_wrong_generation_rejected(self):
  for field in ['generation','type','thread','flags']:
   r=rows([reason()]);r[0][field]=2**32
   with self.assertRaises(ValueError):analyze(r)
  r=rows([reason()]);r[0]['generation']=8
  with self.assertRaises(ValueError):analyze(r)
  r=rows([]);r[-1]['generation']=2**32
  with self.assertRaises(ValueError):analyze(r)
 def test_new_clock_domain_rejected(self):
  r=rows([sync()]);r[0]['clock_domain']='native_gpu_ns'
  with self.assertRaises(ValueError):analyze(r)
 def test_boundary_quality_retained_no_complete_window(self):
  r=rows([sync(),reason()]);r[-1]['uncommitted_reserved']=1;a=analyze(r);self.assertIn('uncommitted_reserved',a['quality_limits']);self.assertIn('producer_quiescence_unverified_boundary_events_may_be_missing',a['quality_limits'])
if __name__=='__main__':unittest.main()
