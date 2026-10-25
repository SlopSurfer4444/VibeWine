"""Sampled native visibility evidence; no interpolation or displayed-FPS claim."""
from collections import defaultdict
STATUS={0:'fresh',1:'bad_request',2:'unsupported',3:'unknown',4:'stale'}
EXPIRE_NS=1_000_000_000

def visibility_analysis(events,summary):
    groups=defaultdict(list);failures=[];samples=[];snapshots={}
    for e in events:
        if 100<=e['type']<=103:groups[(e['generation'],e['thread'],e['object'],e['start'])].append(e)
    for (generation,thread,swap,receipt),es in sorted(groups.items()):
        kinds={t:[e for e in es if e['type']==t] for t in range(100,104)}
        if any(len(v)!=1 for v in kinds.values()):failures.append('missing_or_duplicate_visibility_sample_fields');continue
        a,b,c,d=[kinds[t][0] for t in range(100,104)]
        if not thread or not swap or any(e['end']!=receipt or e.get('clock_domain')!='cpu_steady_ns' for e in es):
            failures.append('invalid_visibility_sample_identity_or_receipt');continue
        status=a['flags'];flags=b['flags'];layer=a['padding0'];sequence=a['padding1']
        observed=b['padding0'];age=b['padding1'];window=c['padding0'];display=c['padding1']
        if status not in STATUS or flags&~255 or c['flags'] or d['flags']:
            failures.append('invalid_visibility_sample_status_or_flags');continue
        if status==0 and (not layer or not sequence or not observed or age>=EXPIRE_NS):
            failures.append('fresh_visibility_missing_identity_or_expired');continue
        if not flags&1 and window and status==0:
            failures.append('visibility_host_identity_disagreement');continue
        if bool(flags&32)!=bool(d['padding0']&2):
            failures.append('visibility_occlusion_bit_disagreement');continue
        # A cached native observation is immutable across PE receipts/callers.
        # Status and age may change as it expires; configuration belongs to the
        # caller's current Present, not to the cached native snapshot.
        if layer and sequence:
            identity=(layer,sequence)
            payload=(observed,window,display,d['padding0'],flags)
            if identity in snapshots and snapshots[identity]!=payload:
                failures.append('conflicting_visibility_snapshot_identity');continue
            snapshots[identity]=payload
        fresh=status==0
        # Exact native sample only. Space membership and native visibility flags
        # do not establish continuous visibility, gameplay, or presentation.
        visible=fresh and bool(window) and flags&1 and flags&2 and flags&8 and flags&32 and not flags&(4|16|64|128)
        samples.append(dict(generation=generation,thread=thread,swapchain=swap,cpu_receipt_ns=receipt,
            native_clock='monotonic_raw_ns',native_observed_ns=observed,native_age_ns=age,
            status=STATUS[status],layer_identity=layer,observation_sequence=sequence,
            window_id=window,display_id=display,flags=flags,occlusion_state=d['padding0'],configuration_revision=d['padding1'],
            fresh=fresh,host_known=fresh and bool(flags&1) and bool(window),visible_on_active_space_at_observation=bool(visible),
            event_indices=[e['index'] for e in es]))
    observed=bool(groups)
    integrity=(summary.get('structurally_complete') is True and summary.get('producer_quiescence_verified') is True
               and summary.get('counters_changed') is False and summary.get('inconclusive_window') is False
               and summary.get('producer_profile_policy_valid') is True)
    for key in ('dropped_before','dropped_after','uncommitted_reserved','boundary_rejected_total'):
        if summary.get(key)!=0:integrity=False
    if observed and not integrity:failures.append('visibility_capture_loss_or_boundary_integrity_unknown')
    return dict(coverage='sampled_observations' if observed else 'not_observed_legacy_or_disabled',
        sample_fields_valid=not failures,capture_integrity_ok=integrity,
        visibility_evidence_usable=observed and integrity and not failures and any(s['fresh'] and s['host_known'] for s in samples),
        samples=samples,failures=sorted(set(failures)),
        fresh_sample_count=sum(s['fresh'] for s in samples),
        visible_on_active_space_sample_count=sum(s['visible_on_active_space_at_observation'] for s in samples),
        continuous_visibility_verified=False,displayed_fps=None,
        limits=['Missing legacy events mean no visibility coverage, never visible or zero FPS.',
                '100-103 are one sampled observation joined by generation/thread/swapchain/CPU receipt; partial groups are rejected.',
                'Freshness is native-reported age in MONOTONIC_RAW ns, never CPU QPC subtraction.',
                'Repeated layer/sequence identities must preserve observed time/window/display/occlusion/flags; age/status may change.',
                'Window visible and active Space are sampled flags; no interpolation, gameplay or presentation proof.',
                'Only independent valid positive native drawable timestamps can support displayed cadence.'])
