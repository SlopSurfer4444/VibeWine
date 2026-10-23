"""Stationary native-presentation windows; no producer CPU clock conversion.

Clock envelopes are execution receipts bound by the caller, not self-attestation:
the caller supplies the actual executable hash and measurement identity. This
module validates that binding and reuses the maintained strict feedback parser.
"""
import math
import re
import uuid
from analyze import CAPACITY,analyze,stats,uint,uint32

def _number(value,name,upper=1e9):
    if type(value) not in (int,float) or not 0<value<upper or not math.isfinite(value):
        raise ValueError('invalid '+name)
    return value

def _uuid(value,name):
    if type(value) is not str or len(value)!=36:raise ValueError('invalid '+name)
    try:parsed=uuid.UUID(value)
    except (ValueError,AttributeError):raise ValueError('invalid '+name) from None
    if str(parsed)!=value.lower() or not parsed.int:raise ValueError('invalid '+name)
    return str(parsed)

def validate_clock_sample(envelope,*,expected_collector_sha256,measurement_id,max_sample_uncertainty_ns=1_000_000):
    if type(expected_collector_sha256) is not str or not re.fullmatch('[0-9a-f]{64}',expected_collector_sha256):raise ValueError('expected actual collector SHA256')
    measurement_id=_uuid(measurement_id,'measurement identity')
    if type(max_sample_uncertainty_ns) is not int or not 0<max_sample_uncertainty_ns<=100_000_000:raise ValueError('invalid clock uncertainty limit')
    if type(envelope) is not dict or envelope.get('collector_sha256')!=expected_collector_sha256 or _uuid(envelope.get('measurement_id'),'sample measurement identity')!=measurement_id:raise ValueError('clock execution binding mismatch')
    sample=envelope.get('clock_sample')
    if type(sample) is not dict:raise ValueError('clock sample object required')
    required={'kind':'clock_sample','clock_domain':'ca_host_seconds','clock_api':'CACurrentMediaTime','monotonic_clock_domain':'native_mach_absolute_ns','monotonic_clock_api':'mach_absolute_time'}
    if any(sample.get(k)!=v for k,v in required.items()) or type(sample.get('schema')) is not int or sample['schema']!=1:raise ValueError('unknown clock sample schema/domain/API')
    identity=_uuid(sample.get('sample_id'),'sample identity')
    if not uint32(sample.get('pid')):raise ValueError('invalid native collector PID')
    lower=_number(sample.get('ca_lower_seconds'),'CA lower bound');upper=_number(sample.get('ca_upper_seconds'),'CA upper bound')
    if upper<lower:raise ValueError('regressed CA clock bracket')
    mono=uint(sample.get('native_monotonic_ns'));width=uint(sample.get('bracket_ns'))
    resolution=uint(sample.get('native_monotonic_resolution_ns'))
    ticks=uint(sample.get('mach_ticks'));numer=uint32(sample.get('timebase_numer'));denom=uint32(sample.get('timebase_denom'))
    if not ticks or not numer or not denom or mono!=ticks*numer//denom or resolution!=(numer+denom-1)//denom:raise ValueError('invalid Mach tick/timebase conversion')
    if not 0<resolution<=1_000_000:raise ValueError('invalid native monotonic resolution')
    if not mono:raise ValueError('invalid native monotonic sample')
    expected_width=math.ceil((upper-lower)*1e9)
    if width!=expected_width or width>max_sample_uncertainty_ns:raise ValueError('clock bracket invalid or exceeds uncertainty limit')
    # The converted tick read must itself be inside the CA bracket, allowing
    # integer nanosecond rounding and floating-point precision only.
    tolerance=2*math.ulp(upper)+resolution/1e9
    if not lower-tolerance<=mono/1e9<=upper+tolerance:raise ValueError('Mach sample outside CA bracket')
    return dict(sample_id=identity,lower=lower,upper=upper,mono=mono,width_ns=width,resolution_ns=resolution,timebase=(numer,denom))

