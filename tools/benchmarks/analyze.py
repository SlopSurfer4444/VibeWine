#!/usr/bin/env python3
"""CPU loop metrics and separately qualified native feedback; no FPS substitution."""
import argparse
import csv
import importlib.util
import json
import math
from pathlib import Path
import statistics
import sys


def distribution(values):
    if not values or any(not math.isfinite(x) or x < 0 for x in values):
        raise ValueError('nonempty finite nonnegative timing distribution required')
    ordered = sorted(values)
    def quantile(fraction):
        return ordered[max(0, math.ceil(len(ordered) * fraction) - 1)]
    return dict(count=len(values), meanMs=statistics.fmean(values), medianMs=statistics.median(values),
                p95Ms=quantile(.95), p99Ms=quantile(.99), maximumMs=ordered[-1])


def cpu_frames(path, expected_frames=None):
    if path.is_symlink() or path.stat().st_size > 4*1024*1024:
        raise ValueError('CPU frame input cap/type')
    with path.open(newline='') as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != ['frame', 'cpuFrameMs', 'presentCallMs']:
            raise ValueError('unexpected CPU frame columns')
        rows = []
        for row in reader:
            if len(rows) >= 60000 or int(row['frame']) != len(rows):
                raise ValueError('CPU frame sequence/cap')
            frame, present = float(row['cpuFrameMs']), float(row['presentCallMs'])
            if not (math.isfinite(frame) and math.isfinite(present) and 0 <= present <= frame):
                raise ValueError('invalid CPU frame timing')
            rows.append((frame, present))
    if expected_frames is not None and len(rows) != expected_frames:
        raise ValueError('CPU frame count differs from guest result')
    return dict(scope='guest measured loop only; CPU wall time includes Present blocking; not GPU time or displayed FPS',
                frame=distribution([x[0] for x in rows]),
                presentCall=distribution([x[1] for x in rows]))


def cadence_status(cadence):
    observed = any(type(row.get('observed_interval_rate_hz')) in (int, float) and
                   math.isfinite(row['observed_interval_rate_hz']) and row['observed_interval_rate_hz'] > 0 and
                   row.get('stream_state') == 'observed' and row.get('intervals') is not None
                   for row in cadence)
    return 'observed-native-cadence' if observed else 'inconclusive-no-qualified-native-cadence'


