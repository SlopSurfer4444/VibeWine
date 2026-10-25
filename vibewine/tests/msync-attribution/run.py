#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Compile actual opt-in attribution helpers with bounded deterministic I/O seams."""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch', choices=('native', 'arm64', 'x86_64', 'all'), default='native')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    root = here.parents[2]
    client = (root / 'dlls/ntdll/unix/msync.c').read_text()
    helpers = client[client.index('/* Optional duration attribution:'):client.index('\nstruct semaphore')]
    print('attribution production SHA256=' + hashlib.sha256(helpers.encode()).hexdigest(), flush=True)
    print('client file SHA256=' + hashlib.sha256(client.encode()).hexdigest(), flush=True)
    template = (here / 'fixture.c.in').read_text()
    arches = ('arm64', 'x86_64') if args.arch == 'all' else (args.arch,)
    with tempfile.TemporaryDirectory(prefix='vibewine-msync-attribution-') as tmp:
        scratch = Path(tmp)
        for arch in arches:
            for mutation, expected in (('none', None), ('cap', 'FAIL snapshot cap exceeded'),
                                       ('admission', 'FAIL duplicate snapshot admission')):
                actual = helpers
                if mutation == 'cap':
                    actual = actual.replace('#define MSYNC_ATTR_MAX_SNAPSHOTS 64', '#define MSYNC_ATTR_MAX_SNAPSHOTS 65')
                elif mutation == 'admission':
                    actual, count = re.subn(r'if \(now < next \|\| !__atomic_compare_exchange_n\( &msync_attribution.next, &next,\s*now \+ MSYNC_ATTR_INTERVAL_NS, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED \)\) return;',
                                            'if (now < next) return;', actual)
                    if count != 1:
                        raise RuntimeError('admission mutation binding changed')
                if mutation != 'none' and actual == helpers:
                    raise RuntimeError('mutation made no change: ' + mutation)
                source, binary = scratch / f'{arch}-{mutation}.c', scratch / f'{arch}-{mutation}'
                source.write_text(client[:client.index('*/') + 2] + '\n' + template.replace('@PRODUCTION@', actual))
                command = ['clang', '-std=gnu11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                           '-Wno-unused-parameter', '-Wno-sign-compare', '-pthread', str(source), '-o', str(binary)]
                if arch != 'native':
                    command += ['-arch', arch]
                if args.sanitize:
                    command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
                subprocess.run(command, check=True, timeout=60)
                result = subprocess.run([str(binary)], text=True, capture_output=True, timeout=15)
                if expected:
                    if result.returncode != 1 or result.stdout or result.stderr != expected + '\n':
                        raise RuntimeError(f'{arch}/{mutation} control not rejected: {result!r}')
                elif result.returncode or result.stderr or result.stdout != 'attribution PASS: disabled metrics interval cap clocks concurrent partial_write\n':
                    raise RuntimeError(f'{arch} contract failed: {result!r}')
                print(f'{arch}/{mutation}: ' + (result.stdout.strip() or result.stderr.strip()), flush=True)


if __name__ == '__main__':
    main()