def analyze_fixed_window(decoded_rows,begin_sample,end_sample,*,window_seconds,
                         expected_swapchain,expected_dimensions,expected_revision,
                         expected_collector_sha256,measurement_id,scenario='stationary',
                         max_sample_uncertainty_ns=1_000_000,max_end_sample_lateness_seconds=2.0):
    if type(decoded_rows) is not list or not decoded_rows or len(decoded_rows)>CAPACITY+1 or any(type(r) is not dict for r in decoded_rows):raise ValueError('bounded decoded record list required')
    duration=_number(window_seconds,'window duration',3600.000000001)
    if not uint(expected_swapchain):raise ValueError('positive swapchain birth identity required')
    if type(expected_dimensions) is not dict or set(expected_dimensions)!={'width','height'} or any(not uint32(v) for v in expected_dimensions.values()):raise ValueError('positive drawable dimensions required')
    uint(expected_revision)
    if type(scenario) is not str:raise ValueError('scenario required')
    lateness=_number(max_end_sample_lateness_seconds,'end sample lateness limit',5.000000001)
    kw=dict(expected_collector_sha256=expected_collector_sha256,measurement_id=measurement_id,max_sample_uncertainty_ns=max_sample_uncertainty_ns)
    begin=validate_clock_sample(begin_sample,**kw);end=validate_clock_sample(end_sample,**kw)
    if begin['sample_id']==end['sample_id']:raise ValueError('distinct beginning/end samples required')
    if begin['timebase']!=end['timebase']:raise ValueError('Mach timebase changed across samples')
    if end['mono']<=begin['mono'] or end['lower']<=begin['upper']:raise ValueError('clock samples regress or overlap')
    elapsed=(end['mono']-begin['mono'])/1e9
    tolerance=4*max(math.ulp(begin['upper']),math.ulp(end['upper']))+(begin['resolution_ns']+end['resolution_ns'])/1e9+2e-9
    if not end['lower']-begin['upper']-tolerance<=elapsed<=end['upper']-begin['lower']+tolerance:raise ValueError('native clock elapsed disagreement')
    start=begin['upper'];stop=start+duration
    if not math.isfinite(stop) or stop>=1e9 or stop<=start:raise ValueError('invalid fixed CA window')
    parsed=analyze(decoded_rows);wave=parsed['wave7'];summary=decoded_rows[-1]
    identity=summary.get('identity')
    if type(identity) is not str or not re.fullmatch('[0-9]+:[0-9]+',identity):raise ValueError('decoder file identity required')
    device,inode=map(int,identity.split(':'));uint(device)
    if not uint(inode):raise ValueError('invalid decoder inode identity')
    reasons=[]
    def reject(reason):
        if reason not in reasons:reasons.append(reason)
    if scenario!='stationary':reject('route_alignment_unproven_only_stationary_admitted')
    if end['lower']<stop:reject('end_clock_sample_does_not_reach_fixed_end')
    if end['upper']>stop+lateness:reject('end_clock_sample_too_late')
    for k in ('dropped_before','dropped_after','uncommitted_reserved','boundary_rejected_total'):
        if uint(summary.get(k)):reject('capture_'+k)
    for k,v in (('structurally_complete',True),('producer_quiescence_verified',True),('inconclusive_window',False),('counters_changed',False)):
        if summary.get(k) is not v:reject('capture_'+k+'_unverified')
    if not wave['strict_acceptance']['capture_identity_quality_ok']:reject('unidentified_native_feedback')
    if parsed['detailed_probes']['source_failures']:reject('detailed_capture_source_failure')
    controls=[e for e in decoded_rows[:-1] if e['type'] in (70,74,75,76,77,78) and e['object'] in (0,expected_swapchain)]
    for e in controls:
        if not e['object'] or e['start']!=e['end'] or e['flags']>(6 if e['type']==74 else 4 if e['type']==77 else 7):reject('malformed_selected_native_control_record')
        if e['type'] in (74,78) and not e['padding0']:reject('missing_selected_native_control_session')
        if e['type']==78 and (e['flags'] or e['padding1']):reject('native_close_feedback_discard_or_unknown')
    capabilities=[e for e in controls if e['type']==70]
    if len(capabilities)!=1 or any(e['flags'] or e['padding0']!=1 or not e['padding1']&1 for e in capabilities):reject('native_presented_capability_unproven')
    frames=[r for r in wave['native_presented_frames'] if r['swapchain']==expected_swapchain]
    # Never form an accepted subset after dropping duplicate/invalid companions.
    if any(not r['strict_accepted'] for r in frames):reject('invalid_or_duplicate_selected_feedback')
    valid=[r for r in frames if r['strict_accepted']]
    if any(b['native_presented_seconds']<=a['native_presented_seconds'] for a,b in zip(valid,valid[1:])):reject('nonmonotonic_selected_feedback')
    before=[i for i,r in enumerate(valid) if r['native_presented_seconds']<=start]
    after=[i for i,r in enumerate(valid) if r['native_presented_seconds']>=stop]
    context=[]
    if not before or not after:reject('feedback_does_not_span_both_window_boundaries')
    else:
        left=before[-1];right=after[0]
        if left>=right:reject('invalid_window_boundary_order')
        else:context=valid[left:right+1]
    if any(b['frame']!=a['frame']+1 for a,b in zip(context,context[1:])):reject('missing_frame_identity_within_window_span')
    if any(r['configuration_revision']!=expected_revision or r['actual_drawable_dimensions']!=expected_dimensions for r in context):reject('window_configuration_disagreement')
    counters=[r for r in wave['native_counter_samples'] if r['swapchain']==expected_swapchain]
    if len(counters)<2 or any(not r['complete'] for r in counters) or len({r['session'] for r in counters})!=1:reject('native_loss_counters_missing_ambiguous_or_incomplete')
    if context and counters:
        # Both are PE receipt timestamps. Compare within this one domain only;
        # there is no conversion/subtraction against the CA presentation epoch.
        receipts=[r['receipt_cpu_ns'] for r in context]
        if min(r['receipt_cpu_ns'] for r in counters)>min(receipts) or max(r['receipt_cpu_ns'] for r in counters)<max(receipts):reject('native_loss_counters_do_not_bracket_feedback_receipts')
        terminal=max(counters,key=lambda r:r['receipt_cpu_ns'])
        if terminal['complete'] and terminal['values']['completed']<len(context):reject('native_completed_count_cannot_cover_feedback_span')
    if any(any(r['values'].get(k,0) for k in ('overflow','rejected','abandoned')) for r in counters):reject('native_overflow_rejection_or_abandonment')
    for r in counters:
        v=r['values']
        if r['complete'] and (v['completed']+v['abandoned']>v['accepted'] or v['pending_live_handler_tickets']>v['accepted'] or v['queued']>min(64,v['completed'])):reject('native_counter_accounting_inconsistent')
    if any(any(a['values'].get(k,0)>b['values'].get(k,0) for k in ('accepted','completed','abandoned','overflow','rejected')) for a,b in zip(counters,counters[1:])):reject('native_counter_regression')
    if any(e['type']==74 and not e['object'] for e in decoded_rows[:-1]):reject('unidentified_native_counter')
    raw_counters=[e for e in decoded_rows[:-1] if e['type']==74 and e['object']==expected_swapchain]
    if len(raw_counters)!=7*len(counters):reject('malformed_selected_native_counter')
    if any(r['swapchain']==expected_swapchain and r['status'] for key in ('native_feedback_failures','native_configuration_apply_failures','native_session_closes') for r in wave[key]):reject('native_feedback_or_configuration_failure')
    inside=[r for r in context if start<=r['native_presented_seconds']<stop]
    intervals=[(b['native_presented_seconds']-a['native_presented_seconds'])*1e9 for a,b in zip(inside,inside[1:])]
    accepted=not reasons
    return dict(schema=1,accepted=accepted,rejection_reasons=reasons,scenario=scenario,measurement_id=measurement_id,
                collector_sha256=expected_collector_sha256,generation=parsed['generation'],decoder_identity=parsed['decoder_identity'],
                window=dict(clock_domain='ca_host_seconds',start_seconds=start,end_seconds=stop,duration_seconds=duration,begin_uncertainty_ns=begin['width_ns'],end_uncertainty_ns=end['width_ns'],sample_ids=[begin['sample_id'],end['sample_id']]),
                expected=dict(swapchain=expected_swapchain,dimensions=expected_dimensions,revision=expected_revision),
                observed_window_frame_ids=[r['frame'] for r in inside],spanning_frame_ids=[r['frame'] for r in context],
                presentation_count=len(inside) if accepted else None,presentations_per_fixed_second=len(inside)/duration if accepted else None,
                interior_interval_distribution=stats(intervals) if accepted else None,
                interior_interval_rate_hz=len(intervals)/(sum(intervals)/1e9) if accepted and intervals else None,
                excluded_boundary_crossing_intervals=(len(context)-1-len(intervals)) if context else None,
                exclusions=dict(other_swapchain_frames=sum(r['swapchain']!=expected_swapchain for r in wave['native_presented_frames']),selected_frames_outside_boundary_span=len(valid)-len(context),historical_generation_records=uint(summary.get('wrong_generation',0))),
                limits=['Stationary scene claim is supplied by the caller; moving-route alignment is not admitted.',
                        'The exact window uses independently sampled CoreAnimation time, not Python route edges or producer Windows CPU time.',
                        'Counts are actual drawable presentation feedback, not physical scanout, full display/frame latency, or an FPS improvement.',
                        'Only intervals wholly inside the half-open fixed window enter its distribution. Boundary-spanning consecutive identities establish coverage.',
                        'Clock samples require caller-bound actual collector hash/measurement receipts. A JSON status flag cannot establish execution.',
                        'This instrumented observer does not provide disabled-instrumentation observation or establish observer overhead.'])

