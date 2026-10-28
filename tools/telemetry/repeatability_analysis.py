"""Descriptive A/A and A/B admission over freshly recomputed fixed windows.

Run metadata is caller-supplied evidence, never installed-module verification.
No launch, collector, GUI, cache mutation, or performance verdict is supplied.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import statistics

from fixed_window_analysis import analyze_fixed_window

CONTROLS = ('scene', 'settings', 'cacheTreatment', 'power', 'background', 'observer', 'route')
BINDINGS = ('hubRevision', 'wineRevision', 'dxmtRevision', 'buildReceiptSha256',
            'installedManifestSha256', 'loadedModules')


def require(ok, message):
    if not ok:
        raise ValueError(message)


def text(value):
    return type(value) is str and 0 < len(value.encode()) <= 4096


def binding(value):
    require(type(value) is dict and set(value) == set(BINDINGS), 'exact source/build/module binding required')
    for key in BINDINGS[:-1]:
        require(type(value[key]) is str and re.fullmatch('[0-9a-f]{40}' if key.endswith('Revision') else '[0-9a-f]{64}', value[key]), 'invalid ' + key)
    modules = value['loadedModules']
    require(type(modules) is dict and 1 <= len(modules) <= 32, 'bounded loaded-module hash map required')
    require(all(text(k) and type(v) is str and re.fullmatch('[0-9a-f]{64}', v) for k, v in modules.items()), 'invalid loaded-module binding')
    return value


def compare(runs):
    """Each window must be the maintained analyzer's result, recomputed by CLI."""
    require(type(runs) is list and 4 <= len(runs) <= 32, '4..32 ordered runs required')
    reasons, identities, measurements, candidates, groups = [], set(), set(), {}, {'A': [], 'B': []}
    controls = None
    collector = None
    captures = set()
    geometry = None
    def reject(reason):
        if reason not in reasons:
            reasons.append(reason)
    for run in runs:
        require(type(run) is dict and run.get('condition') in groups and text(run.get('id')), 'run identity/condition required')
        require(run['id'] not in identities, 'duplicate run ID')
        identities.add(run['id'])
        require(text(run.get('processKey')) and text(run.get('normalExitEvidence')) and text(run.get('relaunchEvidence')), 'process generation and normal-exit/relaunch evidence required')
        actual = binding(run.get('binding'))
        condition = run['condition']
        if condition in candidates and candidates[condition] != actual:
            reject('candidate_binding_changed_within_' + condition)
        candidates[condition] = actual
        declared = run.get('controls')
        require(type(declared) is dict and set(declared) == set(CONTROLS) and all(text(v) for v in declared.values()), 'complete bounded controls required')
        if controls is not None and controls != declared:
            reject('scene_settings_cache_power_background_observer_or_route_changed')
        controls = declared
        window = run['window']
        require(type(window) is dict and window.get('schema') == 1, 'recomputed fixed-window result required')
        actual_collector = window.get('collector_sha256')
        require(type(actual_collector) is str and re.fullmatch('[0-9a-f]{64}', actual_collector), 'actual collector SHA256 required')
        if collector is not None and collector != actual_collector:
            reject('actual_collector_changed')
        collector = actual_collector
        capture_key = (window.get('decoder_identity'), window.get('generation'))
        require(text(capture_key[0]) and type(capture_key[1]) is int, 'decoder identity/generation required')
        if capture_key in captures:
            reject('decoder_identity_and_generation_reused')
        captures.add(capture_key)
        mid = window.get('measurement_id')
        require(text(mid) and mid not in measurements, 'fresh unique measurement identity required')
        measurements.add(mid)
        if window.get('accepted') is not True or window.get('scenario') != 'stationary':
            reject('rejected_or_nonstationary_window:' + run['id'])
        expected = window.get('expected', {})
        # Swapchain birth and revision are process-local identities. Each was
        # already verified against its own capture; do not compare their serials
        # across fresh game generations.
        shape = (window.get('window', {}).get('duration_seconds'), expected.get('dimensions'))
        if geometry is not None and geometry != shape:
            reject('window_duration_or_geometry_changed')
        geometry = shape
        rate = window.get('presentations_per_fixed_second')
        if type(rate) not in (int, float) or not math.isfinite(rate) or rate < 0:
            reject('missing_finite_rate:' + run['id'])
        groups[condition].append(rate)
    for condition in groups:
        if len(groups[condition]) < 2:
            reject('at_least_two_' + condition + '_runs_required')
    if candidates.get('A') == candidates.get('B'):
        reject('A_and_B_have_identical_candidate_bindings')
    # A single process may supply several windows, but these must not masquerade
    # as independent restart repeats for the A/A lifecycle acceptance.
    process_keys = [r['processKey'] for r in runs]
    if len(set(process_keys)) != len(process_keys):
        reject('process_generation_reused_across_restart_runs')
    result = dict(schema=1, comparable=not reasons, rejection_reasons=reasons,
                  run_order=[dict(id=r['id'], condition=r['condition'], processKey=r['processKey'], measurement_id=r['window']['measurement_id'],
                                  interiorIntervalDistribution=r['window'].get('interior_interval_distribution')) for r in runs],
                  controls=controls, bindings=candidates, collectorSha256=collector,
                  attributionStatus='descriptive-only-no-causal-or-performance-verdict',
                  limits=['Metadata and exit/scene/module evidence are caller declarations requiring independent owner review.',
                          'Fixed windows are reanalyzed from original decoded capture and clock samples by the CLI.',
                          'A/A spread is observed repeat variation, not a statistical confidence interval.',
                          'Actual drawable presentation feedback is not physical scanout or full latency.',
                          'Observer-disabled overhead and per-shader GPU cost are not supplied.'])
    if not reasons:
        summaries = {k: dict(runs=len(v), median=statistics.median(v), minimum=min(v), maximum=max(v), spread=max(v)-min(v)) for k, v in groups.items()}
        delta = summaries['B']['median'] - summaries['A']['median']
        noise = max(summaries['A']['spread'], summaries['B']['spread'])
        result.update(presentationsPerFixedSecond=summaries, medianDeltaBMinusA=delta,
                      observedRepeatSpread=noise, deltaExceedsObservedRepeatSpread=abs(delta) > noise)
    return result


