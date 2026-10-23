#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Check complete production level methods against native, nonactivating AppKit panels.

No Wine runtime or installed app is started. Temporary sources/binaries are removed.
The fixture never activates, captures displays, enters fullscreen, or changes Spaces.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import tempfile


def method(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-ref', default='d2237a9a82d9960360ce8400242ddeae6646c347')
    args = parser.parse_args()
    if platform.system() != 'Darwin':
        parser.error('requires macOS AppKit / WindowServer')
    here = Path(__file__).resolve().parent
    root = here.parents[2]
    app_path, window_path = 'dlls/winemac.drv/cocoa_app.m', 'dlls/winemac.drv/cocoa_window.m'
    candidate = (root / app_path).read_text()
    baseline = subprocess.check_output(['git', '-C', str(root), 'show', args.baseline_ref + ':' + app_path], text=True)
    window_candidate = (root / window_path).read_text()
    window_baseline = subprocess.check_output(['git', '-C', str(root), 'show', args.baseline_ref + ':' + window_path], text=True)
    fixture = (here / 'fixture.m.in').read_text()
    results = []
    with tempfile.TemporaryDirectory(prefix='vibewine-native-window-levels-') as directory:
        work = Path(directory)
        for generation, app, window in [('baseline', baseline, window_baseline), ('candidate', candidate, window_candidate)]:
            minimum = method(window, '- (NSInteger) minimumLevelForActive:(BOOL)active')
            methods = '\n\n'.join(method(app, signature) for signature in (
                '- (void) adjustWindowLevels:(BOOL)active',
                '- (void) adjustWindowLevels\n',
                '- (void)applicationWillResignActive:(NSNotification *)notification',
                '- (void)applicationDidBecomeActive:(NSNotification *)notification'))
            code = fixture.replace('/* PRODUCTION_MINIMUM */', minimum).replace('/* PRODUCTION_CONTROLLER */', methods)
            # Retain original notices for every extracted production body.
            for original in (app, window):
                code = original[:original.index('*/') + 2] + '\n' + code
            source, exe = work / (generation + '.m'), work / generation
            source.write_text(code)
            command = ['clang', '-std=gnu11', '-fblocks', '-fno-objc-arc', '-O1', '-g', '-Wall', '-Wextra',
                       '-Wno-unused-parameter', '-framework', 'AppKit', '-framework', 'CoreGraphics',
                       str(source), '-o', str(exe)]
            built = subprocess.run(command, capture_output=True, text=True, timeout=60)
            if built.returncode:
                raise RuntimeError(built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=25)
            lines = run.stdout.splitlines()
            failures = [line for line in lines if line.startswith('FAIL ')]
            if generation == 'candidate':
                valid = run.returncode == 0 and not failures and 'NATIVE_WINDOW_LEVELS_PASS' in lines
            else:
                expected = {
                    'FAIL inactive-below-dock-' + case for case in
                    ('windowed', 'normal-fullscreen', 'floating-fullscreen', 'captured-state',
                     'sentinel-order', 'restored-fullscreen')}
                expected |= {'FAIL active-floating-parent-native-order', 'FAIL inactive-floating-parent-native-order'}
                valid = (run.returncode == 42 and set(failures) == expected
                         and len(failures) == len(expected)
                         and 'NATIVE_WINDOW_LEVELS_EXPECTED_BASELINE_FAILURES=8' in lines)
            if not valid:
                raise RuntimeError(f'{generation}: unexpected result {run.returncode}\n{run.stdout}\n{run.stderr}')
            results.append({'generation': generation, 'exit': run.returncode,
                            'generated_source_sha256': hashlib.sha256(code.encode()).hexdigest(),
                            'observations': lines, 'stderr': run.stderr})
    print(json.dumps({'result': 'PASS', 'baseline_ref': args.baseline_ref, 'production_methods': 'unmodified source extraction',
                      'checks': results, 'limits': 'Actual native panel level/order checks with production methods; '
                      'activation/fullscreen/capture state injected, no actual focus, Space, capture, Dock click, '
                      'installed runtime, gameplay or performance acceptance.'}, indent=2))


if __name__ == '__main__':
    main()
