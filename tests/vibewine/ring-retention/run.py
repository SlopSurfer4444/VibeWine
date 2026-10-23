#!/usr/bin/env python3
"""Compile the verbatim production ring template against counted move-only blocks."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path)
    parser.add_argument('--source-ref', help='Read the production header from this Git ref')
    parser.add_argument('--expect-retained-baseline', action='store_true')
    parser.add_argument('--arch', choices=('native', 'x86_64'), default='native')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    source = (args.source_dir or tests.parents[2]).resolve()
    path = 'src/dxmt/dxmt_ring_bump_allocator.hpp'
    contents = (subprocess.check_output(['git', '-C', str(source), 'show', f'{args.source_ref}:{path}'], text=True)
                if args.source_ref else (source / path).read_text())
    # Preserve the template declaration, methods and constants verbatim. Omit
    # only includes and the unrelated Metal/host block-allocator implementations.
    first = contents.index('\nclass GpuPrivateBufferBlockAllocator {')
    last = contents.index('\ntemplate <typename Allocator, size_t BlockSize, class mutex>\n', first)
    generated = contents[:first] + contents[last:]
    generated = '\n'.join(line for line in generated.splitlines()
                          if not line.startswith('#include') and line != '#pragma once')
    compiler = shutil.which('clang++') or shutil.which('c++')
    if not compiler:
        parser.error('C++ compiler unavailable')
    with tempfile.TemporaryDirectory(prefix='dxmt-ring-test-') as directory:
        scratch = Path(directory)
        (scratch / 'ring-under-test.hpp').write_text(generated)
        command = [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-O1', '-g', '-pthread',
                   '-I', str(tests), '-I', str(scratch), str(tests / 'fixture.cpp'), '-o', str(scratch / 'fixture')]
        if args.arch == 'x86_64':
            command += ['-arch', 'x86_64']
        if args.sanitize:
            command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        subprocess.run(command, check=True, timeout=60)
        run = [str(scratch / 'fixture')]
        if args.expect_retained_baseline:
            run += ['baseline']
        if args.arch == 'x86_64' and platform.machine() == 'arm64':
            run = ['/usr/bin/arch', '-x86_64'] + run
        subprocess.run(run, check=True, timeout=30)
    print(json.dumps({'source': str(source), 'source_ref': args.source_ref, 'arch': args.arch,
                      'sanitizers': args.sanitize, 'baseline': args.expect_retained_baseline,
                      'source_sha256': {path: hashlib.sha256(contents.encode()).hexdigest()}}, sort_keys=True))


if __name__ == '__main__':
    main()
