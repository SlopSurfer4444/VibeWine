#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Exercise extracted Metal-view lifecycle bodies against real hidden AppKit views."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SOURCE = 'dlls/winemac.drv/cocoa_window.m'
BASELINE = 'd2237a9a82d9960360ce8400242ddeae6646c347'
EXPECTED_BASELINE_FAILURES = {
    'earlier HUD-like block retains window association',
    'terminal disposal preserves prior callback hierarchy',
    'retired generation survives subsequent container disposal',
}
EXPECTED_MUTATION_FAILURES = {
    'reject retired-container acquisition after terminal disposal',
}
SANITIZER_DIAGNOSTIC = re.compile(
    r'AddressSanitizer|UndefinedBehaviorSanitizer|LeakSanitizer|MemorySanitizer|'
    r'ThreadSanitizer|runtime error:', re.IGNORECASE)


def body(source, marker, optional=False):
    if marker not in source:
        if optional:
            return ''
        raise RuntimeError('Production extraction marker missing: ' + marker)
    start = source.index(marker)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', action='store_true', help='expect the three exact pre-fix failures')
    parser.add_argument('--mutation', choices=('missing-terminal-guard',), help='require the native fixture to detect an intentionally removed lifecycle guard')
    parser.add_argument('--arch', choices=('arm64', 'x86_64'), help='optional macOS compile/run architecture')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        raise SystemExit('This check needs macOS AppKit, QuartzCore and an available WindowServer session.')
    source = subprocess.check_output(['git', 'show', BASELINE + ':' + SOURCE], cwd=ROOT, text=True) if args.baseline else (ROOT / SOURCE).read_text()
    if args.baseline and args.mutation:
        parser.error('baseline and mutation are independent controls')
    if args.mutation:
        old = 'if (_disposing || _metalView || !device'
        if source.count(old) != 1:
            raise SystemExit('Terminal guard mutation seam changed')
        source = source.replace(old, 'if (_metalView || !device', 1)
    markers = {
        'ACQUIRE': '    - (WineMetalView*) newMetalViewWithDevice:(id<MTLDevice>)device\n    {',
        'RETIRE': '    - (void) retireMetalView:(WineMetalView*)view\n    {',
        'PREPARE': '    - (BOOL) prepareForDisposal\n    {',
        'WILL_REMOVE': '    - (void) willRemoveSubview:(NSView*)subview\n    {',
        'RELEASE': 'void macdrv_view_release_metal_view(macdrv_metal_view v)',
        'DISPOSE': 'void macdrv_dispose_view(macdrv_view v)',
    }
    license_header = source[:source.index('*/') + 2]
    if not license_header.startswith('/*') or 'Copyright' not in license_header or 'Lesser General Public' not in license_header:
        raise SystemExit('Production copyright/license header changed')
    fixture = license_header + '\n#define CANDIDATE ' + str(int(not args.baseline)) + '\n' + (HERE / 'fixture.m.in').read_text()
    for key, marker in markers.items():
        fixture = fixture.replace('/* @' + key + '@ */', body(source, marker, optional=args.baseline and key in ('RETIRE', 'PREPARE')))
    with tempfile.TemporaryDirectory(prefix='vibewine-metal-view-') as directory:
        path = Path(directory)
        (path / 'fixture.m').write_text(fixture)
        command = ['/usr/bin/clang', '-fblocks', '-fno-objc-arc', '-g', '-O1', '-fsanitize=address,undefined', '-Wno-deprecated-declarations', '-Wno-incomplete-implementation', '-framework', 'AppKit', '-framework', 'QuartzCore']
        if args.arch:
            command += ['-arch', args.arch]
        command += [str(path / 'fixture.m'), '-o', str(path / 'fixture')]
        subprocess.run(command, check=True, timeout=60)
        result = subprocess.run([str(path / 'fixture')], text=True, capture_output=True, timeout=15)
        print(result.stdout, end='')
        output = result.stdout + result.stderr
        failure_lines = [line.removeprefix('FAIL ') for line in output.splitlines() if line.startswith('FAIL ')]
        failures = set(failure_lines)
        # Framework service diagnostics do not prove a drawable/GPU exercise.
        if result.stderr:
            print(result.stderr, end='', file=sys.stderr)
        if SANITIZER_DIAGNOSTIC.search(output):
            raise SystemExit('Sanitizer/runtime-error diagnostic is never an expected fixture failure')
        expected_failures = EXPECTED_BASELINE_FAILURES if args.baseline else EXPECTED_MUTATION_FAILURES if args.mutation else set()
        expected_views = 7 if args.mutation else 6
        expected_marker = (
            f'candidate={int(not args.baseline)} callbacks=3 '
            f'views={expected_views}/{expected_views} content=6 windows=5 '
            f'failures={len(expected_failures)}')
        markers = [line for line in result.stdout.splitlines() if line.startswith('candidate=')]
        if markers != [expected_marker]:
            raise SystemExit('Missing or unexpected completion/ownership marker: ' + repr(markers))
        if failures != expected_failures or len(failure_lines) != len(expected_failures):
            raise SystemExit('Unexpected or duplicate fixture failures: ' + repr(failure_lines))
        if args.baseline:
            if result.returncode != 1:
                raise SystemExit('Unexpected baseline result: ' + str((result.returncode, failures)))
            print('PASS: baseline reaches exactly the three expected FIFO hierarchy failures')
        elif args.mutation:
            if result.returncode != 1:
                raise SystemExit('Mutation did not reach the intended terminal acquisition failure')
            print('PASS: native fixture detects missing terminal acquisition guard')
        elif result.returncode:
            raise SystemExit(result.returncode)
        elif failures:
            raise SystemExit('Candidate reported failures despite zero exit status')
        else:
            print('PASS: current production lifecycle bodies with real AppKit/QuartzCore and main GCD FIFO')


if __name__ == '__main__':
    main()