def native_trace(path, dimensions=None, *, expected_profile='combined-bounded'):
    if expected_profile not in ('presentation-waits', 'combined-bounded'):
        raise ValueError('explicit supported expected profile required')
    if path.is_symlink() or path.stat().st_size > 24*1024*1024:
        raise ValueError('decoded trace cap/type')
    rows = []
    with path.open() as stream:
        for line in stream:
            if len(line) > 8192 or len(rows) >= 131073:
                raise ValueError('decoded trace row cap')
            rows.append(json.loads(line))
    if not rows or rows[-1].get('kind') != 'summary':
        raise ValueError('missing decoder summary')
    summary = rows[-1]
    failures = []
    for key in ('structurally_complete', 'producer_quiescence_verified'):
        if summary.get(key) is not True:
            failures.append(key)
    for key in ('dropped_before', 'dropped_after', 'uncommitted_reserved', 'boundary_rejected_total'):
        if type(summary.get(key)) is not int or summary[key] != 0:
            failures.append(key)
    if summary.get('counters_changed') is not False or summary.get('inconclusive_window') is not False:
        failures.append('decoder_inconclusive_or_counters_changed')
    actual_events = [row for row in rows[:-1] if row.get('kind') == 'event' and row.get('type') != 95]
    if not actual_events:
        failures.append('no_actual_events')
    if summary.get('events') != len(rows)-1:
        failures.append('decoder_event_count')
    telemetry = Path(__file__).resolve().parents[1] / 'telemetry'
    sys.path.insert(0, str(telemetry))
    try:
        spec = importlib.util.spec_from_file_location('_benchmark_maintained_trace_analyzer', telemetry / 'analyze.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        revisions = {row['padding1'] for row in rows[:-1] if row.get('type') == 72}
        revision = next(iter(revisions)) if len(revisions) == 1 else None
        result = module.analyze(rows, expected_dimensions=dimensions, expected_revision=revision)
    finally:
        sys.path.pop(0)
    structural_failures = list(failures)
    profile = result['producer_profile']
    if profile.get('name') != expected_profile or profile.get('metadata_observed') is not True:
        failures.append('expected_explicit_requested_profile_metadata')
    if expected_profile == 'combined-bounded':
        if not profile['capture_usable']:
            failures.append('producer_profile_unusable')
    else:
        # Presentation-only metadata can be policy-valid while its strict native
        # whole-envelope coverage is incomplete. Admit observed CPU/GPU metrics
        # separately; never promote the original native/profile acceptance flag.
        if profile.get('policy_valid') is not True:
            failures.append('producer_profile_policy_invalid')
        if result['detailed_probes'].get('capture_usable') is not True:
            failures.append('detailed_capture_unusable')
        if any(result['rejections'].values()):
            failures.append('rejected_trace_records')
        if result['presentation_capture'].get('observed_contradictions') != []:
            failures.append('contradictory_presentation_observations')
        if not any(row.get('type') == 40 and row.get('object') and row.get('thread') and
                   row['end'] >= row['start'] for row in actual_events):
            failures.append('no_actual_cpu_present_records')
        gpu = result['queue_chunk_metrics'].get('gpu_command_buffer_durations')
        if not gpu or not gpu.get('samples') or not result['queue_chunk_metrics']['successful_completed_chunks']:
            failures.append('no_accepted_gpu_command_buffer_samples')
    # Keep source analyzer's restrictions and explicit missing coverage. This capture
    # includes calibration/tails and is never relabelled an exact measured window.
    wave7 = result['wave7']
    cadence = wave7['native_presented_cadence']
    coverage = result['presentation_capture']
    compact_coverage = {key: value for key, value in coverage.items()
                        if key not in ('joined_frames', 'completion_finish_observations', 'swapchain_admission_waits')}
    compact_coverage['joinedFrameCount'] = len(coverage.get('joined_frames', []))
    return dict(usable=not failures and profile['capture_usable'], observationUsable=not failures, failures=failures, events=len(actual_events),
                structurallyUsable=not structural_failures, structuralFailures=structural_failures,
                generation=summary['generation'], identity=summary['identity'], requestedProfile=expected_profile,
                producerProfile=result['producer_profile'],
                captureCoverage=dict(requestedProfile=expected_profile, observedProfile=profile.get('name'),
                    metadataObserved=profile.get('metadata_observed') is True,
                    fullNativePresentationAccepted=coverage['capture_usable'],
                    scope=('CPU Present, native presentation observations, GPU command buffers and queue/wait summaries; per-Map/shader/scheduler detail excluded'
                           if expected_profile == 'presentation-waits' else
                           'combined ordinary/presentation observations plus capped compiler/scheduler/long-wait detail; lifetime capacity unchanged')),
                queueChunkMetrics=result['queue_chunk_metrics'],
                cpuPresentStreams=result['cpu_present_streams'],
                queueChunkMetricsScope='observed completed command buffers: GPU durations use native GPU clock; submission/encode/completion waits use CPU steady clock; milliseconds; overlapping GPU durations are not summed busy time or full-frame GPU time',
                nativePresentation=dict(scope='decoded generation envelope including start/shutdown tails; not exact guest CPU measurement window',
                    status=cadence_status(cadence),
                    zeroPresentedTimeObservations=sum(row.get('type') == 71 and row.get('padding1') == 0 for row in rows[:-1]),
                    nonpositiveOrInvalidTimestampObservations=result['rejections'].get('invalid_native_presented_timestamp_or_domain_flags', 0),
                    cadence=cadence, strictAcceptance=wave7['strict_acceptance'],
                    feedbackFailures=wave7['native_feedback_failures'],
                    configurationFailures=wave7['native_configuration_apply_failures'],
                    limits=wave7['limits']),
                presentationCoverage=compact_coverage, rejections=result['rejections'],
                quality=result['quality_limits'])


def analyze_run(directory, guest_result=None, *, expected_profile=None):
    directory = Path(directory)
    declared_profile = None
    declaration = directory / 'run.json'
    if declaration.exists():
        if declaration.is_symlink() or declaration.stat().st_size > 1024*1024:
            raise ValueError('run declaration cap/type')
        declared_profile = json.loads(declaration.read_text()).get('requestedProfile')
    if expected_profile is None:
        expected_profile = declared_profile
    if expected_profile not in ('presentation-waits', 'combined-bounded'):
        raise ValueError('explicit requested profile required; supply --profile for historical runs')
    if declared_profile is not None and declared_profile != expected_profile:
        raise ValueError('requested profile differs from recorded declaration')
    if guest_result is None:
        guest_result = json.loads((directory / 'results.json').read_text())
    if guest_result.get('status') != 'complete':
        raise ValueError('guest did not complete')
    dimensions = dict(width=guest_result['width'], height=guest_result['height'])
    return dict(cpu=cpu_frames(directory / 'perframe.csv', guest_result['frames']),
                trace=native_trace(directory / 'trace.ndjson', dimensions, expected_profile=expected_profile),
                limitations=['One synthetic scene run does not establish DS3 performance or a speedup.',
                             'CPU timings and native feedback have separate windows and clocks.',
                             'Run repeated unchanged A/A before A/B; account for cache/power/observer changes.'])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory', type=Path)
    p.add_argument('--profile', choices=('presentation-waits', 'combined-bounded'))
    args = p.parse_args()
    result = analyze_run(args.directory, expected_profile=args.profile)
    print(json.dumps(result, indent=2, allow_nan=False))
    return 0 if result['trace']['observationUsable'] else 1


if __name__ == '__main__':
    sys.exit(main())
