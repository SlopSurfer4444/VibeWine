#!/usr/bin/env python3
"""Source-bound ring economy and held actual-consumer failure discriminators."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def block(text, marker, semicolon=False):
    start = text.index(marker)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end + int(semicolon)]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source-ref')
    p.add_argument('--baseline', action='store_true')
    p.add_argument('--sanitize', action='store_true')
    args = p.parse_args()
    tests = Path(__file__).resolve().parent
    source = tests.parents[2]
    paths = ['src/dxmt/dxmt_ring_bump_allocator.hpp', 'src/dxmt/dxmt_command_queue.hpp',
             'src/dxmt/dxmt_occlusion_query.hpp', 'src/util/rc/util_rc_ptr.hpp', 'src/util/util_likely.hpp',
             'src/dxmt/dxmt_context.hpp']
    data = {f: subprocess.check_output(['git', 'show', args.source_ref + ':' + f], cwd=source)
            if args.source_ref else (source / f).read_bytes() for f in paths}
    ring, queue, query = [data[f].decode() for f in paths[:3]]
    first = ring.index('\nclass GpuPrivateBufferBlockAllocator {')
    host = ring.index('\nclass HostBufferBlockAllocator {')
    generated = {
        'ring-under-test.hpp': '\n'.join(line for line in (ring[:first] + ring[host:]).splitlines()
                                        if not line.startswith('#include') and line != '#pragma once'),
        'allocate-command-under-test.hpp': block(queue, '  void *\n  AllocateCommandData('),
        'moveonly-list-under-test.hpp': block(queue, 'template <typename T> class moveonly_list', True),
        'timestamp-under-test.hpp': block(query, 'class TimestampQuery {', True),
        'readback-under-test.hpp': block(query, 'class TimestampReadbackCBuf {', True),
        'pointer-under-test.hpp': block(data[paths[5]].decode(), 'inline void *\nptr_add('),
    }
    print(json.dumps({'source_ref': args.source_ref, 'baseline': args.baseline, 'sanitize': args.sanitize,
                      'source_sha256': {f: hashlib.sha256(b).hexdigest() for f, b in data.items()},
                      'fixture_sha256': {f: hashlib.sha256((tests / f).read_bytes()).hexdigest()
                                         for f in ['run.py', 'fixture.cpp']},
                      'generated_sha256': {f: hashlib.sha256(b.encode()).hexdigest()
                                           for f, b in generated.items()}}, sort_keys=True), flush=True)
    with tempfile.TemporaryDirectory(prefix='dxmt-queue-contract-') as d:
        scratch = Path(d)
        for f, text in generated.items(): (scratch / f).write_text(text)
        (scratch / 'rc').mkdir()
        (scratch / 'rc/util_rc_ptr.hpp').write_bytes(data[paths[3]])
        (scratch / 'util_likely.hpp').write_bytes(data[paths[4]])
        command = [shutil.which('clang++') or 'c++', '-std=c++20', '-O1', '-g', '-Wall', '-Wextra',
                   '-Werror', '-Wno-unused-parameter', '-pthread', '-I', str(scratch), str(tests / 'fixture.cpp'), '-o', str(scratch / 'fixture')]
        if args.sanitize: command += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all']
        subprocess.run(command, check=True, timeout=60)
        subprocess.run([str(scratch / 'fixture'), 'baseline' if args.baseline else 'candidate'], check=True, timeout=10)
        failure = subprocess.run([str(scratch / 'fixture'), 'allocation-consumer'], capture_output=True, text=True, timeout=10)
        if failure.returncode == 0:
            raise RuntimeError('held allocation discriminator unexpectedly returned success')
        if args.sanitize and not ('moveonly-list-under-test.hpp:5:' in failure.stderr and
                                  'constructor call on null pointer' in failure.stderr):
            raise RuntimeError('expected sanitizer-attributed actual consumer failure: ' + failure.stderr)
        print('HELD_CPU18: actual command allocation consumer exits with failure=' + str(failure.returncode))
        print(failure.stderr[-3000:], end='')


if __name__ == '__main__': main()
