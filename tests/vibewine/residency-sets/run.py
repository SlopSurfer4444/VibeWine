#!/usr/bin/env python3
"""Run production residency policy and extracted queue method without Metal/Wine."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def block(source, marker):
    start = source.index(marker)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--negative-controls', action='store_true')
    parser.add_argument('--control', help='Run only this named negative control after positive checks')
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    source = tests.parents[2]
    paths = ['src/dxmt/dxmt_residency_set_policy.hpp', 'src/dxmt/dxmt_command_queue.hpp',
             'src/dxmt/dxmt_command_queue.cpp', 'src/dxmt/dxmt_context.hpp']
    content = {p: (source/p).read_text() for p in paths}
    header, queue_header, queue, context = (content[p] for p in paths)
    raw = block(context, '  makeResident(Buffer *buffer, bool read = true, bool write = false)')
    assert raw.index('CheckResourceResidency(') < raw.index('tryReadOnlyResidency(') < raw.index('allocation->buffer(), requested')
    assert '#if defined(DXMT_RESIDENCY_SETS) && DXMT_RESIDENCY_SETS' in raw
    assert context.count('if (tryReadOnlyResidency(buffer, requested))') == 1
    for marker in ('  makeResident(Counter *counter)', '  makeResident(Buffer *buffer, uint64_t viewId,',
                   '  makeResident(Texture *texture, uint64_t viewId,'):
        assert 'tryReadOnlyResidency' not in block(context, marker)
    fixture = (tests/'fixture.cpp').read_text().replace('// BACKEND', block(queue_header, '  struct ResidencyBackend {')+';')
    refs = '\n'.join(block(queue_header, '  struct '+name+' : WMT::Buffer {')+';'
                     for name in ('ResidencyBufferCandidate','ResidencyBufferReference'))
    fixture = fixture.replace('// NATIVE_REFERENCE', refs)
    fixture = fixture.replace('// QUEUE_API', block(queue, 'bool\nCommandQueue::TryReadOnlyResidency('))
    fixture = fixture.replace('// INITIALIZE', block(queue, 'void\nCommandQueue::InitializeResidencySet()'))
    shutdown = block(queue, 'void\nCommandQueue::ShutdownAndJoin()')
    assert shutdown.index('finishThread.join()') < shutdown.index('residency_policy_.clearAfterDrain') < shutdown.index('chunk.reset()')
    cleanup_start = shutdown.index('  if (capture_state.takeCaptureOwnership())')
    cleanup_end = shutdown.index('#if DXMT_RESIDENCY_SETS', cleanup_start)
    assert shutdown.index('finishThread.join()') < cleanup_start < cleanup_end
    fixture = fixture.replace('// CAPTURE_CLEANUP', shutdown[cleanup_start:cleanup_end])
    drain = block(shutdown, '  if (residency_set_) {')
    fixture = fixture.replace('// SHUTDOWN_DRAIN', drain)
    retain = block(queue, 'void CommandQueue::Retain(')
    assert retain.index('residency_policy_.touch(allocation, seq);') < retain.index('tracker.track(allocation)')
    compiler = shutil.which('clang++') or shutil.which('c++')
    assert compiler, 'C++ compiler unavailable'
    checks = 0
    with tempfile.TemporaryDirectory(prefix='dxmt-residency-') as directory:
        scratch = Path(directory)

        def run(name, policy, test, flag, negative=False):
            nonlocal checks
            (scratch/'dxmt_residency_set_policy.hpp').write_text(policy)
            (scratch/'fixture.cpp').write_text(test)
            cmd = [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
                   f'-DTEST_EXPECT_ENABLED={int(flag == 1)}', '-I', str(scratch), '-I', str(source/'src/util'),
                   str(scratch/'fixture.cpp'), '-o', str(scratch/'fixture')]
            if flag is not None:
                cmd += [f'-DDXMT_RESIDENCY_SETS={flag}']
            if args.sanitize:
                cmd += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            subprocess.run(cmd, check=True, capture_output=True, timeout=60)
            result = subprocess.run([str(scratch/'fixture')], capture_output=True, text=True, timeout=15)
            assert (result.returncode != 0) == negative, (name, result.returncode, result.stdout, result.stderr)
            print(('REJECTED ' if negative else 'PASS ') + name)
            checks += 1

        run('enabled', header, fixture, 1)
        run('default-off', header, fixture, 0)
        run('macro-omitted-default-off', header, fixture, None)
        if args.negative_controls or args.control:
            mutations = [
                ('premature-completion-removal', 'completed - entry.last_use < limits_.idle_completed_sequences', 'false'),
                ('no-stability-filter', 'const bool stable = seq > entry.last_observation;', 'const bool stable = true;'),
                ('byte-cap-bypass', 'held_bytes_ > limits_.bytes - bytes', 'false'),
                ('count-cap-bypass', 'entries_.size() >= limits_.count', 'false'),
                ('shutdown-no-drain', 'chunk.attached_cmdbuf.waitUntilCompleted();', '(void)0;'),
                ('alias-use-not-retained', 'entry.owner == owner && seq > entry.last_use', 'false'),
                ('unsupported-os-called', 'if (major >= 15)', 'if (major >= 14)'),
                ('write-request-covered', '!(requested & DXMT_RESOURCE_RESIDENCY_WRITE)', 'true'),
                ('heap-covered', '!flags.any(BufferAllocationFlag::AllocatedOnHeap, BufferAllocationFlag::CpuPlaced,', '!flags.any(BufferAllocationFlag::CpuPlaced,'),
                ('cached-charge-owner-bypass', 'entry.allocation.handle == handle && entry.owner == owner', 'entry.allocation.handle == handle'),
            ]
            if args.control:
                mutations = [entry for entry in mutations if entry[0] == args.control]
                assert mutations, 'unknown negative control'
            for name, before, after in mutations:
                assert (header+fixture).count(before)==1, name
                run(name, header.replace(before, after), fixture.replace(before, after), 1, negative=True)
    print(json.dumps({'checks': checks, 'sanitizers': args.sanitize,
                      'scope': 'production CPU policy and extracted queue admission; native APIs mocked',
                      'source_sha256': {p:hashlib.sha256(v.encode()).hexdigest() for p,v in content.items()}}, sort_keys=True))


if __name__ == '__main__':
    main()
