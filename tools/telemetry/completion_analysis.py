"""Sparse, bounded completion observations; never joins native and PE epochs."""
from collections import defaultdict

CARD_TYPES=set(range(111,117))
RESET_TYPES={117,118,119}
NO_FRAME_TARGET=2**64-1
THRESHOLD=50_000_000


def completion_analysis(events,summary,profile):
    groups=defaultdict(lambda:defaultdict(list));retirements=defaultdict(list);failures=[];cards=[];identities=set()
    def fail(reason):
        if reason not in failures:failures.append(reason)
    for e in events:
        if e['type'] in {108,109,110}|RESET_TYPES:retirements[(e['object'],e['padding0'])].append(e)
        if e['type'] not in CARD_TYPES:continue
        groups[(e['generation'],e['object'],e['thread'],e['start'],e['end'])][e['type']].append(e)
        if profile['name']!='stall-waits':fail('completion_card_outside_stall_profile')
        if not e['object'] or not e['thread'] or not e['start'] or e['end']<e['start'] or e['end']-e['start']<THRESHOLD:
            fail('invalid_completion_card_interval_or_identity')
    for key,g in sorted(groups.items()):
        if any(len(g[t])!=1 for t in CARD_TYPES):
            fail('missing_or_duplicate_completion_card_part');continue
        a,b,c,d,e,f=[g[t][0] for t in range(111,117)]
        queue,thread,start,end=key[1:];chunk,frame=a['padding0'],a['padding1'];before=a['flags']&7;after=(a['flags']>>3)&7
        cpu_valid=bool(a['flags']&64);gpu_valid=bool(a['flags']&128)
        errors=[]
        def bad(reason):errors.append(reason);fail(reason)
        if not chunk or before>3 or after>5 or a['flags']&~255:bad('invalid_completion_status_or_flags')
        if any(v['flags'] for v in (b,c,e,f)) or d['flags']>32:bad('invalid_completion_reserved_payload')
        if b['padding0']!=NO_FRAME_TARGET and (frame==NO_FRAME_TARGET or b['padding0']!=frame+1):bad('invalid_completion_final_frame_binding')
        if not b['padding1'] and c['padding0']:bad('initializer_observation_without_dependency')
        if not cpu_valid and c['padding1']:bad('invalid_completion_thread_cpu_validity')
        if f['padding1'] or (gpu_valid and after!=4) or (not gpu_valid and f['padding0']):bad('invalid_completion_gpu_validity')
        if d['padding0']<=chunk or d['padding1']<=chunk or e['padding0']>=chunk:bad('invalid_completion_queue_frontier')
        for phase in retirements[(queue,chunk)]:
            if phase['thread']!=thread or phase['padding1']!=frame:bad('completion_retirement_binding_disagreement')
            if phase['start']<end:bad('retirement_precedes_completion_wait_return')
        identity=queue,chunk
        if identity in identities:bad('duplicate_completion_chunk_identity')
        identities.add(identity)
        if errors:continue
        cards.append(dict(event_indices=[g[t][0]['index'] for t in range(111,117)],generation=key[0],queue_birth_serial=queue,
                          chunk_sequence=chunk,queue_frame=frame,thread=thread,start_cpu_ns=start,end_cpu_ns=end,
                          wait_cpu_wall_ms=(end-start)/1e6,status_before=before,status_after=after,
                          final_frame_target=None if b['padding0']==NO_FRAME_TARGET else b['padding0'],
                          initializer_dependency_target=b['padding1'],initializer_observed_pre_wait=c['padding0'],
                          thread_cpu_valid=cpu_valid,thread_cpu_delta_100ns=c['padding1'] if cpu_valid else None,
                          thread_cpu_ms=c['padding1']/10000 if cpu_valid else None,
                          gpu_duration_valid=gpu_valid,native_gpu_duration_ns=f['padding0'] if gpu_valid else None,
                          native_gpu_duration_ms=f['padding0']/1e6 if gpu_valid else None,
                          pre_wait_queue_frontiers=dict(ready_for_encode=d['padding0'],ready_for_commit=d['padding1'],
                              chunk_ongoing=d['flags'],cpu_coherent=e['padding0'],frame_latency=e['padding1'])))
    integrity=(summary.get('inconclusive_window') is False and summary.get('structurally_complete') is True
               and summary.get('producer_quiescence_verified') is True and not summary.get('counters_changed')
               and not any(summary.get(k,0) for k in ('dropped_before','dropped_after','uncommitted_reserved'))
               and not any(e['type']==89 for e in events))
    return dict(coverage='observed_completed_slow_wait_cards' if cards else 'unknown',cards=cards,fields_valid=not failures,
                capture_usable=bool(cards and not failures and integrity and profile['policy_valid']),failures=failures,
                unfinished_waits='unobserved_no_begin_records',missing_cards='unknown_threshold_filtered_disabled_capped_or_unfinished',
                limits=['Cards are emitted only after a wait returns at >=50 ms; an unfinished wait has no card or begin coverage.',
                        'Six parts share one writer admission and the existing detailed budget; cap exhaustion can leave a rejected partial card.',
                        'Thread CPU is optional GetThreadTimes kernel+user delta in 100 ns units; CPU wall includes blocking and preemption.',
                        'Thread CPU samples bracket wall timestamps, include sampler/bookkeeping margins, and are not pure native-wait CPU busy cost or a CPU/wall ratio.',
                        'GPU duration is native same-domain end minus start only for valid completed status; no native/PE epoch join is performed.',
                        'Pre-wait frontier loads are individual observations, not an atomic queue snapshot or a causal diagnosis.',
                        'Missing cards and zero observed durations never establish zero work, cause, FPS, or full-frame coverage.'])
