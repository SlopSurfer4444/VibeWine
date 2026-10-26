"""Adversarial consumer checks using native decoder and independent Python joins."""
import copy
import unittest
from analyze import analyze
from test_analyze import event
from test_stall_profile import marker,complete,slow
import test_trace_profile as profile_fixture


def card(**changes):
    es=[event(t,1_000_000_000,1_050_000_000,obj=99) for t in range(111,117)]
    es[0].update(flags=3|(4<<3)|64|128,padding0=17,padding1=0)
    es[1].update(padding0=1,padding1=23)
    es[2].update(padding0=22,padding1=1000)
    es[3].update(flags=2,padding0=20,padding1=18)
    es[4].update(padding0=16,padding1=0)
    es[5].update(padding0=8_000_000)
    es[0].update(changes)
    return es


def cases():
    result=[]
    for i in range(6):
        es=card();es.pop(i);result.append(('missing'+str(i),es))
        es=card();es.append(dict(es[i]));result.append(('duplicate'+str(i),es))
        for field,value in [('object',100),('thread',3),('start',1_000_000_001),('end',1_050_000_001)]:
            es=card();es[i][field]=value;result.append(('binding'+str(i)+field,es))
    for changes in (dict(padding0=0),dict(flags=4|(4<<3)),dict(flags=3|(6<<3)),dict(flags=256),dict(object=0),dict(thread=0),dict(start=0)):
        result.append(('identity_status',card(**changes)))
    for i,field,value in [(1,'padding0',2),(1,'flags',1),(2,'flags',1),(3,'flags',33),(3,'padding0',17),
                          (3,'padding1',17),(4,'padding0',17),(4,'flags',1),(5,'flags',1),(5,'padding1',1)]:
        es=card();es[i][field]=value;result.append(('payload',es))
    es=card(flags=3|(5<<3)|128);result.append(('error_gpu',es))
    es=card(flags=3|(4<<3));result.append(('invalid_cpu_gpu_nonzero',es))
    es=card();es[1]['padding1']=0;result.append(('initializer_without_target',es))
    es=card();result.append(('duplicate_chunk',es+[dict(e,start=e['start']+100_000_000,end=e['end']+100_000_000) for e in es]))
    es=card();es[0]['padding1']=2**64-1;es[1]['padding0']=0;result.append(('final_frame_overflow',es))
    es=card()+[slow(110,start=1_050_000_000,end=1_100_000_000,object=99,padding0=17,padding1=3)];result.append(('card_reset_frame_mismatch',es))
    es=card()+[slow(110,start=1_050_000_000,end=1_100_000_000,object=99,padding0=17,thread=4)];result.append(('card_reset_thread_mismatch',es))
    es=card()+[slow(117,start=1_025_000_000,end=1_075_000_000,object=99,padding0=17)];result.append(('card_reset_before_wait_return',es))
    return result


