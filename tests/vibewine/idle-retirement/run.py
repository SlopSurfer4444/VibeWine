#!/usr/bin/env python3
"""Extract real owner methods/registry, substituting only counted native allocations."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tempfile


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--arch', choices=('native', 'x86_64'), default='native')
    p.add_argument('--sanitize', action='store_true')
    p.add_argument('--source-ref', help='Immutable old owner source, baseline mode only')
    p.add_argument('--expect-retained-baseline', action='store_true',
                   help='Disable production switch and prove the inactive-resource defect remains')
    args = p.parse_args()
    tests = Path(__file__).resolve().parent
    source = tests.parents[2]
    if not args.expect_retained_baseline:
        wired = []
        for path in sorted((source / 'src/d3d11').glob('*.cpp')):
            content = path.read_text()
            for match in re.finditer(r'new (DynamicBuffer|DynamicLinearTexture|StagingResource)\(', content):
                end = content.index(');', match.end())
                if '.idleRetirement()' not in content[match.end():end]:
                    raise RuntimeError(f'Unbound pool constructor: {path.name}:{content[:match.start()].count(chr(10))+1}')
                wired.append(path.name)
        if len(wired) != 5:
            raise RuntimeError(f'Unexpected pool creation coverage: {wired}')
        print('PASS: all five D3D11 pool creation sites carry the queue domain')
    files = ['src/dxmt/dxmt_idle_retirement.hpp', 'src/dxmt/dxmt_dynamic.hpp',
             'src/dxmt/dxmt_dynamic.cpp', 'src/dxmt/dxmt_staging.hpp', 'src/dxmt/dxmt_staging.cpp']
    if args.source_ref and not args.expect_retained_baseline:
        p.error('--source-ref requires --expect-retained-baseline')
    contents = [(subprocess.check_output(['git', '-C', str(source), 'show', f'{args.source_ref}:{path}'], text=True)
                 if args.source_ref and path != files[0] else (source / path).read_text()) for path in files]
    generated = '\n'.join('\n'.join(line for line in text.splitlines()
                         if not line.startswith('#include') and line != '#pragma once')
                         for text in contents)
    with tempfile.TemporaryDirectory(prefix='dxmt-idle-retirement-') as directory:
        scratch = Path(directory)
        (scratch / 'owners-under-test.hpp').write_text(generated)
        command = [shutil.which('clang++') or 'c++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                   '-O1', '-g', '-pthread', f'-DDXMT_IDLE_RETIREMENT={int(not args.expect_retained_baseline)}',
                   '-I', str(tests), '-I', str(scratch), '-I', str(source / 'src/util'),
                   str(tests / 'fixture.cpp'), '-o', str(scratch / 'fixture')]
        if args.arch == 'x86_64': command += ['-arch', 'x86_64']
        if args.sanitize: command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        subprocess.run(command, check=True, timeout=60)
        run = [str(scratch / 'fixture')]
        if args.arch == 'x86_64' and platform.machine() == 'arm64': run = ['/usr/bin/arch', '-x86_64'] + run
        subprocess.run(run, check=True, timeout=30)
        if args.expect_retained_baseline:
            rejected = subprocess.run(run + ['candidate-target'], timeout=30)
            if rejected.returncode != 2:
                raise RuntimeError('Disabled predecessor unexpectedly met the idle target')
            print('OLDFAIL: unchanged candidate target rejects disabled predecessor (exit 2)')
    print(json.dumps({'arch': args.arch, 'sanitize': args.sanitize,
          'baseline': args.expect_retained_baseline, 'source_ref': args.source_ref,
          'source_sha256': {f: hashlib.sha256(c.encode()).hexdigest() for f, c in zip(files, contents)}}, sort_keys=True))

if __name__ == '__main__': main()
