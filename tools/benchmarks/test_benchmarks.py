import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
import run
import analyze


class ProtocolTests(unittest.TestCase):
    def test_offline_analysis_cannot_override_declared_presentation_mode(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory=Path(tmp)
            (directory/'run.json').write_text(json.dumps(dict(requestedProfile='presentation-waits', requestedPresentEvery=1)))
            guest=dict(status='complete',frames=500,presentEvery=16,measuredPresentCalls=31)
            with self.assertRaisesRegex(ValueError,'mode differs from recorded declaration'):
                analyze.analyze_run(directory,guest)

    def test_diagnostic_render_iterations_cannot_masquerade_as_presented_frames(self):
        run.validate_presentation_result({'frames': 500}, 1)  # historical default
        run.validate_presentation_result(dict(frames=500,presentEvery=16,measuredPresentCalls=31), 16)
        for result in (dict(frames=500), dict(frames=500,presentEvery=1,measuredPresentCalls=500),
                       dict(frames=500,presentEvery=16,measuredPresentCalls=500),
                       dict(frames=500,presentEvery=16,measuredPresentCalls=31.0),
                       dict(frames=500,presentEvery=True,measuredPresentCalls=31)):
            with self.assertRaises(ValueError): run.validate_presentation_result(result,16)

    def test_identity_refuses_replayed_or_changed_pid(self):
        for value in ({'schema':1,'runId':'old','pid':42},
                      {'schema':1,'runId':'new','pid':43},
                      {'schema':1,'runId':'new','pid':True}):
            with self.assertRaises(ValueError):
                run.bound_message(value, 'new', 42)
        self.assertEqual(run.bound_message({'schema':1,'runId':'new','pid':42}, 'new', 42)['pid'],42)

    def test_atomic_receipt_does_not_overwrite(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'go.json'
            run.write_json(path, {'pid':42})
            with self.assertRaises(FileExistsError):
                run.write_json(path, {'pid':43})
            self.assertEqual(run.read_json(path), {'pid':42})
            self.assertEqual(len(list(Path(tmp).iterdir())),1)

    def test_symlink_input_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            actual=Path(tmp)/'actual';actual.write_text('{}')
            link=Path(tmp)/'link';link.symlink_to(actual)
            with self.assertRaises(ValueError):run.read_json(link)

    def test_collector_changed_before_invocation(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'cli';path.write_text('bytes')
            with patch.object(run, 'command') as command:
                with self.assertRaises(ValueError):run.controller(path,'0'*64,'inspect')
                command.assert_not_called()

    def test_no_ready_after_early_exit(self):
        class Child:
            returncode=7
            def poll(self):return 7
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(ValueError,'guest exited'):
                run.wait_for(Path(tmp)/'ready.json',Child(),time.monotonic()+1)

    def test_cap_checked_while_waiting(self):
        class Child:
            def poll(self):return None
        with tempfile.TemporaryDirectory() as tmp:
            def tick():raise ValueError('budget exceeded')
            with self.assertRaisesRegex(ValueError,'budget exceeded'):
                run.wait_for(Path(tmp)/'results.json',Child(),time.monotonic()+1,tick=tick)

    def _fake_lifecycle(self, actual=True, probe_failures=None, invalid_probe_proof=False):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);cli=root/'cli';cli.write_text('fake')
            guest=root/'guest';guest.write_text('fake')
            args=run.parser().parse_args(['--wine',str(cli),'--prefix',str(root),'--guest',str(guest),
                '--trace-controller',str(cli),'--output',str(root/'out'),'--module',str(cli)])
            args.environment={}
            args.frames=500
            operations=[]
            session=root/'dxmt-trace-session-fake';session.mkdir()
            inspections=[(0,0),(1,8),(0,8),(2,20)]
            class Child:
                pid=4321
                returncode=0
                def poll(self):return 0
            def control(_cli,_hash,*argv):
                operations.append(argv[0])
                if argv[0]=='new-session':return {'session':str(session)}
                if argv[0]=='clock-sample':return {'clock':'fake'}
                if argv[0]=='inspect':
                    generation,count=inspections.pop(0)
                    return dict(identity='1:2',capture=generation,next_reserved=count,dropped=0,remaining_capacity=131072-count)
                if argv[0]=='arm':
                    self.assertEqual(argv[3],'1:2')
                    return dict(capture=int(argv[2]),generation=int(argv[2]))
                if argv[0]=='stop':
                    self.assertEqual(argv[3],'1:2')
                    return dict(capture=0,generation=int(argv[2]),producer_quiescence_verified=True)
                raise AssertionError(argv)
            def wait(path,child,deadline,logs=(),tick=None):
                run_id=path.parent.name[len('cube-'):]
                value=dict(schema=1,runId=run_id,pid=42,scene='cube')
                if path.name=='ready.json':
                    with open(session/'events-42.bin','wb') as stream:stream.truncate(run.RAW_BYTES)
                    self.assertNotIn('arm',operations)
                elif path.name=='probe-done.json':
                    if not invalid_probe_proof:
                        value.update(gpuEventComplete=True,cpuReadbackComplete=True,readbackValue=1685613940)
                    self.assertEqual(operations[-1],'arm')
                    self.assertEqual(run.read_json(path.parent/'probe.json')['pid'],42)
                    self.assertFalse((path.parent/'go.json').exists())
                else:
                    self.assertTrue((path.parent/'go.json').exists())
                    tick()
                    value.update(status='complete',frames=1,width=1280,height=720)
                    (path.parent/'perframe.csv').write_text('frame,cpuFrameMs,presentCallMs\n0,2,1\n')
                return value
            with patch.object(run,'controller',side_effect=control), \
                 patch.object(run.subprocess,'Popen',return_value=Child()) as spawned, \
                 patch.object(run,'native_binding',return_value={'nativePid':4321,'birth':'now','mappedModules':{str(cli.resolve()):run.digest(cli)}}), \
                 patch.object(run,'wait_for',side_effect=wait), \
                 patch.object(run,'command',return_value=(json.dumps({'kind':'event','type':40 if actual else 95})+'\n'+json.dumps({'kind':'summary','identity':'1:2','generation':1})+'\n').encode()), \
                 patch.object(analyze,'native_trace',return_value={'usable':not probe_failures,'observationUsable':not probe_failures,
                     'failures':probe_failures or [], 'rejections':{'incomplete_chunk':1,'commit_reason_missing_or_rejected_chunk':1} if probe_failures else {}}), \
                 patch.object(analyze,'analyze_run',return_value={'trace':{'usable':True,'observationUsable':True,'failures':[]}}):
                result,out=run.execute_scene(args,'cube',root)
                argv=spawned.call_args.args[0]
                self.assertEqual(argv[argv.index('--frames')+1], '500')
                self.assertNotIn('--present-every', argv)  # old guest compatibility
            accepted=actual and not probe_failures and not invalid_probe_proof
            self.assertEqual(result['status'],'complete' if accepted else 'failed',result)
            if not invalid_probe_proof:self.assertEqual(result['preMeasureRecords'],8)
            self.assertEqual(operations[-1],'clock-sample')
            self.assertEqual((out/'go.json').exists(),bool(accepted))
            self.assertEqual(run.read_json(out/'run.json')['guestExit'],0)
            if not actual:self.assertIn('no parsed useful Present',result['error'])
            if invalid_probe_proof:
                persisted=run.read_json(out/'run.json')
                self.assertIn('probeCompletion',persisted)
                self.assertIn('probe GPU EVENT completion proof',persisted['error'])
                self.assertEqual(operations.count('arm'),1)
            if probe_failures:
                persisted=run.read_json(out/'run.json')
                self.assertEqual(persisted['probeAnalysis']['failures'],probe_failures)
                self.assertEqual(persisted['probeAnalysis']['rejections']['incomplete_chunk'],1)
                self.assertIn('rejected_trace_records',persisted['error'])
                self.assertIn('commit_reason_missing_or_rejected_chunk',persisted['error'])
                self.assertTrue((out/'probe.ndjson').exists())
                self.assertEqual(operations.count('arm'),1)

    def test_probe_completion_requires_strict_true_and_integer_sentinel(self):
        valid=dict(gpuEventComplete=True,cpuReadbackComplete=True,readbackValue=1685613940)
        run.validate_probe_completion(valid)
        for key in ('gpuEventComplete','cpuReadbackComplete','readbackValue'):
            missing=valid.copy();missing.pop(key)
            with self.assertRaises(ValueError):run.validate_probe_completion(missing)
        for key in ('gpuEventComplete','cpuReadbackComplete'):
            for value in (False,1,'true',None):
                corrupt=dict(valid,**{key:value})
                with self.assertRaises(ValueError):run.validate_probe_completion(corrupt)
        for value in (1685613941,1685613940.0,'1685613940',True,None):
            with self.assertRaises(ValueError):run.validate_probe_completion(dict(valid,readbackValue=value))

    def test_probe_without_checked_completion_proofs_cannot_release_go(self):
        self._fake_lifecycle(invalid_probe_proof=True)

    def test_complete_fake_lifecycle_keeps_barriers_after_native_admission(self):
        self._fake_lifecycle()

    def test_rejected_probe_analysis_is_saved_before_refusing_go(self):
        self._fake_lifecycle(probe_failures=['rejected_trace_records'])

    def test_metadata_only_probe_cannot_release_measurement(self):
        self._fake_lifecycle(actual=False)

    def test_native_controller_is_ready_before_launch(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);cli=root/'cli';cli.write_text('fake')
            guest=root/'guest';guest.write_text('fake')
            args=run.parser().parse_args(['--wine',str(cli),'--prefix',str(root),'--guest',str(guest),
                '--trace-controller',str(cli),'--output',str(root/'out'),'--module',str(cli)])
            args.environment={}
            operations=[]
            def control(_cli,_hash,*argv):
                operations.append(argv[0])
                if argv[0]=='new-session':return {'session':str(root)}
                return {'clock':'fake'}
            def launch(*a,**kw):
                self.assertEqual(operations,['new-session','clock-sample'])
                receipts=list(root.glob('cube-*/observer-ready.json'))
                self.assertEqual(len(receipts),1)
                self.assertFalse(run.read_json(receipts[0])['producerAttached'])
                raise OSError('fake launch fails')
            with patch.object(run,'controller',side_effect=control), patch.object(run.subprocess,'Popen',side_effect=launch):
                result,_=run.execute_scene(args,'cube',root)
            self.assertEqual(result['status'],'failed')
            self.assertIn('fake launch fails',result['error'])


class CPUAnalysisTests(unittest.TestCase):
    def test_gapped_valid_frames_cannot_claim_a_native_cadence(self):
        missing = dict(observed_interval_rate_hz=None, intervals=None, stream_state='observed')
        self.assertEqual(analyze.cadence_status([missing]), 'inconclusive-no-qualified-native-cadence')
        for rate in (None, 0, float('nan'), float('inf')):
            invalid = dict(observed_interval_rate_hz=rate, intervals={'samples':1}, stream_state='observed')
            self.assertEqual(analyze.cadence_status([invalid]), 'inconclusive-no-qualified-native-cadence')
        valid = dict(observed_interval_rate_hz=60., intervals={'samples':1}, stream_state='observed')
        self.assertEqual(analyze.cadence_status([valid]), 'observed-native-cadence')

    def test_separate_cpu_frame_and_present_distributions(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'frames.csv'
            path.write_text('frame,cpuFrameMs,presentCallMs\n0,4,1\n1,8,2\n')
            result=analyze.cpu_frames(path,2)
            self.assertEqual(result['frame']['medianMs'],6)
            self.assertEqual(result['presentCall']['medianMs'],1.5)
            self.assertNotIn('fps',result)
            with self.assertRaises(ValueError):analyze.cpu_frames(path,3)

    def test_invalid_cpu_timings_and_missing_frames_rejected(self):
        for rows in ('0,nan,1\n','0,1,2\n','1,1,0\n',''):
            with tempfile.TemporaryDirectory() as tmp:
                path=Path(tmp)/'frames.csv'
                path.write_text('frame,cpuFrameMs,presentCallMs\n'+rows)
                with self.assertRaises(ValueError):analyze.cpu_frames(path)

    def test_empty_native_records_cannot_be_metrics(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'trace.ndjson';path.write_text('')
            with self.assertRaisesRegex(ValueError,'summary'):analyze.native_trace(path)


class NativeProfileTests(unittest.TestCase):
    def _trace(self, marker=None, timings=False, expected_profile='combined-bounded', native=False, mutation=None, summary_patch=None):
        rows=[]
        def event(kind,index,flags=0,obj=7,start=1000000,end=1200000,p0=0,p1=0):
            return dict(kind='event',index=index,generation=1,type=kind,thread=1,flags=flags,
                        object=obj,start=start,end=end,padding0=p0,padding1=p1,clock_domain='cpu_steady_ns',duration_ns=end-start if kind in (3,40) else None)
        if marker is not None:
            rows.append(event(95,0,flags=marker,obj=1,start=1,end=1,p0=12000,p1=131072))
        rows.append(event(3 if marker==1 else 40,len(rows)))
        if timings:
            for kind in range(20,24):
                at=1000000+(kind-20)*100000
                rows.append(event(kind,len(rows),flags=4 if kind==23 else 0,start=at,end=at,p0=1,p1=1))
            gpu=event(24,len(rows),flags=4,start=900000000000,end=900000500000,p0=1,p1=1)
            gpu['clock_domain']='native_gpu_ns';rows.append(gpu)
            rows.append(event(2,len(rows),start=10000000,end=10010000))
            rows.append(event(2,len(rows),start=11000000,end=11010000))
            rows.append(event(1,len(rows),start=10000000,end=11000000))
        if native:
            for kind,flags,p0,p1 in ((70,0,1,3),(71,1,1,0),(72,0,1,1),(73,0,1,(1280<<32)|720),
                                     (75,0,3,0),(78,0,2,0)):
                rows.append(event(kind,len(rows),flags=flags,start=20000000,end=20000000,p0=p0,p1=p1))
        if mutation is not None:
            mutation(rows,event)
            for index,row in enumerate(rows):row['index']=index
        rows.append(dict(kind='summary',identity='1:2',generation=1,events=len(rows),
            structurally_complete=True,producer_quiescence_verified=True,
            dropped_before=0,dropped_after=0,uncommitted_reserved=0,
            counters_changed=False,inconclusive_window=False,boundary_rejected_total=0,
            producer_profile_policy_valid=True,live_pid_verified=False,bridge_verified=False))
        if summary_patch:rows[-1].update(summary_patch)
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'trace.ndjson'
            path.write_text(''.join(json.dumps(row)+'\n' for row in rows))
            return analyze.native_trace(path, expected_profile=expected_profile)

    def test_actual_gpu_and_cpu_cadence_summaries_keep_separate_clock_scopes(self):
        result=self._trace(marker=5,timings=True)
        chunks=result['queueChunkMetrics']
        self.assertEqual(chunks['joined_chunks'],1)
        self.assertEqual(chunks['successful_completed_chunks'],1)
        self.assertEqual(chunks['gpu_command_buffer_durations']['median_ms'],.5)
        self.assertEqual(chunks['gpu_command_buffer_durations']['p99_ms'],.5)
        self.assertAlmostEqual(chunks['publish_to_encode']['median_ms'],.1)
        self.assertEqual(len(result['cpuPresentStreams']),1)
        self.assertEqual(result['cpuPresentStreams'][0]['intervals']['median_ms'],1)
        self.assertIn('not displayed FPS',result['cpuPresentStreams'][0]['metric'])
        self.assertIn('native GPU clock',result['queueChunkMetricsScope'])
        self.assertNotIn('chunks',result)

    def test_missing_metadata_cannot_admit_benchmark_probe(self):
        for expected in ('presentation-waits','combined-bounded'):
            result=self._trace(expected_profile=expected)
            self.assertTrue(result['structurallyUsable'])
            self.assertFalse(result['producerProfile']['metadata_observed'])
            self.assertFalse(result['usable'])
            self.assertIn('expected_explicit_requested_profile_metadata',result['failures'])

    def test_light_profile_admits_only_observations_with_native_coverage_incomplete(self):
        result=self._trace(marker=3,timings=True,native=True,expected_profile='presentation-waits')
        self.assertTrue(result['observationUsable'],result['failures'])
        self.assertFalse(result['usable'])
        self.assertFalse(result['producerProfile']['capture_usable'])
        self.assertTrue(result['producerProfile']['policy_valid'])
        self.assertFalse(result['captureCoverage']['fullNativePresentationAccepted'])
        self.assertEqual(result['presentationCoverage']['observed_contradictions'],[])
        self.assertIn('native_close_pending_handlers',result['presentationCoverage']['failures'])
        self.assertEqual(result['nativePresentation']['zeroPresentedTimeObservations'],1)

    def test_light_profile_requires_real_cpu_and_completed_gpu_samples(self):
        result=self._trace(marker=3,expected_profile='presentation-waits')
        self.assertFalse(result['observationUsable'])
        self.assertIn('no_accepted_gpu_command_buffer_samples',result['failures'])

    def test_light_profile_rejects_malformed_feedback_loss_duplicates_and_discard(self):
        def change71(rows,event):next(row for row in rows if row['type']==71).update(flags=257)
        def duplicate71(rows,event):rows.append(dict(next(row for row in rows if row['type']==71)))
        def duplicate75(rows,event):rows.append(dict(next(row for row in rows if row['type']==75)))
        def discard(rows,event):next(row for row in rows if row['type']==78).update(padding1=1)
        def counterloss(rows,event):rows.append(event(74,len(rows),flags=3,start=30000000,end=30000000,p0=2,p1=1))
        def missingcpu(rows,event):rows[:]=[row for row in rows if row['type']!=40]
        def regression(rows,event):
            for at,accepted in ((30000000,10),(40000000,9)):
                for index in range(7):rows.append(event(74,len(rows),flags=index,start=at,end=at,p0=2,p1=accepted if index==0 else 0))
        for mutation in (change71,duplicate71,duplicate75,discard,counterloss,regression,missingcpu):
            with self.subTest(mutation=mutation.__name__):
                result=self._trace(marker=3,timings=True,native=True,expected_profile='presentation-waits',mutation=mutation)
                self.assertFalse(result['observationUsable'],result)
                self.assertFalse(result['captureCoverage']['fullNativePresentationAccepted'])
                if mutation is missingcpu:
                    self.assertIn('no_actual_cpu_present_records',result['failures'])
                else:
                    self.assertTrue(result['presentationCoverage']['observed_contradictions'])

    def test_light_profile_never_admits_decoder_loss_or_uncommitted_boundaries(self):
        for field in ('dropped_before','dropped_after','uncommitted_reserved','boundary_rejected_total'):
            result=self._trace(marker=3,timings=True,native=True,expected_profile='presentation-waits',summary_patch={field:1})
            self.assertFalse(result['observationUsable'])
            self.assertFalse(result['structurallyUsable'])
            self.assertIn(field,result['structuralFailures'])

    def test_requested_allowed_profile_must_match_observed_metadata(self):
        for expected, wrong_marker in (('presentation-waits',5),('combined-bounded',3)):
            result=self._trace(marker=wrong_marker,expected_profile=expected)
            self.assertFalse(result['usable'])
            self.assertIn('expected_explicit_requested_profile_metadata',result['failures'])
            self.assertEqual(result['requestedProfile'],expected)

    def test_offline_declared_profile_cannot_be_overridden(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory=Path(tmp)
            (directory/'run.json').write_text(json.dumps({'requestedProfile':'presentation-waits'}))
            with self.assertRaisesRegex(ValueError,'differs from recorded'):
                analyze.analyze_run(directory,expected_profile='combined-bounded')

    def test_profile_cli_is_explicit_enum_with_light_default(self):
        p=run.parser()
        common=['--wine','/wine','--prefix','/prefix','--guest','/guest',
                '--trace-controller','/trace','--output','/new','--module','/module']
        self.assertEqual(p.parse_args(common).profile,'presentation-waits')
        self.assertEqual(p.parse_args(common+['--profile','combined-bounded']).profile,'combined-bounded')

    def test_wrong_explicit_profile_cannot_admit_benchmark_probe(self):
        result=self._trace(marker=1)
        self.assertEqual(result['producerProfile']['name'],'compile-waits')
        self.assertTrue(result['producerProfile']['metadata_observed'])
        self.assertFalse(result['usable'])
        self.assertIn('expected_explicit_requested_profile_metadata',result['failures'])


class BoundedChildTests(unittest.TestCase):
    def test_command_actual_child_output_and_status(self):
        self.assertEqual(run.command([sys.executable,'-c','print("ok")']),b'ok\n')
        with self.assertRaisesRegex(ValueError,'exit 3'):
            run.command([sys.executable,'-c','raise SystemExit(3)'])

    def test_actual_child_output_cap_and_timeout(self):
        with self.assertRaisesRegex(ValueError,'output cap'):
            run.command([sys.executable,'-c','print("x"*10000)'],cap=100)
        before=time.monotonic()
        with self.assertRaisesRegex(ValueError,'timeout'):
            run.command([sys.executable,'-c','import time; time.sleep(5)'],timeout=.1)
        self.assertLess(time.monotonic()-before,3)


if __name__=='__main__':unittest.main()
