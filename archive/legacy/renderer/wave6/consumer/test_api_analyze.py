import unittest
from analyze import analyze
class APIAnalysis(unittest.TestCase):
 def test_uncensored_hresult_grouping_and_structural_scope(self):
  rows=[]
  for i,(t,result,duration) in enumerate([(40,0,50),(40,0,70),(40,0x887a0001,9),(41,1,2),(43,0,3),(43,1,4)]):
   rows.append(dict(kind='event',index=i,generation=7,type=t,thread=1,flags=result,object=42,start=100,end=100+duration,padding0=0,padding1=0,clock_domain='cpu_steady_ns'))
  rows.append(dict(kind='summary',generation=7,events=len(rows),identity='1:2',inconclusive_window=False,structurally_complete=True,producer_quiescence_verified=True,dropped_before=0,dropped_after=0,uncommitted_reserved=0,counters_changed=False,live_pid_verified=False,bridge_verified=False))
  result=analyze(rows);groups=result['full_api_boundaries']
  success=[g for g in groups if g['type']==40 and g['result_or_flush_submission']==0][0]
  self.assertEqual(success['observed_calls'],2);self.assertEqual(success['sum_observed_duration_ns'],120)
  self.assertEqual(len([g for g in groups if g['type']==43]),2)
  self.assertIn('scopes_or_gpu_chunks_crossing_stop_are_not_complete',result['quality_limits'])
  self.assertEqual(result['cpu_present_streams'],[])
