import copy,hashlib,json,math,pathlib,struct,subprocess,tempfile,unittest
import tests_cli
from test_analyze import event,rows
from fixed_window_analysis import analyze_fixed_window,validate_clock_sample

HASH='a'*64
MEASUREMENT='12345678-1234-4234-9234-123456789abc'
SAMPLE1='12345678-1234-4234-9234-123456789ab1'
SAMPLE2='12345678-1234-4234-9234-123456789ab2'

def sample(center_ns,identity):
    lo=(center_ns-50)/1e9;hi=(center_ns+50)/1e9
    return dict(collector_sha256=HASH,measurement_id=MEASUREMENT,clock_sample=dict(kind='clock_sample',schema=1,clock_domain='ca_host_seconds',clock_api='CACurrentMediaTime',monotonic_clock_domain='native_mach_absolute_ns',monotonic_clock_api='mach_absolute_time',sample_id=identity,pid=123,ca_lower_seconds=lo,ca_upper_seconds=hi,native_monotonic_ns=center_ns,native_monotonic_resolution_ns=1,mach_ticks=center_ns,timebase_numer=1,timebase_denom=1,bracket_ns=math.ceil((hi-lo)*1e9)))

def triplet(frame,seconds,*,revision=4,width=1280,height=720,receipt=None,swapchain=11):
    stamp=1000+frame*10 if receipt is None else receipt
    bits=struct.unpack('<Q',struct.pack('<d',seconds))[0]
    return [event(71,stamp,obj=swapchain,seq=frame,frame=bits,flags=0x101),event(72,stamp,obj=swapchain,seq=frame,frame=revision),event(73,stamp,obj=swapchain,seq=frame,frame=width<<32|height)]

def counters(stamp,*,overflow=0,session=5):
    return [event(74,stamp,obj=11,seq=session,frame=value,flags=i) for i,value in enumerate((20,20,0,overflow,0,0,0))]

def fixture(times=None):
    times=[99,99.9,100.1,100.6,101.1,101.6,102.1,103] if times is None else times
    es=[event(70,900,obj=11,seq=1,frame=3)]+counters(950)
    for frame,stamp in enumerate(times,10):es+=triplet(frame,stamp)
    es+=counters(2000)
    result=rows(es)
    result[-1].update(structurally_complete=True,producer_quiescence_verified=True,boundary_rejected_total=0)
    return result

def call(data=None,begin=None,end=None,**kwargs):
    options=dict(window_seconds=2,expected_swapchain=11,expected_dimensions=dict(width=1280,height=720),expected_revision=4,expected_collector_sha256=HASH,measurement_id=MEASUREMENT)
    options.update(kwargs)
    return analyze_fixed_window(fixture() if data is None else data,sample(99_999_999_950,SAMPLE1) if begin is None else begin,sample(102_000_100_050,SAMPLE2) if end is None else end,**options)

