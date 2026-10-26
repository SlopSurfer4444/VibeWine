#!/usr/bin/env python3
"""Exercise the production checked ring and all three block allocators with fault injection."""
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
    parser.add_argument('--arch', choices=('native', 'x86_64'), default='native')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    source = (args.source_dir or tests.parents[2]).resolve()
    path = 'src/dxmt/dxmt_ring_bump_allocator.hpp'
    contents = (source / path).read_text()
    # Only platform dependencies are replaced. The complete ring template and
    # native/placed/host allocator class bodies compile verbatim.
    generated = '\n'.join(line for line in contents.splitlines()
                          if line not in ('#include "Metal.hpp"', '#include "log/log.hpp"',
                                          '#include "thread.hpp"', '#include "util_math.hpp"'))
    compiler = shutil.which('clang++') or shutil.which('c++')
    if not compiler:
        parser.error('C++ compiler unavailable')
    with tempfile.TemporaryDirectory(prefix='dxmt-ring-checked-') as directory:
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
        if args.arch == 'x86_64' and platform.machine() == 'arm64':
            run = ['/usr/bin/arch', '-x86_64'] + run
        subprocess.run(run, check=True, timeout=30)
    print(json.dumps({'source': str(source), 'arch': args.arch, 'sanitizers': args.sanitize,
                      'source_sha256': {path: hashlib.sha256(contents.encode()).hexdigest()}}, sort_keys=True))


if __name__ == '__main__':
    main()