def main():
    import argparse,json
    from pathlib import Path
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture',type=Path);parser.add_argument('begin',type=Path);parser.add_argument('end',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--window-seconds',type=float,required=True)
    parser.add_argument('--swapchain',type=int,required=True)
    parser.add_argument('--width',type=int,required=True);parser.add_argument('--height',type=int,required=True)
    parser.add_argument('--revision',type=int,required=True)
    parser.add_argument('--collector-sha256',required=True);parser.add_argument('--measurement-id',required=True)
    parser.add_argument('--scenario',default='stationary')
    parser.add_argument('--max-sample-uncertainty-ns',type=int,default=1_000_000)
    parser.add_argument('--max-end-sample-lateness-seconds',type=float,default=2.0)
    args=parser.parse_args()
    try:
        envelopes=[]
        for path in (args.begin,args.end):
            with path.open('rb') as handle:
                raw=handle.read(8193)
            if len(raw)>8192:raise ValueError('clock envelope exceeds 8 KiB')
            envelopes.append(json.loads(raw))
        decoded=[];total=0
        with args.capture.open('rb') as handle:
            while True:
                line=handle.readline(8193)
                if not line:break
                total+=len(line)
                if total>64*1024*1024 or len(line)>8192 or len(decoded)>CAPACITY:raise ValueError('decoded capture exceeds bounded input format')
                decoded.append(json.loads(line))
        result=analyze_fixed_window(decoded,*envelopes,window_seconds=args.window_seconds,expected_swapchain=args.swapchain,
            expected_dimensions={'width':args.width,'height':args.height},expected_revision=args.revision,
            expected_collector_sha256=args.collector_sha256,measurement_id=args.measurement_id,scenario=args.scenario,
            max_sample_uncertainty_ns=args.max_sample_uncertainty_ns,max_end_sample_lateness_seconds=args.max_end_sample_lateness_seconds)
        for path in (args.capture,args.begin,args.end):
            if args.output.resolve()==path.resolve() or (args.output.exists() and args.output.samefile(path)):raise ValueError('output would overwrite an immutable input')
        args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
        return 0 if result['accepted'] else 3
    except (ValueError,KeyError,TypeError,OSError) as error:
        parser.error(str(error))

if __name__=='__main__':
    raise SystemExit(main())
