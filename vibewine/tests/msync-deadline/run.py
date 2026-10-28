#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Compile source-bound MSync deadlines and real own-process Darwin ulock checks."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile


def function(source, name):
    match = re.search(r'(?m)^static[^;\n]*\b' + re.escape(name) + r'\s*\(', source)
    if not match:
        raise RuntimeError('missing production function: ' + name)
    start = source.index('{', match.start())
    depth, end = 1, start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch', choices=('arm64', 'x86_64'), default='arm64')
    parser.add_argument('--revision')
    parser.add_argument('--expect-regression', action='store_true')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    if args.expect_regression != bool(args.revision):
        parser.error('revision control requires --expect-regression')
    here = Path(__file__).resolve().parent
    root = here.parents[2]
    source = (subprocess.check_output(['git', 'show', args.revision + ':dlls/ntdll/unix/msync.c'], cwd=root, text=True)
              if args.revision else (root / 'dlls/ntdll/unix/msync.c').read_text())
    if args.revision:
        start = source.index('    NtQuerySystemTime( &now );', source.index('NTSTATUS msync_wait_objs('))
        end = source.index('\n\n    for (i = 0; i < count;', start)
        # Wrap the old initializer bytes so the exact former clock conversion is
        # exercised without substituting a hand-written faulty implementation.
        initializer = source[start:end]
        deadline = ('static ULONGLONG msync_get_deadline(const LARGE_INTEGER *timeout) {\n'
                    '    LARGE_INTEGER now; ULONGLONG end=0;\n' + initializer + '\n    return end;\n}')
        ns = 'static ULONGLONG msync_timeout_ns(ULONGLONG end) { return update_timeout(end)*100; }'
    else:
        deadline = function(source, 'msync_get_deadline')
        ns = function(source, 'msync_timeout_ns')
    bodies = '\n\n'.join((deadline, function(source, 'update_timeout'), ns,
                             function(source, 'ulock_wait'), function(source, 'msync_wait_single')))
    generated = (here / 'fixture.c.in').read_text().replace('@PRODUCTION@', bodies).replace('@BASELINE@', str(int(bool(args.revision))))
    with tempfile.TemporaryDirectory(prefix='vibewine-msync-deadline-') as tmp:
        c, binary = Path(tmp) / 'fixture.c', Path(tmp) / 'fixture'
        c.write_text(generated)
        command = ['clang', '-arch', args.arch, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                   '-Wno-unused-function', '-Wno-unused-parameter', '-pthread', str(c), '-o', str(binary)]
        if args.sanitize:
            command += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
        subprocess.run(command, check=True, timeout=60)
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
    print(result.stdout, end='')
    print(result.stderr, end='')
    failures = {line[5:] for line in result.stderr.splitlines() if line.startswith('FAIL ')}
    expected = ({'relative ignores backward wall jump', 'relative ignores forward wall jump',
                 'relative uses monotonic elapsed ticks', 'relative expires at its monotonic deadline',
                 'legacy microseconds never wrap to infinite',
                 'interrupted relative wait ignores backward wall jump',
                 'interrupted relative wait ignores forward wall jump',
                 'finite kernel chunk timeout rechecks deadline'} if args.revision else set())
    count = 14 if args.revision else 18
    summary = f'checks={count} failed={len(expected)} native_owned_ulocks={int(not args.revision)}\n'
    if (failures != expected or result.returncode != bool(expected) or 'Sanitizer' in result.stderr
            or not re.fullmatch(r'runtime translated=[01] pointer_bits=64\n' + re.escape(summary), result.stdout)
            or len(result.stderr.splitlines()) != len(expected)):
        raise RuntimeError('unexpected deadline checks: ' + repr((result.returncode, result.stdout, result.stderr, expected)))
    print(json.dumps({'arch': args.arch, 'revision': args.revision or 'working-tree', 'sanitize': args.sanitize,
                      'source_sha256': hashlib.sha256(source.encode()).hexdigest(),
                      'production_sha256': hashlib.sha256(bodies.encode()).hexdigest(),
                      'fixture_sha256': hashlib.sha256(generated.encode()).hexdigest(),
                      'old_failures': len(expected)}))


if __name__ == '__main__':
    main()
