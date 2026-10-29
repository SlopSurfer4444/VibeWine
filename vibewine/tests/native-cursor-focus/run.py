#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Exercise actual cursor admission and public callback bodies without UI."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def body(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-ref', default='506004b729856034a7d280f0ab505a8b3cfe54d6')
    parser.add_argument('--translated', action='store_true')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    root = here.parents[2]
    app_path, window_path, clipping_path = ('dlls/winemac.drv/' + p for p in ('cocoa_app.m', 'cocoa_window.m', 'cocoa_cursorclipping.m'))
    expected = {'key-loss-releases-before-event', 'queued-request-after-key-loss',
                'focus-callback-does-not-grab', 'focus-return-reacquires', 'inactive-request',
                'keyless-request', 'native-key-request', 'hidden-request', 'minimized-request',
                'off-space-request', 'closing-request', 'space-change-releases',
                'space-change-stale-request', 'space-return-does-not-grab',
                'space-return-reacquires', 'same-rect-inactive-releases',
                'permission-exception-ended-releases', 'confinement-key-owner',
                'internal-switch-same-rect-rebind'}
    results = []
    with tempfile.TemporaryDirectory(prefix='vibewine-native-cursor-focus-') as directory:
        work = Path(directory)
        for generation in ('baseline', 'candidate'):
            def read(path):
                return ((root / path).read_text() if generation == 'candidate' else
                        subprocess.check_output(['git', '-C', str(root), 'show', args.baseline_ref + ':' + path], text=True))
            app, window_source = read(app_path), read(window_path)
            clipping = read(clipping_path)
            confinement = clipping[clipping.index('@implementation WineConfinementClipCursorHandler'):]
            methods = ['- (BOOL) startClippingCursor:(CGRect)rect', '- (BOOL) stopClippingCursor',
                       '- (BOOL) clippingCursor', '- (void) updateWindowsForCursorClipping',
                       '- (void) activeSpaceDidChange']
            if '- (BOOL) canClipCursor\n' in app:
                methods.append('- (BOOL) canClipCursor\n')
            fixture = (here / 'fixture.m.in').read_text().replace('/* APP_METHODS */', '\n\n'.join(body(app, s) for s in methods))
            fixture = fixture.replace('/* WINDOW_METHODS */', '\n\n'.join(body(window_source, s) for s in (
                '- (void)windowDidResignKey:(NSNotification *)notification',
                '- (void)windowDidBecomeKey:(NSNotification *)notification')))
            confinement_methods = ['- (BOOL) startClippingCursor:(CGRect)rect', '- (BOOL) stopClippingCursor']
            if '- (WineWindow*) currentClippingWindow' in confinement:
                confinement_methods += ['- (WineWindow*) currentClippingWindow', '- (BOOL) isClippingToCurrentWindow']
            fixture = fixture.replace('/* CONFINEMENT_METHODS */', '\n\n'.join(body(confinement, s) for s in confinement_methods))
            src, exe = work / (generation + '.m'), work / generation
            src.write_text(fixture)
            command = ['clang', '-std=gnu11', '-fno-objc-arc', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                       '-Wno-unused-parameter', '-Wno-incomplete-implementation',
                       '-fsanitize=address,undefined', '-framework', 'Foundation', '-framework', 'CoreGraphics',
                       str(src), '-o', str(exe)]
            if args.translated:
                command[1:1] = ['-arch', 'x86_64']
            built = subprocess.run(command, capture_output=True, text=True, timeout=60)
            if built.returncode:
                raise RuntimeError(built.stderr)
            command = [str(exe)]
            if args.translated:
                command[:0] = ['arch', '-x86_64']
            run = subprocess.run(command, capture_output=True, text=True, timeout=25)
            failures = [line[5:] for line in run.stdout.splitlines() if line.startswith('FAIL ')]
            valid = (run.returncode == 0 and not failures) if generation == 'candidate' else (
                run.returncode == 42 and set(failures) == expected and len(failures) == len(expected))
            if not valid or run.stderr or 'NATIVE_CURSOR_FOCUS_CHECKS=' not in run.stdout:
                raise RuntimeError(f'{generation}: unexpected result {run.returncode}\n{run.stdout}\n{run.stderr}')
            results.append({'generation': generation, 'exit': run.returncode,
                            'app_sha256': hashlib.sha256(app.encode()).hexdigest(),
                            'window_sha256': hashlib.sha256(window_source.encode()).hexdigest(),
                            'clipping_sha256': hashlib.sha256(clipping.encode()).hexdigest(),
                            'fixture_sha256': hashlib.sha256(fixture.encode()).hexdigest(),
                            'stdout': run.stdout, 'stderr': run.stderr})
    print(json.dumps({'result': 'PASS', 'translated': args.translated, 'baseline_ref': args.baseline_ref,
                      'checks': results, 'limits': 'Actual production method bodies; Foundation fakes for application/window/clip handler. '
                      'No NSApplication, native cursor operation, callback delivery, Mission Control gesture, Wine runtime or game.'}, indent=2))


if __name__ == '__main__':
    main()
