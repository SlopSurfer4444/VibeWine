"""Wave7 additive v2 analysis. All cross-event joins are identities, not clocks."""
import math
import struct
from collections import defaultdict

COUNTERS=('accepted','completed','abandoned','overflow','rejected','pending_live_handler_tickets','queued')
STATUS={0:'OK',1:'BAD_REQUEST',2:'UNSUPPORTED',3:'NO_SESSION',4:'LIMIT',5:'EMPTY',6:'STALE',7:'PENDING'}

def dimensions(value):
    return {'width':value>>32,'height':value&0xffffffff}

def host_time(e):
    value=struct.unpack('<d',struct.pack('<Q',e['padding1']))[0]
    finite_positive=math.isfinite(value) and value>0
    domain=e['flags']&0xff
    valid=(e['flags']&~0x1ff)==0 and domain==1 and bool(e['flags']&0x100)==finite_positive and finite_positive
    return value,valid,domain,finite_positive

def wave7_analysis(events,summary,rejected,stats):
    frames=defaultdict(lambda:defaultdict(list));countergroups=defaultdict(lambda:defaultdict(list))
    capabilities=[];closes=[];failures=[];snapshots=[];apis=[];apply_failures=[];close_accounting=[]
    for e in events:
        t=e['type']
        if t in (44,45) and e['object'] and e['end']>=e['start']:
            apis.append({'event_index':e['index'],'swapchain':e['object'],'thread':e['thread'],'api':t,'start_cpu_ns':e['start'],'end_cpu_ns':e['end'],'hresult':e['flags'],'requested_dimensions':dimensions(e['padding0']),'argument1':e['padding1'],'snapshot_event_indices':[]})
        if t not in (61,62,70,71,72,73,74,75,76,77,78):continue
        if not e['object'] or e['start']!=e['end']:
            rejected['invalid_wave7_identity_or_noninstant']+=1;continue
        base={'event_index':e['index'],'swapchain':e['object'],'receipt_cpu_ns':e['start']}
        if t in (71,72,73):
            if not e['padding0'] or (t in (72,73) and e['flags']):
                rejected['invalid_feedback_frame_identity_or_flags']+=1;continue
            frames[(e['object'],e['padding0'])][t].append(e)
        elif t==70:
            capabilities.append(dict(base,status=e['flags'],protocol_version=e['padding0'],feature_bits=e['padding1']))
        elif t==74:
            if not e['padding0'] or e['flags']>=len(COUNTERS):
                rejected['invalid_native_counter_identity_or_index']+=1;continue
            countergroups[(e['object'],e['padding0'],e['start'])][e['flags']].append(e)
        elif t==75:
            closes.append(dict(base,status=e['flags'],status_name=STATUS.get(e['flags'],'unknown'),live_handler_tickets_at_close=e['padding0'],abandoned_handler_count=e['padding1']))
        elif t==76:
            failures.append(dict(base,status=e['flags'],status_name=STATUS.get(e['flags'],'unknown'),frame=e['padding0'],configuration_revision=e['padding1'],operation='open' if e['padding1']==0 else 'register'))
        elif t==77:
            apply_failures.append(dict(base,status=e['flags'],status_name={0:'OK',1:'BAD_REQUEST',2:'UNSUPPORTED',3:'REJECTED',4:'FAILED'}.get(e['flags'],'unknown'),configuration_revision=e['padding0'],reserved_detail=e['padding1']))
        elif t==78:
            if not e['padding0']:
                rejected['invalid_feedback_close_accounting_session']+=1;continue
            close_accounting.append(dict(base,status=e['flags'],status_name=STATUS.get(e['flags'],'unknown'),session=e['padding0'],queued_discarded=e['padding1'] if e['flags']==0 else None,raw_queued_payload=e['padding1'],accounting_verified=e['flags']==0))
        else:
            api=e['flags']&0xffff;allowed=0x1ffff if t==61 else 0x3ffff
            if api not in (44,45) or e['flags']&~allowed:
                rejected['invalid_resize_snapshot_flags']+=1;continue
            row=dict(base,type=t,thread=e['thread'],api=api,phase='after' if e['flags']&0x10000 else 'before',api_join_state='missing_or_ambiguous_api',api_event_index=None)
            if t==61:row.update(committed_descriptor=dimensions(e['padding0']),allocated_texture=dimensions(e['padding1']))
            else:row.update(hwnd_owner_thread=e['padding0']>>32,caller_thread=e['padding0']&0xffffffff,client_dimensions=dimensions(e['padding1']),client_query_valid=not bool(e['flags']&0x20000))
            snapshots.append(row)

    # ApiScope lifetime encloses both snapshot phases. Nested intervals are ambiguous.
    for s in snapshots:
        candidates=[a for a in apis if a['api']==s['api'] and a['swapchain']==s['swapchain'] and a['thread']==s['thread'] and a['start_cpu_ns']<=s['receipt_cpu_ns']<=a['end_cpu_ns']]
        if len(candidates)==1:
            a=candidates[0];s.update(api_join_state='unique_same_thread_enclosing_api',api_event_index=a['event_index']);a['snapshot_event_indices'].append(s['event_index'])
        else:rejected['resize_snapshot_missing_or_ambiguous_api']+=1
    for a in apis:
        group=[s for s in snapshots if s['api_event_index']==a['event_index']]
        a['snapshot_completeness']='four_unique_samples' if len(group)==4 and len({(s['type'],s['phase']) for s in group})==4 else 'missing_or_duplicate_samples'
        # Samples are not atomic, and no configuration revision exists in resize events.

    frame_rows=[];display=defaultdict(list)
    for (swapchain,frame),group in sorted(frames.items()):
        duplicate=any(len(v)>1 for v in group.values())
        row={'swapchain':swapchain,'frame':frame,'event_indices':{str(t):[e['index'] for e in v] for t,v in group.items()},'join_state':'duplicate_identity' if duplicate else 'complete' if len(group)==3 else 'incomplete_capture_or_boundary','configuration_revision':None,'actual_drawable_dimensions':None,'native_presented_seconds':None,'presented_time_valid':False}
        if duplicate:rejected['duplicate_native_feedback_frame_type']+=1
        else:
            instants={v[0]['start'] for v in group.values()}
            if len(instants)>1:
                row['join_state']='receipt_disagreement';rejected['native_feedback_receipt_disagreement']+=1
            if 72 in group:row['configuration_revision']=group[72][0]['padding1']
            if 73 in group:row['actual_drawable_dimensions']=dimensions(group[73][0]['padding1'])
            if 71 in group:
                e=group[71][0];value,valid,domain,positive=host_time(e)
                row.update(receipt_cpu_ns=e['start'],native_time_bits=e['padding1'],native_clock_domain=domain,native_presented_seconds=value if math.isfinite(value) else None,finite_positive_native_time=positive,presented_validity_flag=bool(e['flags']&0x100),presented_time_valid=valid)
                if not valid:rejected['invalid_native_presented_timestamp_or_domain_flags']+=1
                elif row['join_state']!='receipt_disagreement':display[swapchain].append(row)
        frame_rows.append(row)

    cadence=[]
    for swapchain,rows in sorted(display.items()):
        # Identity order handles callback/PE receipt reordering. Never sort timestamps to hide reversals.
        bad=any(b['native_presented_seconds']<=a['native_presented_seconds'] for a,b in zip(rows,rows[1:]))
        gaps=sum(b['frame']!=a['frame']+1 for a,b in zip(rows,rows[1:]))
        intervals=[(b['native_presented_seconds']-a['native_presented_seconds'])*1e9 for a,b in zip(rows,rows[1:]) if b['frame']==a['frame']+1] if not bad else []
        if bad:rejected['nonmonotonic_native_presentation_stream']+=1
        cadence.append({'swapchain':swapchain,'metric':'observed actual drawable presented-time cadence in CoreAnimation host seconds','valid_timestamp_frames':len(rows),'stream_state':'nonmonotonic_rejected' if bad else 'observed','missing_identity_gaps':gaps,'intervals':stats(intervals),'observed_interval_rate_hz':len(intervals)/(sum(intervals)/1e9) if intervals else None,'display_latency_ms':None})

    counters=[];last={};regressed=set()
    for (swapchain,session,t),group in sorted(countergroups.items()):
        complete=len(group)==7 and all(len(v)==1 for v in group.values())
        values={COUNTERS[i]:v[0]['padding1'] for i,v in group.items() if len(v)==1}
        row={'swapchain':swapchain,'session':session,'receipt_cpu_ns':t,'complete':complete,'values':values,'cumulative_deltas':None}
        prior=last.get((swapchain,session))
        if complete:
            if prior and any(values[k]<prior[k] for k in COUNTERS[:5]):
                rejected['native_cumulative_counter_regression']+=1;regressed.add((swapchain,session))
            if prior and (swapchain,session) not in regressed:row['cumulative_deltas']={k:values[k]-prior[k] for k in COUNTERS[:5]}
            last[(swapchain,session)]=values
        else:rejected['incomplete_or_duplicate_native_counter_sample']+=1
        counters.append(row)
    loss={k:summary.get(k,0) for k in ('dropped_before','dropped_after','uncommitted_reserved','boundary_rejected_total','wrong_generation')}
    loss.update({k:summary.get(k) for k in ('structurally_complete','producer_quiescence_verified','inconclusive_window','counters_changed')})
    return {'native_presented_frames':frame_rows,'native_presented_cadence':cadence,'capabilities':capabilities,'native_counter_samples':counters,'native_session_closes':closes,'native_close_queued_discard_accounting':close_accounting,'native_configuration_apply_failures':apply_failures,'native_feedback_failures':failures,'resize_api_chronology':sorted(apis,key=lambda x:(x['start_cpu_ns'],x['event_index'])),'resize_snapshots':sorted(snapshots,key=lambda x:(x['receipt_cpu_ns'],x['event_index'])),'pe_capture_loss_evidence':loss,'limits':['Native feedback is drained on later Present or teardown; the last or single frame can remain outside capture. V2 writer quiescence is not native feedback finalization.', 'Event 78 reports negotiated queued record discards at CLOSE, not GPU drops, uncalled callbacks, or final pending-ticket totals; unsupported accounting stays unknown.', 'PE receipt timestamps and CoreAnimation host timestamps are separate domains; display latency is unavailable.','Actual presented cadence uses only valid native timestamps and consecutive observed frame identities; CPU Present cadence is separate.','A missing feedback record or resize sample is incomplete observation, never proof of a GPU drop.','Native overflow and rejection counters are separate from PE capture loss and callback observations.','Native pending and close live-ticket counts describe handler lifetime, including fired callbacks still alive; never missing-frame counts.','Resize snapshots are sampled instants; interval joins are diagnostic and do not assert atomic state or configuration revision equivalence.','Native frame geometry and revision join only by generation/swapchain/frame; resize has no revision identifier.']}