class FixedWindowTests(unittest.TestCase):
    def test_exact_window_excludes_envelope_tails_and_boundary_intervals(self):
        r=call();self.assertTrue(r['accepted'],r['rejection_reasons'])
        self.assertEqual(r['window']['start_seconds'],100);self.assertEqual(r['window']['end_seconds'],102)
        self.assertEqual(r['presentation_count'],4);self.assertEqual(r['presentations_per_fixed_second'],2)
        self.assertEqual(r['interior_interval_distribution']['samples'],3);self.assertEqual(r['interior_interval_distribution']['median_ms'],500)
        self.assertEqual(r['spanning_frame_ids'],[11,12,13,14,15,16]);self.assertEqual(r['observed_window_frame_ids'],[12,13,14,15]);self.assertEqual(r['excluded_boundary_crossing_intervals'],2)
    def test_half_open_boundaries_and_zero_frame_stall(self):
        r=call(fixture([99.9,100,100.5,101,101.5,102,102.1]));self.assertTrue(r['accepted']);self.assertEqual(r['presentation_count'],4)
        r=call(fixture([99,99.9,102.1,103]));self.assertTrue(r['accepted']);self.assertEqual(r['presentation_count'],0);self.assertEqual(r['presentations_per_fixed_second'],0);self.assertIsNone(r['interior_interval_distribution'])
    def test_stationary_only_end_reach_and_lateness(self):
        self.assertIn('route_alignment_unproven_only_stationary_admitted',call(scenario='route')['rejection_reasons'])
        for stamp,reason in ((101_999_999_900,'end_clock_sample_does_not_reach_fixed_end'),(105_000_000_000,'end_clock_sample_too_late')):
            r=call(end=sample(stamp,SAMPLE2));self.assertFalse(r['accepted']);self.assertIsNone(r['presentation_count']);self.assertIn(reason,r['rejection_reasons'])
    def test_missing_boundary_gap_config_and_time_regression_reject(self):
        self.assertFalse(call(fixture([100.1,101,102.1]))['accepted'])
        self.assertFalse(call(fixture([99.9,100.1,101.6]))['accepted'])
        for mutation in ('gap','revision','dimensions','regression','duplicate','bad_duplicate','missing','invalid_timestamp'):
            data=fixture();es=data[:-1];target=[e for e in es if e['type']==71][3];frame=target['padding0']
            if mutation=='gap':es[:]=[e for e in es if not (e['type'] in (71,72,73) and e['padding0']==frame)]
            elif mutation in ('revision','dimensions'):
                next(e for e in es if e['type']==(72 if mutation=='revision' else 73) and e['padding0']==frame)['padding1']=999
            elif mutation=='regression':target['padding1']=struct.unpack('<Q',struct.pack('<d',99.5))[0]
            elif mutation in ('duplicate','bad_duplicate'):
                duplicate=copy.deepcopy(target)
                if mutation=='bad_duplicate':duplicate['flags']=1
                es.append(duplicate)
            elif mutation=='missing':es.remove(next(e for e in es if e['type']==72 and e['padding0']==frame))
            else:target['padding1']=struct.unpack('<Q',struct.pack('<d',float('nan')))[0]
            for i,e in enumerate(es):e['index']=i
            data=es+[dict(data[-1],events=len(es))]
            self.assertFalse(call(data)['accepted'],mutation)
    def test_invalid_unlocalizable_tail_cannot_be_hidden_by_subset(self):
        data=fixture();es=data[:-1]+triplet(99,float('nan'));data=rows(es);data[-1].update(structurally_complete=True,producer_quiescence_verified=True,boundary_rejected_total=0)
        self.assertFalse(call(data)['accepted'])
        # Known good geometry transitions wholly outside the spanning interval
        # can be excluded; no accepted interior configuration is inferred from them.
        data=fixture()
        for e in data[:-1]:
            if e['type']==72 and e['padding0'] in (10,17):e['padding1']=999
        self.assertTrue(call(data)['accepted'])
    def test_capture_loss_required_fields_and_historical_exclusion(self):
        for key in ('dropped_before','dropped_after','uncommitted_reserved','boundary_rejected_total'):
            data=fixture();data[-1][key]=1;self.assertFalse(call(data)['accepted'])
            del data[-1][key]
            with self.assertRaises(ValueError):call(data)
        for key in ('structurally_complete','producer_quiescence_verified','inconclusive_window','counters_changed'):
            data=fixture();data[-1].pop(key,None);self.assertFalse(call(data)['accepted'])
        data=fixture();data[-1]['wrong_generation']=9;r=call(data);self.assertTrue(r['accepted']);self.assertEqual(r['exclusions']['historical_generation_records'],9)
        data=fixture();data[-1].pop('identity')
        with self.assertRaises(ValueError):call(data)
    def test_native_counter_missing_incomplete_placement_and_loss(self):
        for mutation in ('missing','incomplete','session','overflow','bad_index','receipt','regression'):
            data=fixture();es=data[:-1]
            if mutation=='missing':es[:]=[e for e in es if not(e['type']==74 and e['start']==2000)]
            elif mutation=='incomplete':es.remove(next(e for e in es if e['type']==74))
            elif mutation=='session':
                for e in es:
                    if e['type']==74 and e['start']==2000:e['padding0']=6
            elif mutation=='overflow':next(e for e in es if e['type']==74 and e['flags']==3)['padding1']=1
            elif mutation=='bad_index':next(e for e in es if e['type']==74)['flags']=99
            elif mutation=='receipt':
                for e in es:
                    if e['type']==74 and e['start']==2000:e['start']=e['end']=960
            else:next(e for e in es if e['type']==74 and e['start']==2000 and e['flags']==0)['padding1']=1
            data=rows(es);data[-1].update(structurally_complete=True,producer_quiescence_verified=True,boundary_rejected_total=0)
            self.assertFalse(call(data)['accepted'],mutation)
        for completed in (0,5):
            data=fixture()
            for e in data[:-1]:
                if e['type']==74:e['padding1']=completed if e['flags'] in (0,1) else 0
            r=call(data);self.assertFalse(r['accepted']);self.assertIn('native_completed_count_cannot_cover_feedback_span',r['rejection_reasons'])
    def test_malformed_native_control_cannot_hide_failure(self):
        for t in (70,74,75,76,77,78):
            data=fixture();es=data[:-1]+[event(t,1500,1501,obj=11,seq=5,flags=4)]
            data=rows(es);data[-1].update(structurally_complete=True,producer_quiescence_verified=True,boundary_rejected_total=0)
            self.assertFalse(call(data)['accepted'],t)
        data=fixture();next(e for e in data if e.get('type')==70)['flags']=2;self.assertFalse(call(data)['accepted'])
    def test_clock_schema_domain_binding_types_and_uncertainty(self):
        for field,value in (('clock_domain','cpu_steady_ns'),('monotonic_clock_api','clock_gettime(CLOCK_MONOTONIC)'),('schema',True),('ca_lower_seconds',float('nan')),('ca_upper_seconds',float('inf')),('ca_upper_seconds',10**1000),('ca_upper_seconds',99),('native_monotonic_ns',True),('bracket_ns',True),('bracket_ns',999999),('mach_ticks',2**64),('timebase_numer',0),('timebase_denom',0),('timebase_numer',True),('pid',0),('sample_id','bogus')):
            begin=sample(99_999_999_950,SAMPLE1);begin['clock_sample'][field]=value
            with self.assertRaises(ValueError,msg=field):call(begin=begin)
        for field,value in (('collector_sha256','b'*64),('measurement_id',SAMPLE2)):
            begin=sample(99_999_999_950,SAMPLE1);begin[field]=value
            with self.assertRaises(ValueError):call(begin=begin)
        begin=sample(99_999_999_950,SAMPLE1);begin['clock_sample']['ca_upper_seconds']=100.01;begin['clock_sample']['bracket_ns']=math.ceil((100.01-begin['clock_sample']['ca_lower_seconds'])*1e9)
        with self.assertRaises(ValueError):call(begin=begin)
        with self.assertRaises(ValueError):call(end=sample(99_999_999_950,SAMPLE1))
        with self.assertRaises(ValueError):call(end=sample(99_000_000_000,SAMPLE2))
        with self.assertRaises(ValueError):call(expected_collector_sha256='unknown')
        begin=sample(99_999_999_950,SAMPLE1);begin['clock_sample']={'kind':'clock_sample','schema':1,'valid':True,'status':'ok'}
        with self.assertRaises(ValueError):call(begin=begin)
    def test_tick_conversion_and_timebase_consistency(self):
        for change in ('ticks','resolution','outside','timebase'):
            end=sample(102_000_100_050,SAMPLE2);s=end['clock_sample']
            if change=='ticks':s['mach_ticks']+=1
            elif change=='resolution':s['native_monotonic_resolution_ns']=42
            elif change=='outside':s['mach_ticks']+=1_000_000;s['native_monotonic_ns']+=1_000_000
            else:s['timebase_numer']=2;s['timebase_denom']=2
            with self.assertRaises(ValueError,msg=change):call(end=end)
        begin=sample(100_000_000_000,SAMPLE1);s=begin['clock_sample'];s['ca_lower_seconds']=s['ca_upper_seconds']=100;s['bracket_ns']=0
        self.assertTrue(call(begin=begin)['accepted'])
    def test_native_clock_command_uses_public_clock_and_caller_hash(self):
        binary=tests_cli.CONTROLLER;digest=hashlib.sha256(binary.read_bytes()).hexdigest()
        samples=[]
        for _ in range(2):
            r=subprocess.run([str(binary),'clock-sample'],capture_output=True,text=True,timeout=5)
            self.assertEqual(r.returncode,0,r.stderr);self.assertEqual(r.stderr,'');self.assertLess(len(r.stdout),2048)
            raw=json.loads(r.stdout);envelope=dict(clock_sample=raw,collector_sha256=digest,measurement_id=MEASUREMENT)
            samples.append(validate_clock_sample(envelope,expected_collector_sha256=digest,measurement_id=MEASUREMENT,max_sample_uncertainty_ns=100_000_000))
        self.assertNotEqual(samples[0]['sample_id'],samples[1]['sample_id']);self.assertGreater(samples[1]['mono'],samples[0]['mono'])
        r=subprocess.run([str(binary),'clock-sample','unexpected'],capture_output=True,text=True,timeout=5);self.assertEqual(r.returncode,2)

    def test_bounded_analysis_cli_accept_reject_and_preserve_inputs(self):
        with tempfile.TemporaryDirectory(prefix='fixed-window-cli-') as td:
            p=pathlib.Path(td);capture=p/'trace.ndjson';begin=p/'begin.json';end=p/'end.json';out=p/'window.json'
            capture.write_text(''.join(json.dumps(e)+'\n' for e in fixture()))
            begin.write_text(json.dumps(sample(99_999_999_950,SAMPLE1)));end.write_text(json.dumps(sample(102_000_100_050,SAMPLE2)))
            command=['python3',str(pathlib.Path(__file__).with_name('fixed_window_analysis.py')),str(capture),str(begin),str(end),'--window-seconds','2','--swapchain','11','--width','1280','--height','720','--revision','4','--collector-sha256',HASH,'--measurement-id',MEASUREMENT,'--output',str(out)]
            r=subprocess.run(command,capture_output=True,text=True,timeout=5);self.assertEqual((r.returncode,r.stderr),(0,''));self.assertTrue(json.loads(out.read_text())['accepted'])
            r=subprocess.run(command+['--scenario','route'],capture_output=True,text=True,timeout=5);self.assertEqual(r.returncode,3);self.assertFalse(json.loads(out.read_text())['accepted'])
            original=capture.read_bytes();r=subprocess.run(command+['--output',str(capture)],capture_output=True,text=True,timeout=5);self.assertEqual(r.returncode,2);self.assertEqual(capture.read_bytes(),original)
            begin.write_bytes(b'x'*8193);r=subprocess.run(command,capture_output=True,text=True,timeout=5);self.assertEqual(r.returncode,2)
            begin.write_text(json.dumps(sample(99_999_999_950,SAMPLE1)));capture.write_bytes(b'x'*8193);r=subprocess.run(command,capture_output=True,text=True,timeout=5);self.assertEqual(r.returncode,2)

if __name__=='__main__':unittest.main()
