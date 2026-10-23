"""W1 identity joins: CPU steady durations only, no displayed-FPS inference."""
from collections import defaultdict

def sqlite_status(code,operation):
    if code==0xffffffff:return dict(code=code,classification='native_result_unavailable',operation_result='unknown')
    if code==0xfffffffe:return dict(code=code,classification='cpp_exception',operation_result='failed')
    primary=code&255
    name={0:'SQLITE_OK',5:'SQLITE_BUSY',6:'SQLITE_LOCKED',11:'SQLITE_CORRUPT',26:'SQLITE_NOTADB',100:'SQLITE_ROW',101:'SQLITE_DONE'}.get(primary,'SQLITE_OTHER')
    # Extended errors retain primary classification; fabricated high bits may
    # never turn into exact success (OK/ROW/DONE are not extended success codes).
    result=('initialized' if code==0 else 'failed') if operation=='init' else ('hit' if code==100 else 'miss' if code==101 else 'failed') if operation=='get' else ('write_completed' if code==101 else 'failed')
    return dict(code=code,primary_code=primary,classification=name,operation_result=result)

def detail_analysis(events, summary, chunks, rejected):
    tasks=defaultdict(lambda:defaultdict(list)); finishes=defaultdict(lambda:defaultdict(list)); failures=[];cachegroups=defaultdict(list)
    invalid=0
    def bad(reason):
        nonlocal invalid
        rejected[reason]+=1;invalid+=1
    for e in events:
        t=e['type']
        if t==89:
            failures.append(dict(event_index=e['index'],reason={1:'detail_budget_exhausted',2:'stale_generation_work'}.get(e['flags'],'invalid_failure_marker'),argument=e['padding0']))
            if e['flags'] not in (1,2) or e['object'] or e['start']!=e['end'] or e['padding1']:bad('invalid_detail_failure_marker')
        elif 90<=t<=92:finishes[(e['object'],e['padding0'])][t].append(e)
        elif t in (93,94):cachegroups[(t,e['object'],e['padding0'])].append(e)
        elif 80<=t<=88:tasks[e['object']][t].append(e)
    taskrows=[];links=[]
    for serial,group in sorted(tasks.items()):
        errors=[];es=[e for g in group.values() for e in g]
        def reject(reason):errors.append(reason);bad(reason)
        owners={e['padding1'] if e['type'] in (82,83) else e['padding0'] for e in es}
        if not serial or len(owners)!=1 or not all(owners):reject('invalid_detail_task_identity_or_owner')
        for e in es:
            t=e['type'];f=e['flags']
            if e['end']<e['start']:reject('negative_detail_interval')
            if t<=85 and e['start']!=e['end']:reject('noninstant_detail_marker')
            if (t in (80,81) and not f) or (t in (82,83,84,87) and f) or (t in (85,86) and f not in (0,1)):reject('invalid_detail_flags')
            if t in (82,83) and (not e['padding0'] or e['padding0']==serial):reject('invalid_dependency_identity')
            if t in (84,87,88) and e['padding1']:reject('invalid_detail_reserved_payload')
            if t==80 and not e['padding1']:reject('invalid_enqueue_depth')
        if any(len(group[t])>1 for t in (80,84,85,86,87,88)):reject('duplicate_detail_task_marker')
        row=dict(task=serial,scheduler=next(iter(owners)) if len(owners)==1 else None,join_state='invalid',ready_publication_cpu_ns=None,enqueue_to_first_start_ms=None,run_attempts=[],dependency_waits=[],persistence=None,event_indices=[e['index'] for e in es])
        running=None;blocked=None;enqueued=None;ready=None
        if not errors:
            for e in sorted((e for e in es if e['type']<=84),key=lambda e:(e['start'],e['index'])):
                t=e['type']
                if t==80:
                    if running or blocked or ready or enqueued:reject('invalid_task_transition');break
                    enqueued=e
                elif t==81:
                    if running or blocked or ready:reject('invalid_task_transition');break
                    running=e
                    if row['dependency_waits'] and row['dependency_waits'][-1]['resumed_cpu_ns'] is None:row['dependency_waits'][-1]['resumed_cpu_ns']=e['start']
                    if enqueued and row['enqueue_to_first_start_ms'] is None:row['enqueue_to_first_start_ms']=(e['start']-enqueued['start'])/1e6
                elif t==82:
                    if not running:reject('invalid_task_transition');break
                    row['run_attempts'].append(dict(start_event=running['index'],end_event=e['index'],duration_ms=(e['start']-running['start'])/1e6,outcome='dependency_block'));running=None;blocked=e
                elif t==83:
                    if enqueued and not blocked:reject('orphan_dependency_release');break
                    if blocked and blocked['padding0']!=e['padding0']:reject('ambiguous_dependency_release');break
                    if blocked:
                        link=dict(dependency=e['padding0'],blocked_event=blocked['index'],released_event=e['index'],resumed_cpu_ns=None,wait_ms=(e['start']-blocked['start'])/1e6,dependency_join='unknown')
                        row['dependency_waits'].append(link);links.append(link)
                    blocked=None
                elif t==84:
                    if blocked or (enqueued and not running):reject('invalid_task_transition');break
                    if running:row['run_attempts'].append(dict(start_event=running['index'],end_event=e['index'],duration_ms=(e['start']-running['start'])/1e6,outcome='ready_publication_edge'))
                    running=None;ready=e
            row['join_state']='complete_observed_task' if enqueued and ready else 'incomplete_capture_or_boundary'
            row['ready_publication_cpu_ns']=ready['start'] if ready else None
            if group[85]:
                admission=group[85][0];run=group[86][0] if group[86] else None
                p=dict(admission='accepted' if admission['flags']==0 else 'rejected',charged_bytes=admission['padding1'],queue_wait_ms=None,callable_duration_ms=None,callable_result=None,cache_lock_wait_ms=None,cache_call_ms=None,cache_write_status=None)
                if ready and admission['start']<ready['start']:reject('persistence_before_readiness')
                if run:
                    if admission['flags'] or run['padding1']!=admission['padding1'] or run['start']<admission['start']:reject('invalid_persistence_join')
                    else:p.update(queue_wait_ms=(run['start']-admission['start'])/1e6,callable_duration_ms=(run['end']-run['start'])/1e6,callable_result='exception' if run['flags'] else 'returned_result_unknown')
                for t,name in ((87,'cache_lock_wait_ms'),(88,'cache_call_ms')):
                    if group[t]:
                        e=group[t][0]
                        if not run or not run['start']<=e['start']<=e['end']<=run['end']:reject('writer_interval_outside_persistence')
                        else:
                            p[name]=(e['end']-e['start'])/1e6
                            if t==88:p['cache_write_status']=sqlite_status(e['flags'],'set')
                if group[87] and group[88] and group[87][0]['end']>group[88][0]['start']:reject('writer_call_before_lock_acquired')
                if group[88] and group[88][0]['flags']==0xfffffffe and run and run['flags']!=1:reject('writer_exception_not_propagated')
                row['persistence']=p
            elif any(group[t] for t in (86,87,88)):row['join_state']='incomplete_capture_or_boundary'
        if errors:row.update(join_state='invalid',ready_publication_cpu_ns=None,enqueue_to_first_start_ms=None,run_attempts=[],dependency_waits=[],persistence=None)
        taskrows.append(row)
    taskmap={r['task']:r for r in taskrows}
    for link in links:
        dep=taskmap.get(link['dependency']);link['dependency_join']='observed_ready_task' if dep and dep['join_state']=='complete_observed_task' else 'missing_incomplete_or_invalid_dependency'
        if dep and dep['ready_publication_cpu_ns'] is not None and link['resumed_cpu_ns'] is not None and dep['ready_publication_cpu_ns']>link['resumed_cpu_ns']:
            bad('dependency_resumed_before_ready');link.update(dependency_join='contradictory_ready_chronology',wait_ms=None)
    caches=[]
    for (t,cache,operation),es in sorted(cachegroups.items()):
        if len(es)!=1:bad('duplicate_checked_cache_operation');continue
        e=es[0]
        if not cache or not operation or e['padding1'] or e['end']<e['start']:bad('invalid_checked_cache_operation');continue
        name='get' if t==93 else 'init'
        caches.append(dict(cache=cache,operation=operation,event_type=t,event_index=e['index'],name=name,duration_ms=(e['end']-e['start'])/1e6,status=sqlite_status(e['flags'],name)))
    chunkmap={(c['queue'],c['chunk']):c for c in chunks};completion=defaultdict(list)
    for e in events:
        if e['type']==23:completion[(e['object'],e['padding0'])].append(e)
    readbacks=[]
    for (queue,seq),group in sorted(finishes.items()):
        if not queue or not seq or set(group)!={90,91,92} or any(len(v)!=1 for v in group.values()):bad('incomplete_or_duplicate_finish_probes');continue
        a,b,c=[group[t][0] for t in (90,91,92)];chunk=chunkmap.get((queue,seq));done=completion.get((queue,seq),[])
        if chunk is None or len(done)!=1:bad('finish_missing_or_ambiguous_chunk');continue
        if any(e['padding1']!=chunk['frame'] or e['flags'] for e in (a,b,c)):bad('finish_frame_or_flags_disagreement');continue
        if b['start']!=b['end'] or c['start']!=c['end'] or not done[0]['start']<=a['start']<=a['end']<=b['start']<=c['start']:bad('invalid_finish_cpu_chronology');continue
        readbacks.append(dict(queue=queue,chunk=seq,frame=chunk['frame'],completion_status=chunk['completion_status'],completion_to_readback_ms=(a['start']-done[0]['start'])/1e6,readback_publication_ms=(a['end']-a['start'])/1e6,readback_to_reset_complete_ms=(b['start']-a['end'])/1e6,coherence_signal_scope_ms=(c['start']-b['start'])/1e6,completion_to_coherence_observed_ms=(c['start']-done[0]['start'])/1e6))
    loss={k:summary.get(k,0) for k in ('dropped_before','dropped_after','uncommitted_reserved')}
    historical=summary.get('wrong_generation',0)
    for value in (*loss.values(),historical):
        if type(value) is not int or not 0<=value<2**64:raise ValueError('invalid detailed capture loss counter')
    return dict(capture_usable=not failures and not invalid and not any(loss.values()) and not summary.get('counters_changed'),source_failures=failures,loss=loss,historical_generation_records_excluded=historical,tasks=taskrows,readbacks=readbacks,checked_cache_operations=caches,
                runnable_samples=[dict(task=e['object'],scheduler=e['padding0'],event=e['type'],cpu_ns=e['start'],runnable_depth=e['padding1'],workers=e['flags']) for e in events if e['type'] in (80,81) and taskmap[e['object']]['join_state']!='invalid'],
                limits=['CPU steady durations only; no GPU/CPU epoch subtraction, CPU busy sum, critical-path or FPS claim.',
                        '80/81 depth and workers are mutex-held samples, not occupancy between samples.',
                        '84/83 label ready-publication and continuation release edges before consumers can run.',
                        '86 callable returned is not SQL/cache success. 88/93/94 retain checked SQLite status; write completion is not durability or an FPS gain.',
                        '92 observes signal return, not consumer wake latency. Missing boundary evidence stays unknown.'])