class CompletionAnalyzerTests(unittest.TestCase):
    def report(self,es,limit=100):return analyze(complete([marker(padding0=limit)]+es))

    def test_valid_fields_zero_frame_exact_units_and_no_epoch_join(self):
        report=self.report(card());self.assertTrue(report['stall_capture']['capture_usable'])
        completion=report['stall_capture']['completion_cards'];self.assertTrue(completion['capture_usable'])
        c=completion['cards'][0]
        self.assertEqual((c['queue_birth_serial'],c['chunk_sequence'],c['queue_frame'],c['final_frame_target']),(99,17,0,1))
        self.assertEqual((c['status_before'],c['status_after'],c['wait_cpu_wall_ms'],c['thread_cpu_delta_100ns'],c['thread_cpu_ms'],c['native_gpu_duration_ms']),(3,4,50,1000,.1,8))
        self.assertEqual(len(report['stall_capture']['slow_scopes']),1)
        self.assertEqual(completion['unfinished_waits'],'unobserved_no_begin_records')
        self.assertIsNone(report['stall_capture']['fps'])
        self.assertTrue(self.report(card()+[slow(110,start=1_050_000_000,end=1_100_000_000,object=99,padding0=17)])['stall_capture']['capture_usable'])

    def test_missing_duplicate_binding_and_malformed_fields_fail_independent_summary(self):
        for name,es in cases():
            with self.subTest(name=name,events=es):
                r=self.report(es);self.assertFalse(r['producer_profile']['policy_valid']);self.assertFalse(r['stall_capture']['capture_usable'])
                self.assertFalse(r['stall_capture']['completion_cards']['fields_valid'])

    def test_optional_cpu_gpu_unavailable_and_zero_native_duration(self):
        es=card(flags=3|(5<<3));es[2]['padding1']=0;es[5]['padding0']=0;es[1]['padding0']=2**64-1
        c=self.report(es)['stall_capture']['completion_cards']['cards'][0]
        self.assertIsNone(c['thread_cpu_delta_100ns']);self.assertIsNone(c['native_gpu_duration_ns']);self.assertIsNone(c['final_frame_target'])
        es=card();es[5]['padding0']=0
        self.assertEqual(self.report(es)['stall_capture']['completion_cards']['cards'][0]['native_gpu_duration_ns'],0)
        es=card(flags=3|(2<<3));es[2]['padding1']=0;es[5]['padding0']=0
        self.assertEqual(self.report(es)['stall_capture']['completion_cards']['cards'][0]['status_after'],2)

    def test_cap_stale_stop_and_wrong_clock_fail_closed(self):
        cap=event(89,1_060_000_000,obj=0,flags=1,seq=4)
        r=self.report(card()[:4]+[cap],4)
        self.assertFalse(r['stall_capture']['capture_usable']);self.assertFalse(r['stall_capture']['completion_cards']['fields_valid'])
        stale=event(89,1_060_000_000,obj=0,flags=2,seq=6)
        self.assertFalse(self.report(card()+[stale])['stall_capture']['capture_usable'])
        for fields in (dict(inconclusive_window=True),dict(structurally_complete=False),dict(producer_quiescence_verified=False),dict(uncommitted_reserved=1),dict(dropped_after=1)):
            data=complete([marker(padding0=100)]+card());data[-1].update(fields)
            self.assertFalse(analyze(data)['stall_capture']['capture_usable'])
            self.assertFalse(analyze(data)['stall_capture']['completion_cards']['capture_usable'])
        data=complete([marker(padding0=100)]+card());data[3]['clock_domain']='native_gpu_ns'
        with self.assertRaises(ValueError):analyze(data)
        data=complete([marker(padding0=100)]+card());data[3]['generation']=8
        with self.assertRaises(ValueError):analyze(data)

    def test_missing_profile_with_forged_valid_summary_rejected(self):
        r=analyze(complete(card()));self.assertFalse(r['producer_profile']['policy_valid'])
        self.assertFalse(r['stall_capture']['completion_cards']['fields_valid'])

    def test_card_budget_zero_too_small_and_nonstall_profile(self):
        for limit in (0,5):self.assertFalse(self.report(card(),limit)['stall_capture']['capture_usable'])
        self.assertFalse(self.report([slow(117,padding0=17)],0)['producer_profile']['policy_valid'])
        for p in (profile_fixture.marker(flags=1),profile_fixture.marker(flags=3)):
            self.assertFalse(analyze(complete([p]+card()))['producer_profile']['policy_valid'])

    def test_no_cards_does_not_claim_unfinished_coverage(self):
        r=self.report([slow()]);self.assertTrue(r['stall_capture']['capture_usable'])
        c=r['stall_capture']['completion_cards'];self.assertEqual(c['coverage'],'unknown');self.assertFalse(c['capture_usable'])
        self.assertEqual(c['unfinished_waits'],'unobserved_no_begin_records')

    def test_reset_subscopes_bind_contain_and_order(self):
        es=[slow(110,start=1_000_000_000,end=1_200_000_000)]+[slow(t,padding0=17,start=1_000_000_000+i*50_000_000,end=1_050_000_000+i*50_000_000) for i,t in enumerate((117,118,119))]
        self.assertTrue(self.report(es)['stall_capture']['capture_usable'])
        for changes in (dict(padding1=3),dict(thread=4),dict(flags=1),dict(padding0=0),dict(start=900_000_000,end=950_000_000)):
            bad=copy.deepcopy(es);bad[1].update(changes)
            self.assertFalse(self.report(bad)['stall_capture']['capture_usable'])
        bad=copy.deepcopy(es);bad[2]['start']=1_025_000_000;bad[2]['end']=1_075_000_000
        self.assertFalse(self.report(bad)['stall_capture']['capture_usable'])


