#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Extract production MSync pointer-table callers; gate a reader across growth."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def function(source, name):
    match = re.search(r'(?m)^static[^;\n]*\b' + re.escape(name) + r'\s*\([^;]*?\)\s*\{', source)
    if not match:
        raise RuntimeError('production function missing: ' + name)
    start = source.index('{', match.start())
    depth, end = 1, start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--revision', help='Git production revision; fixture always comes from working tree')
    parser.add_argument('--expect-regression', action='store_true')
    parser.add_argument('--arch', choices=('native', 'arm64', 'x86_64', 'all'), default='native')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('native Mach table fixture requires Darwin')
    if args.expect_regression and not args.sanitize:
        parser.error('the deterministic old-fail control requires --sanitize')
    here = Path(__file__).resolve().parent
    root = here.parents[2]

    def read(path):
        if args.revision:
            return subprocess.check_output(['git', 'show', f'{args.revision}:{path}'], cwd=root, text=True)
        return (root / path).read_text()

    arches = ('arm64', 'x86_64') if args.arch == 'all' else (args.arch,)
    template = (here / 'table.c.in').read_text()
    with tempfile.TemporaryDirectory(prefix='vibewine-msync-table-') as temporary:
        scratch = Path(temporary)
        for side, path in (('client', 'dlls/ntdll/unix/msync.c'), ('server', 'server/msync.c')):
            production = read(path)
            fixed = 'static struct msync_table shm_addrs;' in production
            if fixed == args.expect_regression:
                raise RuntimeError('unexpected table implementation for selected expectation')
            names = ('get_shm_slow', 'get_shm') if side == 'client' else ('get_shm',)
            bodies = '\n\n'.join(function(production, name) for name in names)
            declarations = re.findall(r'(?m)^static (?:void \*\*shm_addrs|int shm_addrs_size|struct msync_table shm_addrs|os_unfair_lock shm_addrs_lock)[^;]*;', production)
            if not declarations:
                raise RuntimeError('production table declarations missing')
            header = read('include/wine/msync_table.h') if fixed else ''
            digest = hashlib.sha256(('\n'.join(declarations) + header + bodies).encode()).hexdigest()
            print(f'{side} production SHA256={digest} source={args.revision or "working-tree"}', flush=True)
            print(f'{side} file SHA256={hashlib.sha256(production.encode()).hexdigest()}', flush=True)
            # The baseline has plain slot loads. Add only the same pre-load gate
            # that wraps the fixed helper's atomic loads, retaining all callers.
            instrumented = bodies if fixed else bodies.replace('shm_addrs[entry]', '(*(void **)pin_slot(&shm_addrs[entry]))')
            generated = (template.replace('@FIXED@', str(int(fixed))).replace('@CLIENT@', str(int(side == 'client')))
                         .replace('@HEADER@', header).replace('@GLOBALS@', '\n'.join(declarations))
                         .replace('@FUNCTIONS@', instrumented))
            for arch in arches:
                source, binary = scratch / f'{side}-{arch}.c', scratch / f'{side}-{arch}'
                source.write_text(generated)
                command = ['clang', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                           '-Wno-unused-parameter', '-Wno-unused-function', '-Wno-unused-variable',
                           '-Wno-sign-compare', '-pthread', '-I', str(root / 'include'), str(source), '-o', str(binary)]
                if arch != 'native':
                    command += ['-arch', arch]
                if args.sanitize:
                    command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
                subprocess.run(command, check=True, timeout=60)
                modes = ('grow',) if args.expect_regression else ('grow', 'alloc-fail', 'map-fail', 'invalid-index')
                for mode in modes:
                    environment = dict(os.environ, ASAN_OPTIONS='detect_leaks=0:abort_on_error=0', UBSAN_OPTIONS='halt_on_error=1')
                    result = subprocess.run([str(binary), mode], capture_output=True, text=True, timeout=20, env=environment)
                    if args.expect_regression:
                        if result.returncode == 0 or 'AddressSanitizer: heap-use-after-free' not in result.stderr:
                            raise RuntimeError(f'{side}/{arch}: expected pinned-reader UAF absent: {result!r}')
                        print(f'{side}/{arch}/grow EXPECTED_REGRESSION heap-use-after-free pinned_reader=1 forced_move=1', flush=True)
                        print(result.stderr, end='', flush=True)
                    elif mode == 'grow':
                        if result.returncode or result.stderr or 'TABLE_GROWTH_PASS' not in result.stdout:
                            raise RuntimeError(f'{side}/{arch}/{mode} failed: {result!r}')
                        print(f'{arch}: {result.stdout.strip()}', flush=True)
                    else:
                        marker = {'alloc-fail': 'INJECT allocation failure', 'map-fail': 'INJECT mapping failure',
                                  'invalid-index': 'INJECT invalid object index'}[mode]
                        if (result.returncode != 1 or marker not in result.stderr or 'Sanitizer' in result.stderr
                                or 'production returned' in result.stderr):
                            raise RuntimeError(f'{side}/{arch}/{mode}: expected controlled fatal exit absent: {result!r}')
                        print(f'{side}/{arch}/{mode} CONTROLLED_FATAL_PASS exit=1 diagnostic={result.stderr.strip()!r}', flush=True)


if __name__ == '__main__':
    main()
