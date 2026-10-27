#!/usr/bin/env python3
"""Exercise complete production finish/Retain/reset, tracker and host ring bodies."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tempfile


def body(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    end, depth = opening + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def strip(text):
    return '\n'.join(line for line in text.splitlines()
                     if not line.startswith('#include') and line != '#pragma once')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source-ref')
    p.add_argument('--expect-retained-baseline', action='store_true')
    p.add_argument('--arch', choices=('native', 'x86_64'), default='native')
    p.add_argument('--sanitize', action='store_true')
    args = p.parse_args()
    tests = Path(__file__).resolve().parent
    source = tests.parents[2]
    files = ['src/dxmt/dxmt_allocation.hpp', 'src/dxmt/dxmt_allocation.cpp',
             'src/dxmt/dxmt_ring_bump_allocator.hpp', 'src/dxmt/dxmt_command_queue.hpp',
             'src/dxmt/dxmt_command_queue.cpp']
    revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', '--verify',
                 '--end-of-options', args.source_ref + '^{commit}'], text=True).strip() if args.source_ref else None
    data = {f: subprocess.check_output(['git', '-C', str(source), 'show', revision + ':' + f])
            if revision else (source / f).read_bytes() for f in files}
    text = {f: b.decode() for f, b in data.items()}
    ring = text[files[2]]
    first = ring.index('\nclass GpuPrivateBufferBlockAllocator {')
    host = ring.index('\nclass HostBufferBlockAllocator {')
    methods = ring.index('\ntemplate <typename Allocator, size_t BlockSize, class mutex>\n', host)
    generated_ring = strip(ring[:first] + '\n#define malloc tracked_malloc\n#define free tracked_free\n' +
                           ring[host:methods] + '\n#undef malloc\n#undef free\n' + ring[methods:])
    generated = '\n'.join([strip(text[files[0]]), strip(text[files[1]]), generated_ring])
    methods = '\n'.join([body(text[files[4]], 'uint32_t\nCommandQueue::WaitForFinishThread()'),
                         body(text[files[4]], 'void CommandQueue::Retain(')])
    reset = body(text[files[3]], '  void\n  reset(uint64_t traceQueue = 0) noexcept')
    constant = re.search(r'constexpr uint32_t kCommandChunkCount = \d+;', text[files[3]])[0]
    fixtures = {f: (tests / f).read_bytes() for f in ('fixture.cpp', 'run.py')}
    print(json.dumps({'source_ref': revision, 'arch': args.arch, 'sanitize': args.sanitize,
          'source_sha256': {f: hashlib.sha256(b).hexdigest() for f, b in data.items()},
          'fixture_sha256': {f: hashlib.sha256(b).hexdigest() for f, b in fixtures.items()},
          'generated_sha256': hashlib.sha256((generated + methods + reset + constant).encode()).hexdigest()}, sort_keys=True), flush=True)
    with tempfile.TemporaryDirectory(prefix='dxmt-reftracker-') as directory:
        scratch = Path(directory)
        (scratch / 'owners.hpp').write_text(generated)
        (scratch / 'queue-methods.hpp').write_text(methods)
        (scratch / 'chunk-reset.hpp').write_text(reset)
        (scratch / 'queue-constant.hpp').write_text(constant)
        (scratch / 'fixture.cpp').write_bytes(fixtures['fixture.cpp'])
        exe = scratch / 'fixture'
        command = [shutil.which('clang++') or 'c++', '-std=c++20', '-O1', '-Wall', '-Wextra', '-Werror',
                   '-pthread', '-I', str(scratch), str(scratch / 'fixture.cpp'), '-o', str(exe)]
        if args.arch == 'x86_64': command += ['-arch', 'x86_64']
        if args.sanitize: command += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all']
        subprocess.run(command, check=True, timeout=60)
        run = [str(exe)]
        if args.arch == 'x86_64' and platform.machine() == 'arm64': run = ['/usr/bin/arch', '-x86_64'] + run
        result = subprocess.run(run, timeout=30, capture_output=True, text=True)
        print(result.stdout, end='')
        print(result.stderr, end='')
        expected = 2 if args.expect_retained_baseline else 0
        if result.returncode != expected:
            raise RuntimeError(f'candidate acceptance returned {result.returncode}, expected {expected}')
        if args.expect_retained_baseline:
            failures = ['FAIL: full finish method completed storage bound',
                        'FAIL: expired burst preserves four pending tracking blocks and warm spare',
                        'FAIL: all 32 completed tracker slots, including terminal error, retire']
            if result.stderr.splitlines() != failures:
                raise RuntimeError('Predecessor did not produce the three exact retention failures')
            print('OLDFAIL: unchanged completed-burst bound rejects predecessor with exit 2')


if __name__ == '__main__': main()