class CompletionBinaryTests(unittest.TestCase):
    setUpClass=classmethod(profile_fixture.TraceProfileTests.setUpClass.__func__)
    tearDownClass=classmethod(profile_fixture.TraceProfileTests.tearDownClass.__func__)
    fixture=profile_fixture.TraceProfileTests.fixture
    append=profile_fixture.TraceProfileTests.append
    decode=profile_fixture.TraceProfileTests.decode

    def test_native_valid_card_and_zero_duration(self):
        decoded,report=self.decode([marker(padding0=100)]+card())
        self.assertTrue(decoded[-1]['producer_profile_policy_valid']);self.assertTrue(report['stall_capture']['completion_cards']['capture_usable'])
        self.assertEqual(decoded[1]['completion_card_part'],1)
        es=card();es[5]['padding0']=0;self.decode([marker(padding0=100)]+es)
        self.decode([marker(padding0=100)]+card()+[slow(110,start=1_050_000_000,end=1_100_000_000,object=99,padding0=17)])

    def test_native_adversarial_cards_fail(self):
        for name,es in cases():
            with self.subTest(name=name):
                decoded,report=self.decode([marker(padding0=100)]+es,3)
                self.assertFalse(decoded[-1]['producer_profile_policy_valid']);self.assertFalse(report['stall_capture']['capture_usable'])

    def test_native_cap_and_nonstall_cards_fail(self):
        self.decode([marker(padding0=5)]+card(),3)
        self.decode([marker(padding0=0),slow(117,padding0=17)],3)
        self.decode([marker(padding0=4)]+card()[:4]+[event(89,1_060_000_000,obj=0,flags=1,seq=4)],3)
        self.decode([profile_fixture.marker(flags=3,padding0=100)]+card(),3)
        self.decode(card(),3)

    def test_native_reset_subscopes_bind_contain_and_order(self):
        es=[slow(110,start=1_000_000_000,end=1_200_000_000)]+[slow(t,padding0=17,start=1_000_000_000+i*50_000_000,end=1_050_000_000+i*50_000_000) for i,t in enumerate((117,118,119))]
        self.decode([marker(padding0=100)]+es)
        for changes in (dict(padding1=3),dict(thread=4),dict(flags=1),dict(padding0=0),dict(start=900_000_000,end=950_000_000)):
            bad=copy.deepcopy(es);bad[1].update(changes)
            self.decode([marker(padding0=100)]+bad,3)
        bad=copy.deepcopy(es);bad[2]['start']=1_025_000_000;bad[2]['end']=1_075_000_000
        self.decode([marker(padding0=100)]+bad,3)

    def test_native_stop_requires_quiescent_complete_records(self):
        fixture=self.fixture();fixture.arm(7);self.append(fixture,[marker(padding0=100)]+card())
        # A decoder must refuse open capture even if its rows form a card.
        self.assertEqual(fixture.call('decode',fixture.path,7,fixture.identity).returncode,1)
        fixture.stop(7)
        decoded=fixture.rows(fixture.call('decode',fixture.path,7,fixture.identity))
        self.assertTrue(analyze(decoded)['stall_capture']['completion_cards']['capture_usable'])


if __name__=='__main__':unittest.main()
