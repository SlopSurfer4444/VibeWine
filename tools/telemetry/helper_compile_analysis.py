"""Bounded helper compilation CPU wall observations; opaque owner identities."""
from collections import Counter, defaultdict
import re

ROUTES = {1: 'tessellation_mutex_acquisition', 2: 'tessellation_native_pipeline_creation',
          3: 'clear_uav_compute_pipeline_request', 4: 'tile_helper_pipeline_creation',
          5: 'rtv_helper_pipeline_creation'}
RESULTS = {1: 'success', 2: 'failure', 3: 'unknown'}


def helper_span_errors(events):
    spans = [e for e in events if e['type'] == 120]
    counts = Counter(e['object'] for e in spans)
    errors = defaultdict(set)
    for e in spans:
        if not e['object'] or not e['thread'] or not e['start']:
            errors[e['index']].add('invalid_helper_span_identity')
        if e['end'] < e['start']:
            errors[e['index']].add('invalid_helper_span_interval')
        if e['flags'] not in RESULTS or e['padding0'] not in ROUTES:
            errors[e['index']].add('invalid_helper_span_payload')
        if counts[e['object']] != 1:
            errors[e['index']].add('duplicate_helper_span_serial')
    return errors


def helper_compile_analysis(events, summary, profile, detailed, rejected):
    spans = [e for e in events if e['type'] == 120]
    errors = helper_span_errors(events)
    if spans and profile['name'] != 'combined-bounded':
        for e in spans:errors[e['index']].add('helper_span_outside_combined_profile')
    owners = defaultdict(list)
    for e in spans:
        if e['padding1']:owners[e['padding1']].append(e)
    pairs = []
    joins = {}
    for owner, group in sorted(owners.items()):
        locks = [e for e in group if e['padding0'] == 1]
        native = [e for e in group if e['padding0'] == 2]
        state = 'missing_or_ambiguous_partner'
        if len(locks) == len(native) == 1:
            a, b = locks[0], native[0]
            if errors[a['index']] or errors[b['index']]:
                state = 'invalid_partner'
            elif a['thread'] != b['thread'] or a['end'] > b['start']:
                state = 'contradictory_thread_or_chronology'
                for e in (a, b):errors[e['index']].add('invalid_helper_owner_pair_binding')
            else:
                state = 'exact_owner_and_thread_ordered_scopes'
                pairs.append(dict(owner_birth_serial=owner, thread=a['thread'],
                                  lock_event_index=a['index'], native_event_index=b['index'],
                                  lock_result=RESULTS[a['flags']], native_result=RESULTS[b['flags']]))
        for e in locks + native:joins[e['index']] = state
    rows = []
    for e in spans:
        valid = not errors[e['index']]
        for reason in errors[e['index']]:rejected[reason] += 1
        rows.append(dict(event_index=e['index'], generation=e['generation'], span_serial=e['object'],
                         owner_birth_serial=e['padding1'] or None, route_code=e['padding0'],
                         route=ROUTES.get(e['padding0'], 'unknown'), result=RESULTS.get(e['flags'], 'unknown'),
                         thread=e['thread'], start_cpu_ns=e['start'], end_cpu_ns=e['end'], valid=valid,
                         observed_cpu_wall_duration_ms=(e['end']-e['start'])/1e6 if valid else None,
                         owner_join=('unknown_owner' if not e['padding1'] else joins.get(e['index'], 'opaque_owner_only')),
                         task_join='unavailable_owner_is_not_task_identity', errors=sorted(errors[e['index']])))
    failures = []
    if any(errors.values()):failures.append('invalid_helper_span_fields_or_binding')
    if profile['name'] == 'combined-bounded':
        if not profile['policy_valid']:failures.append('invalid_profile_policy')
        if not detailed['capture_usable']:failures.append('detail_loss_cap_or_invalid_records')
        if summary.get('inconclusive_window') is not False or summary.get('counters_changed') is not False:
            failures.append('inconclusive_or_changing_capture')
        if summary.get('structurally_complete') is not True or summary.get('producer_quiescence_verified') is not True:
            failures.append('unverified_capture_quiescence')
        for field in ('dropped_before', 'dropped_after', 'uncommitted_reserved', 'boundary_rejected_total'):
            if type(summary.get(field)) is not int or summary[field] != 0:
                failures.append(field + '_nonzero_or_unknown')
        if not isinstance(summary.get('identity'), str) or re.fullmatch(r'[0-9]+:[1-9][0-9]*', summary['identity']) is None:
            failures.append('unknown_mapping_identity')
    return dict(coverage='observed_helper_cpu_wall_scopes' if spans else 'unknown',
                capture_usable=bool(spans and not failures), fields_valid=not any(errors.values()),
                failures=failures, spans=rows, tessellation_owner_pairs=pairs,
                owner_groups=[dict(owner_birth_serial=owner, event_indices=[e['index'] for e in group])
                              for owner, group in sorted(owners.items())],
                missing_helpers='unknown_disabled_capped_boundary_or_unobserved',
                summed_cpu_cost_ms=None, backend_queue_time_ms=None, critical_path_ms=None, gpu_time_ms=None, fps=None,
                limits=['Event 120 is an optional combined-bounded detail record using CPU steady wall time and the shared detail cap.',
                        'Scope serials identify immutable spans; owner serials identify opaque helper/pipeline objects, never compiler task or pointer identities.',
                        'Only tessellation lock/native records with an exact nonzero owner, matching thread and ordered intervals are paired; missing partners remain unknown.',
                        'Outcome 3 means unknown, not exception; a successful enclosing task cannot resolve it.',
                        'ClearUAV requests include cache lookup and possible native creation; they are not pure native compiler time.',
                        'Wall time includes preemption and blocking. Nested scopes can overlap and must not be summed as CPU busy cost, critical path, GPU time or FPS.',
                        'Absent records and cap/loss/boundary uncertainty cannot establish zero helper work or complete coverage.'])
