#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Real production cursor-warp methods; offline CPU seams, no native UI."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-ref', default='b6d49057b9f473eb0a903b80f6942e0594afa514')
    parser.add_argument('--translated', action='store_true')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    root = here.parents[2]
    spec = importlib.util.spec_from_file_location('cursor_focus', here.parent / 'native-cursor-focus/run.py')
    extract = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(extract)
    path = 'dlls/winemac.drv/cocoa_app.m'
    candidate = (root / path).read_text()
    env_method = extract.body(candidate, 'static BOOL foreground_cursor_warp_enabled(void)')
    if candidate.count('foregroundCursorWarp = foreground_cursor_warp_enabled();') != 1:
        raise RuntimeError('production initialization no longer binds the tested policy parser')
    expected = {prefix + suffix for prefix in ('inactive-', 'keyless-', 'native-key-', 'hidden-', 'mini-', 'space-', 'closing-', 'stale-', 'stop-failure-', 'return-inactive-') for suffix in ('direct', 'tap')}
    expected.add('internal-selector-veto')
    results = []
    with tempfile.TemporaryDirectory(prefix='vibewine-cursor-warp-') as directory:
        work = Path(directory)
        for generation in ('baseline', 'candidate'):
            app = candidate if generation == 'candidate' else subprocess.check_output(['git', '-C', str(root), 'show', args.baseline_ref + ':' + path], text=True)
            methods = ('- (BOOL) setCursorPosition:(CGPoint)pos\n', '- (BOOL) canClipCursor\n', '- (BOOL) clippingCursor', '- (BOOL) stopClippingCursor', '- (void) updateWindowsForCursorClipping')
            if generation == 'candidate':
                methods = ('- (BOOL) setCursorPosition:(CGPoint)pos restricted:(BOOL*)restricted',) + methods
            mouse_path = 'dlls/winemac.drv/mouse.c'
            mouse = (root / mouse_path).read_text() if generation == 'candidate' else subprocess.check_output(['git', '-C', str(root), 'show', args.baseline_ref + ':' + mouse_path], text=True)
            fixture = (here / 'fixture.m.in').read_text().replace('/* ENV_METHOD */', env_method).replace('/* APP_METHODS */', '\n\n'.join(extract.body(app, s) for s in methods))
            fixture = fixture.replace('/* POLICY_DECLARATION */', '- (BOOL)setCursorPosition:(CGPoint)position restricted:(BOOL*)restricted;' if generation == 'candidate' else '')
            fixture = fixture.replace('/* C_WRAPPERS */', extract.body(app, 'int macdrv_set_cursor_position(CGPoint pos)') + '\n\n' + extract.body(mouse, 'BOOL macdrv_SetCursorPos(INT x, INT y)'))
            src, exe = work / (generation + '.m'), work / generation
            src.write_text(fixture)
            command = ['clang', '-std=gnu11', '-fno-objc-arc', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-fblocks', '-fsanitize=address,undefined', '-framework', 'Foundation', '-framework', 'CoreGraphics', str(src), '-o', str(exe)]
            if args.translated:
                command[1:1] = ['-arch', 'x86_64']
            built = subprocess.run(command, capture_output=True, text=True, timeout=60)
            if built.returncode:
                raise RuntimeError(built.stderr)
            for value in (None, '', '0', 'true', '01', '1'):
                env = dict(os.environ)
                env.pop('VIBEWINE_FOREGROUND_CURSOR_WARP', None)
                if value is not None:
                    env['VIBEWINE_FOREGROUND_CURSOR_WARP'] = value
                command = [str(exe)]
                if args.translated:
                    command[:0] = ['arch', '-x86_64']
                run = subprocess.run(command, env=env, capture_output=True, text=True, timeout=25)
                failures = [line[5:] for line in run.stdout.splitlines() if line.startswith('FAIL ')]
                old_fail = generation == 'baseline' and value == '1'
                valid = (run.returncode == 42 and set(failures) == expected | {'stale-release'} and len(failures) == len(expected) + 2) if old_fail else run.returncode == 0 and not failures
                if not valid or run.stderr or 'CURSOR_WARP_CHECKS=36 ' not in run.stdout or f'POLICY={int(value == "1")}' not in run.stdout:
                    raise RuntimeError(f'{generation} env={value!r}: unexpected result {run.returncode}\n{run.stdout}\n{run.stderr}')
                results.append({'generation': generation, 'env': value, 'exit': run.returncode, 'stdout': run.stdout, 'stderr': run.stderr, 'production_sha256': hashlib.sha256(app.encode()).hexdigest(), 'mouse_sha256': hashlib.sha256(mouse.encode()).hexdigest(), 'fixture_sha256': hashlib.sha256(fixture.encode()).hexdigest()})
    print(json.dumps({'result': 'PASS', 'translated': args.translated, 'baseline_ref': args.baseline_ref, 'runs': results, 'limits': 'Actual production cursor-warp, eligibility, clipping getter/release/update bodies. Cursor primitives, application/window state and queues are seams. No AppKit callback delivery, native window/cursor, Wine runtime, gesture or game.'}, indent=2))


if __name__ == '__main__':
    main()
