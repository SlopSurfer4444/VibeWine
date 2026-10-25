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
    p.add_argument('--expect-uncached-baseline', action='store_true',
                   help='Use immutable old owner source with retirement on and reject its getter count')
    p.add_argument('--check-reuse-with-retirement-off', action='store_true',
                   help='Exercise the default dynamic reuse path with the opt-in compiled out')
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
    files = ['src/dxmt/dxmt_buffer.hpp', 'src/dxmt/dxmt_idle_retirement.hpp', 'src/dxmt/dxmt_dynamic.hpp',
             'src/dxmt/dxmt_dynamic.cpp', 'src/dxmt/dxmt_staging.hpp', 'src/dxmt/dxmt_staging.cpp']
    if args.expect_retained_baseline and args.expect_uncached_baseline:
        p.error('Choose one baseline mode')
    if args.expect_uncached_baseline and not args.source_ref:
        p.error('--expect-uncached-baseline requires --source-ref')
    if args.source_ref and not (args.expect_retained_baseline or args.expect_uncached_baseline):
        p.error('--source-ref requires a baseline mode')
    contents = [(subprocess.check_output(['git', '-C', str(source), 'show', f'{args.source_ref}:{path}'], text=True)
                 if args.source_ref and path != 'src/dxmt/dxmt_idle_retirement.hpp' else (source / path).read_text()) for path in files]
    generated = '\n'.join('\n'.join(line for line in text.splitlines()
                         if not line.startswith('#include') and line != '#pragma once')
                         for text in contents[1:])
    with tempfile.TemporaryDirectory(prefix='dxmt-idle-retirement-') as directory:
        scratch = Path(directory)
        # Compile the production BufferAllocation cache method/member inside
        # the native-boundary mock. All pool owners are compiled verbatim below.
        method = re.search(r'  uint64_t allocatedSizeForIdleRetirement\(\) const \{', contents[0])
        charge = ''
        if method:
            end = method.end()
            depth = 1
            while depth:
                depth += (contents[0][end] == '{') - (contents[0][end] == '}')
                end += 1
            member = re.search(r'  mutable std::atomic<uint64_t> idle_retirement_bytes_ = \{0\};', contents[0])
            if not member: raise RuntimeError('Missing production buffer charge member')
            charge = '#if DXMT_IDLE_RETIREMENT\n' + contents[0][method.start():end] + '\n' + member[0] + '\n#endif\n'
        (scratch / 'buffer-charge-under-test.hpp').write_text(charge)
        (scratch / 'owners-under-test.hpp').write_text(generated)
        command = [shutil.which('clang++') or 'c++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                   '-O1', '-g', '-pthread', f'-DDXMT_IDLE_RETIREMENT={int(not (args.expect_retained_baseline or args.check_reuse_with_retirement_off))}',
                   '-I', str(tests), '-I', str(scratch), '-I', str(source / 'src/util'),
                   str(tests / 'fixture.cpp'), '-o', str(scratch / 'fixture')]
        if args.arch == 'x86_64': command += ['-arch', 'x86_64']
        if args.sanitize: command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        subprocess.run(command, check=True, timeout=60)
        run = [str(scratch / 'fixture')]
        if args.arch == 'x86_64' and platform.machine() == 'arm64': run = ['/usr/bin/arch', '-x86_64'] + run
        subprocess.run(run + (['reuse-target'] if args.check_reuse_with_retirement_off else ['uncached-baseline'] if args.expect_uncached_baseline else []), check=True, timeout=30)
        if args.expect_uncached_baseline:
            rejected = subprocess.run(run + ['getter-target'], timeout=30)
            if rejected.returncode != 2:
                raise RuntimeError('Uncached predecessor unexpectedly met the getter target')
            print('OLDFAIL: unchanged getter target rejects uncached predecessor (exit 2)')
            rejected = subprocess.run(run + ['reuse-target'], timeout=30)
            if rejected.returncode != 2:
                raise RuntimeError('Copying predecessor unexpectedly met the reference target')
            print('OLDFAIL: unchanged reference target rejects copying predecessor (exit 2)')
        if args.expect_retained_baseline:
            rejected = subprocess.run(run + ['candidate-target'], timeout=30)
            if rejected.returncode != 2:
                raise RuntimeError('Disabled predecessor unexpectedly met the idle target')
            print('OLDFAIL: unchanged candidate target rejects disabled predecessor (exit 2)')
    print(json.dumps({'arch': args.arch, 'sanitize': args.sanitize,
          'baseline': args.expect_retained_baseline, 'default_reuse': args.check_reuse_with_retirement_off, 'uncached_baseline': args.expect_uncached_baseline, 'source_ref': args.source_ref,
          'source_sha256': {f: hashlib.sha256(c.encode()).hexdigest() for f, c in zip(files, contents)}}, sort_keys=True))

if __name__ == '__main__': main()
