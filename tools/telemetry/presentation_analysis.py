"""Identity-only presentation coverage gate; no epoch conversion or missing zeros."""
from collections import Counter, defaultdict
import re


def presentation_analysis(events, summary, profile, wave7, chunks, detailed, stats, rejected):
    failures = []
    def bad(reason):
        if reason not in failures:
            failures.append(reason)
    if profile['name'] not in ('presentation-waits', 'combined-bounded'):
        bad('expected_presentation_waits_profile')
    if any(rejected.values()):bad('rejected_trace_record')
    if not profile['policy_valid']:
        bad('invalid_profile_policy')
    for field in ('inconclusive_window', 'counters_changed'):
        if summary.get(field) is not False:
            bad('capture_' + field)
    for field in ('structurally_complete', 'producer_quiescence_verified'):
        if summary.get(field) is not True:
            bad('capture_' + field + '_unverified')
    for field in ('dropped_before', 'dropped_after', 'uncommitted_reserved', 'boundary_rejected_total'):
        if type(summary.get(field)) is not int or summary[field] != 0:
            bad('capture_' + field + '_nonzero_or_unknown')
    if type(summary.get('identity')) is not str or re.fullmatch(r'[0-9]+:[1-9][0-9]*', summary['identity']) is None:
        bad('missing_mapping_identity')
    selected = [e for e in events if e['type'] in (96, 97, 98, 99)]
    groups = defaultdict(list)
    for e in selected:
        t, obj, frame, other, flags = (e[k] for k in ('type', 'object', 'padding0', 'padding1', 'flags'))
        groups[(t, obj, frame)].append(e)
        valid = obj and e['thread'] and e['start'] <= e['end']
        if t == 96:
            valid = valid and flags and frame + 1 > flags and other == frame + 1 - flags
        elif t == 97:
            valid = valid and flags and frame > flags and other == frame - flags
        elif t == 98:
            valid = valid and frame and other and flags == 0
        else:
            valid = valid and frame and flags == 0 and e['start'] == e['end']
        if not valid:
            bad('invalid_presentation_identity_or_payload')
    if any(len(v) != 1 for v in groups.values()):
        bad('duplicate_presentation_identity')
    counts = Counter(e['type'] for e in events)
    # These coverage families are mandatory; their absence is unknown, never zero.
    for kind in (20, 21, 22, 23, 24, 25, 40, 70, 71, 72, 73, 96, 98, 99):
        if not counts[kind]:
            bad('missing_required_event_' + str(kind))
    rawchunks = defaultdict(list)
    for e in events:
        if 20 <= e['type'] <= 24:
            rawchunks[(e['object'], e['padding0'])].append(e)
    for (queue, seq), es in rawchunks.items():
        if not queue or not seq or Counter(e['type'] for e in es) != Counter({k: 1 for k in range(20, 25)}):
            bad('incomplete_or_duplicate_raw_chunk_identity')
        if profile['name']=='combined-bounded' and any(count>1 for count in Counter(e['type'] for e in es).values()):bad('duplicate_raw_chunk_identity')
        if any(not e['thread'] for e in es):
            bad('missing_chunk_thread_identity')
        if any(e['flags'] != (4 if e['type'] in (23,24) else 0) or
               (e['type']!=24 and e['start']!=e['end']) for e in es):
            bad('invalid_chunk_flags_or_instant')
    if len(rawchunks)!=len(chunks):bad('unaccepted_raw_chunk_identity')
    if any(c['swapchain_if_unambiguous'] is None or not c['included_in_successful_aggregates'] or c['gpu_duration_ms'] is None for c in chunks):bad('unbound_or_unsuccessful_chunk')
    owners = defaultdict(list)
    for e in events:
        if e['type'] == 25:
            owners[(e['object'], e['padding1'], e['start'])].append(e)
    if any(len(v)!=1 for v in owners.values()) or len({(k[0],k[1]) for k in owners})!=len(owners):bad('duplicate_queue_frame_owner')
    chunk_groups = defaultdict(list)
    for c in chunks:
        chunk_groups[(c['queue'], c['frame'])].append(c)
    feedback = {(f['swapchain'], f['frame']): f for f in wave7['native_presented_frames']}
    joined = []
    for binding in (e for e in selected if e['type'] == 99):
        swap, native, queueframe = binding['object'], binding['padding0'], binding['padding1']
        owner = owners.get((swap, queueframe, binding['start']), [])
        drawable = groups.get((98, swap, native), [])
        f = feedback.get((swap, native))
        if len(owner) != 1 or not owner[0]['padding0'] or owner[0]['flags'] or owner[0]['end'] != owner[0]['start']:
            bad('missing_or_ambiguous_queue_native_frame_binding'); continue
        queue = owner[0]['padding0']
        cs = chunk_groups.get((queue, queueframe), [])
        wait = groups.get((96, queue, queueframe), [])
        if not cs or any(c['swapchain_if_unambiguous'] != swap for c in cs):
            bad('missing_or_ambiguous_frame_chunks'); continue
        if any(not c['included_in_successful_aggregates'] or c['gpu_duration_ms'] is None for c in cs):
            bad('unsuccessful_or_missing_gpu_completion'); continue
        if len(wait) != 1 or len(drawable) != 1 or f is None or not f['strict_accepted']:
            bad('missing_or_invalid_frame_wait_drawable_feedback'); continue
        apis=[e for e in events if e['type']==40 and e['object']==swap and e['thread']==binding['thread'] and e['start']<=binding['start']<=e['end']]
        if len(apis)!=1 or apis[0]['flags']!=0:
            bad('missing_duplicate_or_failed_present_api'); continue
        d = drawable[0]
        if d['padding1'] != f['configuration_revision']:
            bad('drawable_feedback_revision_disagreement'); continue
        joined.append(dict(swapchain=swap, native_frame=native, queue=queue, queue_frame=queueframe,
                           present_api_event_index=apis[0]['index'], binding_event_index=binding['index'], chunk_sequences=[c['chunk'] for c in cs],
                           drawable_event_index=d['index'], feedback_event_indices=f['event_indices'],
                           frame_latency_wait_ms=(wait[0]['end']-wait[0]['start'])/1e6,
                           drawable_acquire_ms=(d['end']-d['start'])/1e6,
                           configuration_revision=d['padding1']))
    # Boundary or corrupt extra identities cannot silently disappear behind a good row.
    if len(joined) != counts[99] or len(joined) != counts[25] or len(joined) != counts[98] or len(joined) != counts[96] or len(joined) != len(feedback) or len(joined) != counts[40]:
        bad('unjoined_or_boundary_presentation_records')
    if not joined:
        bad('no_complete_identity_joined_frames')
    if not any(c['intervals'] for c in wave7['native_presented_cadence']):
        bad('no_consecutive_native_cadence')
    if not all(f['strict_accepted'] and f['configuration_revision'] for f in feedback.values()):
        bad('invalid_native_feedback_or_revision')
    if not wave7['strict_acceptance']['capture_identity_quality_ok']:
        bad('unidentified_native_feedback')
    capabilities=wave7['capabilities']
    if any(sum(c['swapchain']==swap for c in capabilities)!=1 for swap,_ in feedback):bad('missing_or_duplicate_native_capability_identity')
    for c in capabilities:
        if c['status'] != 0 or c['protocol_version'] != 1 or not (c['feature_bits'] & 1):
            bad('native_feedback_capability_unsupported')
    if wave7['native_feedback_failures'] or wave7['native_configuration_apply_failures']:
        bad('native_feedback_or_configuration_failure')
    last_native={}
    for c in wave7['native_counter_samples']:
        key=(c['swapchain'],c['session'])
        if c['complete'] and key in last_native and any(c['values'][k]<last_native[key][k] for k in ('accepted','completed','abandoned','overflow','rejected')):bad('native_counter_regression')
        if c['complete']:last_native[key]=c['values']
        if profile['name']=='combined-bounded' and any(c['values'].get(k, 0) for k in ('abandoned', 'overflow', 'rejected')):bad('observed_native_counter_loss')
        if not c['complete'] or any(c['values'].get(k, 0) for k in ('abandoned', 'overflow', 'rejected')):
            bad('native_counter_loss_or_incomplete_sample')
    close_groups=defaultdict(Counter)
    for e in events:
        if e['type'] in (75,78):close_groups[(e['object'],e['start'])][e['type']]+=1
    if profile['name']=='combined-bounded' and any(any(count>1 for count in g.values()) for g in close_groups.values()):bad('duplicate_native_close_part')
    if any(g!=Counter({75:1,78:1}) for g in close_groups.values()):bad('incomplete_or_duplicate_native_close_pair')
    for c in wave7['native_session_closes']:
        if c['status'] or c['live_handler_tickets_at_close'] or c['abandoned_handler_count']:
            bad('native_close_incomplete')
    for c in wave7['native_close_queued_discard_accounting']:
        if profile['name']=='combined-bounded' and c['status']:bad('native_close_accounting_failure')
        if profile['name']=='combined-bounded' and c['queued_discarded']:bad('observed_native_close_discard')
        if not c['accounting_verified'] or c['queued_discarded']:
            bad('native_close_discard_or_unknown')
    if not detailed['capture_usable']:
        bad('detailed_finish_or_cap_failure')
    admission=[]
    swaps={r['swapchain'] for r in joined}
    for e in selected:
        if e['type']==97:
            if e['object'] not in swaps:bad('unbound_swapchain_admission_wait')
            admission.append(dict(swapchain=e['object'],local_presentation_count=e['padding0'],awaited_count=e['padding1'],configured_latency=e['flags'],duration_ms=(e['end']-e['start'])/1e6,event_index=e['index']))
    finishes=[]
    for row in detailed['readbacks']:
        row=dict(row)
        row['post_reset_signal_finish_observation_ms']=row.pop('coherence_signal_scope_ms')
        row['completion_to_post_signal_finish_observed_ms']=row.pop('completion_to_coherence_observed_ms')
        finishes.append(row)
    contradiction_types={'invalid_presentation_identity_or_payload','duplicate_presentation_identity',
                         'duplicate_raw_chunk_identity','missing_chunk_thread_identity','invalid_chunk_flags_or_instant',
                         'duplicate_queue_frame_owner','drawable_feedback_revision_disagreement',
                         'unidentified_native_feedback',
                         'native_feedback_capability_unsupported','native_feedback_or_configuration_failure',
                         'native_counter_regression','observed_native_counter_loss',
                         'duplicate_native_close_part','native_close_incomplete','native_close_accounting_failure','observed_native_close_discard'}
    contradictions=[reason for reason in failures if reason in contradiction_types]
    # Missing triplets/counter samples/CLOSE partners can be capture boundaries.
    # Malformed observed native fields and duplicate parts are contradictions.
    native_field_errors={'unidentified_native_feedback_record','invalid_wave7_identity_or_noninstant',
                         'invalid_native_counter_identity_or_index','invalid_feedback_close_accounting_session',
                         'invalid_resize_snapshot_flags','duplicate_native_feedback_frame_type',
                         'invalid_feedback_frame_identity_or_flags','native_feedback_receipt_disagreement',
                         'invalid_native_drawable_dimensions','invalid_native_presented_timestamp_or_domain_flags',
                         'nonfinite_native_presentation_interval','nonmonotonic_native_presentation_stream',
                         'native_cumulative_counter_regression'}
    contradictions.extend(reason for reason in native_field_errors if rejected.get(reason,0))
    counter_parts=Counter((e['object'],e['padding0'],e['start'],e['flags']) for e in events if e['type']==74)
    if any(count>1 for count in counter_parts.values()):contradictions.append('duplicate_native_counter_part')
    if any(e['type']==72 and not e['padding1'] for e in events):contradictions.append('zero_observed_configuration_revision')
    contradictions=sorted(set(contradictions))
    return dict(**(dict(coverage='observed_identity_joined_frames' if joined else 'unknown',observed_contradictions=contradictions) if profile['name']=='combined-bounded' else {}),capture_usable=not failures, failures=failures, completion_finish_observations=finishes, swapchain_admission_waits=admission, event_counts={str(k): v for k, v in sorted(counts.items())},
                joined_frames=joined,
                frame_latency_wait=stats([int(r['frame_latency_wait_ms']*1e6) for r in joined]) if joined else None,
                drawable_acquire=stats([int(r['drawable_acquire_ms']*1e6) for r in joined]) if joined else None,
                native_close_observed=bool(wave7['native_session_closes']),
                limits=['Acceptance covers only complete captured identities; boundary extras fail this gate and remain visible.',
                        '96 queue presented-frame and 97 swapchain local admission counter differ from native frame; 99 explicitly bridges queue/native counters through 25.',
                        '1/2 and 6-16 are legacy censored or pointer/local-counter observations; no exact joins or missing zeros are inferred from them.',
                        'CPU steady nanoseconds, native GPU nanoseconds and CoreAnimation host seconds stay separate; no CPU-to-native displayed latency is claimed.',
                        '91 to 92 is post-reset signal/finish observation including optional idle retirement, not pure coherence signalling cost.',
                        'Producer quiescence does not drain all native callbacks; close evidence is optional and reported separately, never assumed.',
                        'This offline coverage gate does not prove loaded binaries, phase/gameplay, physical scanout, GPU saturation or any FPS improvement.'])
