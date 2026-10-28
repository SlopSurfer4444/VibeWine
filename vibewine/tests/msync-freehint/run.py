#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Extract MSync allocation/destruction and exercise legal bounded-domain reuse."""
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
    source = (subprocess.check_output(['git', 'show', args.revision + ':server/msync.c'], cwd=root, text=True)
              if args.revision else (root / 'server/msync.c').read_text())
    declarations = (re.search(r'struct msync_shm\n\{.*?\n\};', source, re.S).group() + '\n' +
                    '\n'.join(re.findall(r'(?m)^static unsigned int last_(?:allocated|destroyed)_idx[^;]*;', source)))
    bodies = '\n\n'.join(function(source, name) for name in ('destroy_all_internal', 'msync_alloc_shm'))
    generated = (here / 'fixture.c.in').read_text().replace('@DECLARATIONS@', declarations).replace('@FUNCTIONS@', bodies)
    with tempfile.TemporaryDirectory(prefix='vibewine-msync-freehint-') as tmp:
        c, binary = Path(tmp) / 'fixture.c', Path(tmp) / 'fixture'
        c.write_text(generated)
        command = ['clang', '-arch', args.arch, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                   '-Wno-unused-parameter', '-pthread', str(c), '-o', str(binary)]
        if args.sanitize:
            command += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
        subprocess.run(command, check=True, timeout=60)
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
    print(result.stdout, end='')
    print(result.stderr, end='')
    failures = {line[5:] for line in result.stderr.splitlines() if line.startswith('FAIL ')}
    expected = ({'multiple frees preserve lowest hole', 'four-live-object churn stays bounded',
                 'boundary consumes lower concurrent free before exhaustion'} if args.revision else set())
    summary = f'checks=7 failed={len(expected)} churn_iterations=2000 bound=15\n'
    if (failures != expected or result.returncode != bool(expected) or 'Sanitizer' in result.stderr
            or not re.fullmatch(r'runtime translated=[01] pointer_bits=64\n' + re.escape(summary), result.stdout)
            or len(result.stderr.splitlines()) != len(expected)):
        raise RuntimeError('unexpected freehint checks: ' + repr((result.returncode, result.stdout, result.stderr, expected)))
    print(json.dumps({'arch': args.arch, 'revision': args.revision or 'working-tree', 'sanitize': args.sanitize,
                      'source_sha256': hashlib.sha256(source.encode()).hexdigest(),
                      'production_sha256': hashlib.sha256((declarations+bodies).encode()).hexdigest(),
                      'fixture_sha256': hashlib.sha256(generated.encode()).hexdigest(),
                      'old_failures': len(expected)}))


if __name__ == '__main__':
    main()
