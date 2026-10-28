#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run the production header's CPU concurrency contracts, without Wine/Metal."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', choices=['none', 'address', 'thread'], default='address')
    parser.add_argument('--arch', choices=['arm64', 'x86_64'])
    parser.add_argument('--keep', type=Path, help='Retain bounded source hashes and test logs, not binaries')
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    source = tests.parents[2]
    files = [source / 'src/d3d11/d3d11_shader_singleflight.hpp', tests / 'singleflight.cpp', Path(__file__)]
    snapshot = {path: path.read_bytes() for path in files}
    compiler = shutil.which(os.environ.get('CXX', 'clang++'))
    if not compiler:
        parser.error('C++ compiler unavailable')
    receipt = {'scope': 'actual production header; CPU only; no Wine/Metal/GPU/installation',
               'sanitize': args.sanitize, 'compiler': compiler, 'arch': args.arch or platform.machine(),
               'source_sha256': {str(path.relative_to(source)): hashlib.sha256(snapshot[path]).hexdigest()
                                 for path in files}, 'results': []}
    logs = {}
    with tempfile.TemporaryDirectory(prefix='dxmt-singleflight-') as directory:
        binary = Path(directory) / 'singleflight'
        for mode in ['exceptions', 'no-exceptions']:
            command = [compiler, '-std=c++20', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-pthread',
                       '-I', str(source / 'src'), str(tests / 'singleflight.cpp'), '-o', str(binary)]
            if mode == 'no-exceptions':
                command.append('-fno-exceptions')
            if args.arch:
                command += ['-arch', args.arch]
            if args.sanitize != 'none':
                command += ['-fsanitize=' + ('address,undefined' if args.sanitize == 'address' else 'thread'),
                            '-fno-omit-frame-pointer']
            build = subprocess.run(command, capture_output=True, text=True, timeout=60)
            logs[mode + '-build.txt'] = build.stdout + build.stderr
            if build.returncode:
                print(logs[mode + '-build.txt'], end='')
                build.check_returncode()
            launch = [str(binary)]
            if args.arch and platform.machine() != args.arch:
                launch = ['/usr/bin/arch', '-' + args.arch, *launch]
            result = subprocess.run(launch, capture_output=True, text=True, timeout=30,
                                    env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=' + ('0' if platform.system() == 'Darwin' else '1'),
                                         'UBSAN_OPTIONS': 'halt_on_error=1',
                                         'TSAN_OPTIONS': 'exitcode=66:abort_on_error=0'})
            logs[mode + '-stdout.txt'] = result.stdout
            logs[mode + '-stderr.txt'] = result.stderr
            print(result.stdout, end='')
            print(result.stderr, end='')
            result.check_returncode()
            if result.stderr or not result.stdout.startswith('PASS production singleflight:'):
                raise SystemExit('unexpected test output')
            receipt['results'].append({'mode': mode, 'status': result.returncode})
        assert sum(path.stat().st_size for path in Path(directory).iterdir()) < 64 * 1024 * 1024, '64 MiB scratch cap'
    assert all(path.read_bytes() == contents for path, contents in snapshot.items()), 'source changed during test run; discard receipt'
    if args.keep:
        args.keep.mkdir(parents=True, exist_ok=True)
        for name, content in logs.items():
            (args.keep / name).write_text(content)
        (args.keep / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt, sort_keys=True))


if __name__ == '__main__':
    main()
