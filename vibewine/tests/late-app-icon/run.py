#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Exercise production icon conversion and async entry without creating an application."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
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
    parser.add_argument('--baseline-ref', default='5d4b43e48f48d83254468596a0df8592d62e3242')
    parser.add_argument('--translated', action='store_true')
    args = parser.parse_args()
    if platform.system() != 'Darwin':
        parser.error('requires macOS AppKit and CoreGraphics')
    here = Path(__file__).resolve().parent
    root = here.parents[2]
    path = 'dlls/winemac.drv/cocoa_app.m'
    candidate = (root / path).read_text()
    baseline = subprocess.check_output(['git', '-C', str(root), 'show', args.baseline_ref + ':' + path], text=True)
    expected = {'late-cgimage-icon', 'late-url-icon', 'empty-restores-default',
                'masked-icon-preserved', 'policy-at-execution-promoted'}
    results = []
    with tempfile.TemporaryDirectory(prefix='vibewine-late-app-icon-') as directory:
        work = Path(directory)
        for generation, source in [('baseline', baseline), ('candidate', candidate)]:
            fixture = (here / 'fixture.m.in').read_text()
            fixture = fixture.replace('/* PRODUCTION_METHOD */', body(source, '- (void) setApplicationIconFromCGImageArray:(NSArray*)images'))
            fixture = fixture.replace('/* PRODUCTION_ENTRY */', body(source, 'void macdrv_set_application_icon(CFArrayRef images, CFURLRef urlRef)'))
            fixture = source[:source.index('*/') + 2] + '\n' + fixture
            src, exe = work / (generation + '.m'), work / generation
            src.write_text(fixture)
            command = ['clang', '-std=gnu11', '-fblocks', '-fno-objc-arc', '-O1', '-g',
                       '-Wall', '-Wextra', '-Wno-unused-parameter', '-fsanitize=address,undefined',
                       '-framework', 'AppKit', '-framework', 'CoreGraphics', str(src), '-o', str(exe)]
            if args.translated:
                command[1:1] = ['-arch', 'x86_64']
            built = subprocess.run(command, capture_output=True, text=True, timeout=60)
            if built.returncode:
                raise RuntimeError(built.stderr)
            command = [str(exe), str(work / (generation + '.tiff'))]
            if args.translated:
                command[:0] = ['arch', '-x86_64']
            run = subprocess.run(command, capture_output=True, text=True, timeout=25)
            failures = [line[5:] for line in run.stdout.splitlines() if line.startswith('FAIL ')]
            valid = (run.returncode == 0 and not failures) if generation == 'candidate' else (
                run.returncode == 42 and set(failures) == expected and len(failures) == len(expected))
            if not valid or run.stderr or 'LATE_APP_ICON_CHECKS=' not in run.stdout:
                raise RuntimeError(f'{generation}: unexpected result {run.returncode}\n{run.stdout}\n{run.stderr}')
            results.append({'generation': generation, 'exit': run.returncode,
                            'source_sha256': hashlib.sha256(fixture.encode()).hexdigest(),
                            'stdout': run.stdout, 'stderr': run.stderr})
    print(json.dumps({'result': 'PASS', 'translated': args.translated, 'baseline_ref': args.baseline_ref,
                      'checks': results, 'limits': 'Real icon objects; GCD substituted for CFRunLoop request transport; observing application seam. '
                      'No NSApplication, Dock observation, focus, Wine runtime or gameplay acceptance.'}, indent=2))


if __name__ == '__main__':
    main()
