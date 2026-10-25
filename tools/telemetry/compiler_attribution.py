"""Optional, identity-bound compiler CPU wall intervals; absence stays unknown."""
from collections import defaultdict

STAGES = {105: 'AIR conversion', 106: 'Metal newLibrary', 107: 'Metal newFunction'}
RESULTS = {1: 'success', 2: 'returned_failure', 3: 'exception'}


def compiler_attribution(events, summary, detailed, profile, rejected):
    observed = [e for e in events if 104 <= e['type'] <= 107]
    errors = defaultdict(set)
    by_index = {e['index']: e for e in events}
    tasks = {t['task']: t for t in detailed['tasks']}
    stages = [e for e in observed if e['type'] != 104]
    waits = [e for e in observed if e['type'] == 104]
    reads = defaultdict(list)
    for e in events:
        if e['type'] == 93:
            reads[e['object'], e['padding0']].append(e)

    def bad(e, reason):
        if reason not in errors[e['index']]:
            rejected[reason] += 1
            errors[e['index']].add(reason)

    # The producer allocates one global operation serial per scope. Only the
    # reader wait and its checked get intentionally share their read serial.
    operations = defaultdict(list)
    for e in observed:
        op = e['padding1'] if e['type'] == 104 else e['padding0']
        operations[op].append(e)
        if not e['object'] or not op or not e['thread'] or not e['start']:
            bad(e, 'invalid_compiler_scope_identity')
        if e['end'] < e['start']:
            bad(e, 'invalid_compiler_scope_interval')
        if e['type'] == 104:
            if e['flags'] not in (1, 3):bad(e, 'invalid_reader_wait_result')
        elif e['flags'] not in RESULTS or e['padding1'] not in (0, 1) or (e['type'] == 105 and e['padding1']):
            bad(e, 'invalid_compiler_stage_payload')
    for group in operations.values():
        if len(group) > 1:
            for e in group:bad(e, 'duplicate_compiler_operation_serial')

    def task_join(e, serial, end=None):
        if not serial:return 'noncompiler_or_unavailable_parent'
        task = tasks.get(serial)
        if not task or task['join_state'] != 'complete_observed_task':
            return 'missing_incomplete_or_invalid_task'
        stop = e['end'] if end is None else end
        attempts = []
        for attempt in task['run_attempts']:
            a, b = by_index[attempt['start_event']], by_index[attempt['end_event']]
            if a['start'] <= e['start'] <= stop <= b['start']:
                attempts.append((a, b))
        if len(attempts) != 1:
            bad(e, 'compiler_scope_outside_observed_task_run')
            return 'contradictory_task_chronology'
        a, b = attempts[0]
        if a['thread'] != e['thread'] or b['thread'] != e['thread']:
            bad(e, 'compiler_scope_task_thread_disagreement')
            return 'contradictory_task_thread'
        if e['flags'] == 3:
            bad(e, 'compiler_exception_followed_by_task_readiness')
            return 'contradictory_task_completion'
        return 'joined_observed_task_run'

    # Non-overlap is a property of these synchronous calls on one work item,
    # not an assumption that all work items or backend compiler queues serialize.
    task_stages = defaultdict(list)
    for e in stages:task_stages[e['object']].append(e)
    for group in task_stages.values():
        ordered = sorted(group, key=lambda e: (e['start'], e['end'], e['index']))
        for a, b in zip(ordered, ordered[1:]):
            if a['end'] > b['start']:
                bad(a, 'overlapping_compiler_stages_for_task')
                bad(b, 'overlapping_compiler_stages_for_task')
            # Cache failure may fall back to cold conversion; reverse fallback
            # and stages after an exception contradict the observed code path.
            if (a['padding1'] == 0 and b['padding1'] == 1) or a['flags'] == 3:
                bad(b, 'contradictory_compiler_stage_order')
            if a['padding1'] == b['padding1']:
                if a['type'] >= b['type'] or (a['type'] == 105 and a['flags'] != 1):
                    bad(b, 'contradictory_compiler_stage_order')
            if a['padding1'] == 1:
                # Cached library failure skips newFunction; a successful cached
                # function finishes this path. Missing cached function evidence
                # after library success cannot establish either outcome.
                if (a['type'] == 106 and a['flags'] == 2 and b['type'] == 107 and b['padding1'] == 1) or (a['type'] == 107 and a['flags'] == 1 and b['padding1'] == 0):
                    bad(b, 'contradictory_compiler_stage_order')
        # A shared work identity with any contradictory stage is not usable for
        # partial stage attribution; retain raw records and explicit errors.
        if any(errors[e['index']] for e in group):
            for e in group:bad(e, 'invalid_compiler_task_stage_sequence')

    checked = {(r['cache'],r['operation']):r for r in detailed['checked_cache_operations'] if r['event_type']==93}
    for wait in waits:
        group = reads.get((wait['object'],wait['padding1']), [])
        if wait['padding0'] and wait['flags']==3:
            for e in task_stages.get(wait['padding0'], []):
                if e['start'] >= wait['end']:
                    bad(e, 'compiler_stage_after_reader_lock_exception')
                    bad(wait, 'compiler_stage_after_reader_lock_exception')
        if wait['padding0'] and wait['flags']==1 and len(group)==1:
            read = group[0]
            if read['thread']==wait['thread'] and wait['end']<=read['start']<=read['end']:
                for e in task_stages.get(wait['padding0'], []):
                    if e['start'] < read['end']:
                        bad(e, 'compiler_stage_before_checked_read_finished')
                        bad(wait, 'compiler_stage_before_checked_read_finished')
                    if (wait['object'],wait['padding1']) in checked and checked[wait['object'],wait['padding1']]['status']['operation_result'] in ('miss','failed') and e['padding1']==1:
                        bad(e, 'cached_compiler_stage_after_failed_or_missed_checked_get')
                        bad(wait, 'cached_compiler_stage_after_failed_or_missed_checked_get')
    read_serials = {e['padding0'] for group in reads.values() for e in group}
    for e in stages:
        if e['padding0'] in read_serials:bad(e, 'compiler_stage_reuses_read_operation_serial')

    stage_rows = []
    for e in stages:
        join = task_join(e, e['object'])
        valid = not errors[e['index']]
        stage_rows.append(dict(event_index=e['index'],task=e['object'],operation=e['padding0'],
            thread=e['thread'],stage=STAGES[e['type']],event_type=e['type'],
            route='cold' if e['padding1'] == 0 else 'cache' if e['padding1'] == 1 else 'unknown',
            result=RESULTS.get(e['flags'], 'unknown'),task_join=join,valid=valid,
            start_cpu_ns=e['start'],end_cpu_ns=e['end'],
            observed_cpu_wall_duration_ms=(e['end']-e['start'])/1e6 if valid else None,
            errors=sorted(errors[e['index']])))

    wait_rows = []
    for e in waits:
        key = e['object'],e['padding1']
        group = reads.get(key, [])
        read = group[0] if len(group) == 1 else None
        read_join = 'missing_checked_get'
        full_end = e['end']
        if group:
            if read is None or key not in checked:
                bad(e, 'reader_wait_ambiguous_or_invalid_get');read_join='ambiguous_or_invalid_get'
            elif read['thread'] != e['thread']:
                bad(e, 'reader_wait_get_thread_disagreement');read_join='contradictory_get_thread'
            elif e['flags'] != 1 or e['end'] > read['start']:
                bad(e, 'reader_wait_get_chronology');read_join='contradictory_get_chronology'
            else:
                read_join='exact_cache_and_read_operation';full_end=read['end']
        join = task_join(e, e['padding0'], full_end)
        valid = not errors[e['index']]
        wait_rows.append(dict(event_index=e['index'],cache=e['object'],operation=e['padding1'],
            task=e['padding0'] or None,thread=e['thread'],task_join=join,
            result={1:'acquired',3:'exception'}.get(e['flags'],'unknown'),valid=valid,
            start_cpu_ns=e['start'],end_cpu_ns=e['end'],
            observed_cpu_wall_wait_ms=(e['end']-e['start'])/1e6 if valid else None,
            checked_get_join=read_join,checked_get_event_index=read['index'] if read_join=='exact_cache_and_read_operation' else None,
            checked_get_status=checked[key]['status'] if valid and read_join=='exact_cache_and_read_operation' else None,
            errors=sorted(errors[e['index']])))
    usable = bool(observed) and not any(errors.values()) and detailed['capture_usable'] and profile['capture_usable'] and summary.get('inconclusive_window') is False
    return dict(coverage='observed_scopes' if observed else 'unknown',capture_usable=bool(usable),
        stages=stage_rows,reader_waits=wait_rows,
        missing_stages='unknown',summed_cpu_cost_ms=None,backend_queue_time_ms=None,critical_path_ms=None,
        limits=['Optional events 104-107 use CPU steady wall intervals and the existing shared detail budget.',
            'Wall intervals include blocking and preemption; they are not CPU busy cost, backend queue residence, or GPU time.',
            'Task joins require exact work serial, a valid observed run, matching thread, and contained chronology.',
            'Reader wait joins checked get only by exact cache birth serial and read operation serial; event 93 retains SQLite semantics.',
            'Absent scopes, absent task boundaries, and absent get records remain unknown; no zero-cost, critical-path, or FPS inference.',
            'Capture integrity and profile validity are required; complete observed scopes do not prove complete task or capture coverage.'])