def unique(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'duplicate JSON key')
        result[key] = value
    return result


def load(path, cap):
    path = Path(path)
    require(path.is_absolute() and path.resolve() == path and path.is_file() and path.stat().st_nlink == 1, 'canonical regular single-link input required')
    before = path.stat()
    with path.open('rb') as stream:
        raw = stream.read(cap + 1)
    after = path.stat()
    require(all(getattr(before, k) == getattr(after, k) for k in ('st_dev', 'st_ino', 'st_size', 'st_mtime_ns', 'st_ctime_ns')), 'input changed while reading')
    require(len(raw) <= cap, 'input byte cap')
    return raw


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    def decode(raw):
        return json.loads(raw, object_pairs_hook=unique, parse_constant=lambda _: (_ for _ in ()).throw(ValueError('nonfinite JSON')))
    try:
        raw = load(args.manifest, 1024 * 1024)
        manifest = decode(raw)
        require(type(manifest) is dict and set(manifest) == {'schema', 'runs'} and manifest['schema'] == 1, 'comparison manifest schema')
        require(type(manifest['runs']) is list and 4 <= len(manifest['runs']) <= 32, 'bounded ordered runs required')
        inputs = {str(args.manifest): hashlib.sha256(raw).hexdigest()}
        runs = []
        for source in manifest['runs']:
            run = dict(source)
            files = run.pop('inputs')
            require(type(files) is dict and set(files) == {'capture', 'begin', 'end'}, 'capture/begin/end input paths required')
            payload = {}
            for key, path in files.items():
                data = load(path, 64 * 1024 * 1024 if key == 'capture' else 8192)
                inputs[path] = hashlib.sha256(data).hexdigest()
                if key == 'capture':
                    lines = data.splitlines()
                    require(len(lines) <= 131073 and all(len(line) <= 8192 for line in lines), 'decoded record byte/count cap')
                    payload[key] = [decode(line) for line in lines]
                else:
                    payload[key] = decode(data)
            run['window'] = analyze_fixed_window(payload['capture'], payload['begin'], payload['end'], **run.pop('windowArguments'))
            runs.append(run)
        result = compare(runs)
        result['inputSha256'] = inputs
        output = json.dumps(result, indent=2, allow_nan=False) + '\n'
        require(len(output.encode()) <= 1024 * 1024, 'comparison output cap')
        require(str(args.output.resolve()) not in inputs, 'output would overwrite immutable input')
        # No replacement of an existing result or symlink, including on rerun.
        with args.output.open('x') as stream:
            stream.write(output)
        return 0 if result['comparable'] else 3
    except (ValueError, KeyError, TypeError, OSError, RecursionError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    raise SystemExit(main())
