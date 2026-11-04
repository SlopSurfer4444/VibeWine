#!/usr/bin/env python3
"""Analyze immutable native-controller NDJSON. Never reads a live mapping."""
import argparse,json,math,statistics
from collections import defaultdict
from pathlib import Path
CAPACITY=131072
REASONS={0:'Unknown',1:'Present',2:'StagingMap',3:'GetDataNotReady',4:'ExplicitFlush',5:'GPUIdle',6:'FenceSignal',7:'FenceWait',8:'PassPromotion'}
SYNC_NAMES={30:'StagingMap CPU fence wait',31:'GetData not-ready Flush',32:'GPUIdle CPU fence wait'}
MAP_FLAGS={1:'READ',2:'WRITE',3:'READ_WRITE'}
QUERY_FLAGS={0:'EVENT',1:'OCCLUSION',2:'TIMESTAMP',3:'TIMESTAMP_DISJOINT',5:'OCCLUSION_PREDICATE'}

def uint(v):
    if type(v) is not int or not 0<=v<2**64: raise ValueError('expected uint64, not bool/float')
    return v

def uint32(v):
    uint(v)
    if v>=2**32:raise ValueError('expected native uint32 field')
    return v

def sync_analysis(events,chunkrows,rejected):
    """Observed CPU scopes and identity-only reason joins; never infer missing cost."""
    joined={(c['queue'],c['chunk']):c for c in chunkrows}
    for c in chunkrows:c['commit_reason']=None;c['commit_reason_code']=None
    reasons=defaultdict(list)
    for e in events:
        if e['type']==33:reasons[(e['object'],e['padding0'])].append(e)
    reasonrows=[];counts=defaultdict(int)
    for (queue,seq),group in sorted(reasons.items()):
        if not queue or not seq:rejected['invalid_commit_reason_identity']+=len(group);continue
        if len(group)!=1:rejected['duplicate_commit_reason']+=1;continue
        e=group[0]
        if e['flags'] not in REASONS:rejected['invalid_commit_reason_code']+=1;continue
        if e['start']!=e['end']:rejected['noninstant_commit_reason']+=1;continue
        c=joined.get((queue,seq));state='joined_observed_chunk'
        if c is None:state='missing_or_rejected_chunk';rejected['commit_reason_missing_or_rejected_chunk']+=1
        elif c['frame']!=e['padding1']:state='frame_disagreement';rejected['commit_reason_frame_disagreement']+=1;c=None
        if c is not None:
            c['commit_reason']=REASONS[e['flags']];c['commit_reason_code']=e['flags'];c['commit_reason_event_index']=e['index']
        counts[REASONS[e['flags']]]+=1
        reasonrows.append({'event_index':e['index'],'generation':e['generation'],'queue':queue,'chunk':seq,'frame':e['padding1'],'cpu_instant_ns':e['start'],
                           'reason_code':e['flags'],'reason':REASONS[e['flags']],'chunk_join_state':state,'duration_ms':None})
    scopes=[];groups=defaultdict(list)
    for e in events:
        t=e['type']
        if t not in SYNC_NAMES:continue
        if not e['object'] or not e['padding0']:rejected['invalid_sync_identity']+=1;continue
        if e['end']<e['start']:rejected['invalid_sync_interval']+=1;continue
        flags=MAP_FLAGS if t==30 else QUERY_FLAGS if t==31 else {0:'none'}
        if e['flags'] not in flags:rejected['invalid_sync_flags']+=1;continue
        row={'event_index':e['index'],'generation':e['generation'],'type':t,'event_name':SYNC_NAMES[t],'queue':e['padding0'],'call_frame':e['padding1'],
             'thread':e['thread'],'flags':e['flags'],'flags_name':flags[e['flags']],'start_cpu_ns':e['start'],'end_cpu_ns':e['end'],
             'observed_cpu_duration_ms':(e['end']-e['start'])/1e6,'awaited_chunk_if_observed':None}
        if t==31:
            row['async_pointer']=e['object'];row['chunk_correlation']='unavailable_async_pointer_is_not_sequence'
        else:
            row['awaited_sequence']=e['object'];c=joined.get((e['padding0'],e['object']))
            if c is None:row['chunk_correlation']='missing_or_rejected_chunk';rejected['sync_awaited_chunk_missing_or_rejected']+=1
            else:
                row['chunk_correlation']='exact_queue_and_sequence';row['awaited_chunk_if_observed']={'generation':e['generation'],'queue':c['queue'],'chunk':c['chunk'],'frame':c['frame'],'completion_status':c['completion_status'],'commit_reason':c['commit_reason']}
        scopes.append(row);groups[(t,e['padding0'],e['flags'])].append(e['end']-e['start'])
    aggregates=[{'type':t,'event_name':SYNC_NAMES[t],'queue':queue,'flags':flags,'observed_cpu_duration':stats(values),'summed_cpu_time':None}
                for (t,queue,flags),values in sorted(groups.items())]
    return {'cpu_scopes':scopes,'cpu_scope_duration_groups':aggregates,'commit_reason_records':reasonrows,'observed_commit_reason_counts':dict(counts),
            'limits':['SyncScope durations are CPU-side observed scopes, not GPU duration or GPU-idle residence.','GetData records an asynchronous-object pointer, never an awaited chunk sequence.','Wait call-frame can differ from awaited historical chunk frame; both are retained.','Commit reason is an identity-linked instant label; it supplies no cost or submission latency.','Missing or duplicate chunk/reason identities prevent attribution; boundary data is never filled with zero.','CPU scope durations can overlap; no summed CPU cost or per-frame cost is inferred.']}

def stats(values):
    if not values:return None
    values=sorted(values)
    def p(q):return values[max(0,math.ceil(len(values)*q)-1)]/1e6
    return {'samples':len(values),'median_ms':statistics.median(values)/1e6,'p95_ms':p(.95),'p99_ms':p(.99),'max_ms':values[-1]/1e6,
            'over_25ms':sum(x>25_000_000 for x in values),'over_33_333ms':sum(x>33_333_333 for x in values),'over_50ms':sum(x>50_000_000 for x in values),'over_100ms':sum(x>100_000_000 for x in values)}

def analyze(rows):
    if not rows or rows[-1].get('kind')!='summary' or sum(r.get('kind')=='summary' for r in rows)!=1:raise ValueError('one final decoder summary required')
    summary=rows[-1];gen=uint32(summary['generation']);events=rows[:-1]
    if not gen or len(events)>CAPACITY:raise ValueError('invalid generation or event count')
    if uint(summary['events'])!=len(events):raise ValueError('summary/event count mismatch')
    seen=set()
    for e in events:
        if e.get('kind')!='event':raise ValueError('unexpected record kind')
        for k in ['index','generation','type','thread','flags','object','start','end','padding0','padding1']:uint(e[k])
        for k in ['generation','type','thread','flags']:uint32(e[k])
        if e['generation']!=gen or e['index']>=CAPACITY or e['index'] in seen:raise ValueError('generation/index mismatch or duplicate')
        seen.add(e['index'])
        expected='native_gpu_ns' if e['type']==24 else 'cpu_steady_ns' if e['type'] in set(range(1,17))|set(range(20,24))|{25,30,31,32,33,40,41,42,43,44,45,46,47,48,49} else 'unknown'
        if e.get('clock_domain')!=expected:raise ValueError('clock domain mismatch')
    rejected=defaultdict(int); cadence=[]; presentations=defaultdict(list);intervals=defaultdict(list)
    for e in events:
        key=(e['object'],e['thread'])
        if e['type']==2:presentations[key].append(e)
        if e['type']==1:intervals[key].append(e)
    for key,ints in sorted(intervals.items()):
        if not all(key):rejected['unidentified_present_stream']+=1;continue
        complete=defaultdict(list)
        for e in presentations[key]:
            if e['end']>=e['start']:complete[e['start']].append(e)
        accepted=[];pairs=set();invalid=False
        for e in sorted(ints,key=lambda e:(e['start'],e['end'])):
            pair=(e['start'],e['end'])
            if pair in pairs:invalid=True;rejected['duplicate_cadence_pair']+=1;continue
            pairs.add(pair)
            if e['end']<=e['start']:rejected['nonpositive_cadence']+=1;continue
            if len(complete[e['start']])!=1 or len(complete[e['end']])!=1:rejected['cadence_missing_or_duplicate_completed_present']+=1;continue
            if complete[e['start']][0]['end']>e['end']:
                invalid=True;rejected['overlapping_present_calls_same_thread']+=1;continue
            if accepted and e['start']<accepted[-1]['end']:invalid=True;rejected['overlapping_cadence']+=1;continue
            accepted.append(e)
        if invalid:
            rejected['cadence_stream_rejected']+=1;continue
        if not accepted:continue
        gaps=sum(a['end']!=b['start']for a,b in zip(accepted,accepted[1:]))
        cadence.append({'swapchain':key[0],'thread':key[1],'metric':'CPU legacy Present body-start cadence, not displayed FPS','segments':gaps+1,'gaps_between_observed_intervals':gaps,
                        'first_cpu_ns':accepted[0]['start'],'last_cpu_ns':accepted[-1]['end'],'intervals':stats([e['end']-e['start']for e in accepted]),'present_cpu_duration':stats([v[0]['end']-v[0]['start'] for t,v in complete.items() if len(v)==1 and accepted[0]['start']<=t<=accepted[-1]['end']])})
    chunks=defaultdict(lambda:defaultdict(list));framejoins=defaultdict(list)
    for e in events:
        if 20<=e['type']<=24:chunks[(e['object'],e['padding0'])][e['type']].append(e)
        if e['type']==25:
            if not e['object'] or not e['padding0'] or e['start']!=e['end']:rejected['invalid_present_frame_identity']+=1;continue
            framejoins[(e['padding0'],e['padding1'])].append(e)
    queuewait=[];encodecommit=[];completion=[];gpu=[];chunkrows=[]
    for (queue,seq),bytype in sorted(chunks.items()):
        if not queue or not seq:rejected['invalid_chunk_identity']+=1;continue
        if any(len(v)!=1 for v in bytype.values()):rejected['duplicate_chunk_type']+=1;continue
        if any(t not in bytype for t in range(20,24)):rejected['incomplete_chunk']+=1;continue
        ordered=[bytype[t][0]for t in range(20,24)];frame=ordered[0]['padding1']
        if any(e['padding1']!=frame for v in bytype.values()for e in v):rejected['chunk_frame_disagreement']+=1;continue
        times=[e['start']for e in ordered]
        if any(e['start']!=e['end']for e in ordered)or times!=sorted(times):rejected['nonmonotonic_cpu_chunk']+=1;continue
        q,a,c=[times[i+1]-times[i]for i in range(3)];status=ordered[3]['flags']
        if status==4:queuewait.append(q);encodecommit.append(a);completion.append(c)
        else:rejected['nonsuccessful_chunk_excluded_from_timing_aggregates']+=1
        owners=framejoins.get((queue,frame),[]);attribution=owners[0]['object'] if len(owners)==1 else None
        if len(owners)>1:rejected['ambiguous_frame_to_swapchain']+=1
        gd=None
        if 24 in bytype:
            g=bytype[24][0]
            if status==4 and g['flags']==status and g['start']>0 and g['end']>=g['start']:
                gd=g['end']-g['start'];gpu.append(gd)
            else:rejected['invalid_gpu_interval']+=1
        chunkrows.append({'queue':queue,'chunk':seq,'frame':frame,'completion_status':status,'included_in_successful_aggregates':status==4,'swapchain_if_unambiguous':attribution,'publish_to_encode_ms':q/1e6,'encode_setup_commit_ms':a/1e6,'commit_to_cpu_completion_observation_ms':c/1e6,'gpu_duration_ms':None if gd is None else gd/1e6})
    sync=sync_analysis(events,chunkrows,rejected)
    quality=[]
    if summary.get('inconclusive_window') is not False:quality.append('decoder_snapshot_inconclusive')
    for k in ['dropped_before','dropped_after','uncommitted_reserved']:
        if uint(summary.get(k,0)):quality.append(k)
    if summary.get('counters_changed'):quality.append('counters_changed')
    if summary.get('producer_quiescence_verified') is not True:quality.append('producer_quiescence_unverified_boundary_events_may_be_missing')
    if summary.get('live_pid_verified') is not True:quality.append('live_pid_unverified')
    if summary.get('bridge_verified') is not True:quality.append('bridge_unverified')
    api_names={40:'Present1 full API',41:'GetData full API',42:'Map full API',43:'Flush1 full API',44:'ResizeBuffers full API',45:'ResizeTarget full API',46:'SetFullscreenState full API',47:'CommandQueue shutdown body',48:'Swapchain teardown body',49:'Native view release'}
    api_groups=defaultdict(list)
    for e in events:
        if e['type'] in api_names:
            if e['end']<e['start']:rejected['invalid_api_interval']+=1;continue
            api_groups[(e['type'],e['object'],e['flags'],e['padding0'],e['padding1'])].append(e['end']-e['start'])
    api=[{'type':t,'name':api_names[t],'birth_serial':obj,'result_or_flush_submission':result,'argument0':arg0,'argument1':arg1,'observed_calls':len(v),'duration':stats(v),'sum_observed_duration_ns':sum(v)} for (t,obj,result,arg0,arg1),v in sorted(api_groups.items())]
    if summary.get('structurally_complete') is not True:quality.append('writer_quiescence_unproven')
    quality.append('scopes_or_gpu_chunks_crossing_stop_are_not_complete')
    return {'schema':2,'generation':gen,'decoder_identity':summary.get('identity'),'scope':'observed immutable snapshot only; no displayed FPS or FPS gain inference',
      'quality_limits':quality,'rejections':dict(rejected),'cpu_present_streams':cadence,'queue_chunk_metrics':{'joined_chunks':len(chunkrows),'successful_completed_chunks':sum(c['completion_status']==4 for c in chunkrows),'publish_to_encode':stats(queuewait),'encode_setup_commit':stats(encodecommit),'commit_to_cpu_completion_observation':stats(completion),'gpu_command_buffer_durations':stats(gpu)},'chunks':chunkrows,
      'full_api_boundaries':api,'sync_probes':sync,'interpretation':['GPU duration uses native GPU end-start only; never subtract CPU and GPU epochs.','GPU durations may overlap and must not be summed as GPU busy time.','No absent event is interpreted as zero cost; old scopes below 100 us are censored.','V2 queue identity uses birth serials; legacy Present body identity remains a pointer. Duplicate chunk keys are rejected.','Missing boundary chunks and frame ownership prevent complete-frame GPU attribution.']}

def main():
    p=argparse.ArgumentParser();p.add_argument('input',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    rows=[]
    with a.input.open() as f:
        for line in f:
            if len(line)>8192 or len(rows)>CAPACITY:raise ValueError('input exceeds bounded decoder format')
            rows.append(json.loads(line))
    result=analyze(rows);a.output.write_text(json.dumps(result,indent=2)+'\n')
if __name__=='__main__':main()
