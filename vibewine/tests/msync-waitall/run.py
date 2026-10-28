#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Extract the complete production WaitAll and event-reset bodies; force races at atomic boundaries."""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import tempfile


def function(source, name):
    match = re.search(r'(?m)^(?:static[^;\n]*|NTSTATUS|void)\s*\b' + re.escape(name) + r'\s*\(', source)
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
    parser.add_argument('--revision')
    parser.add_argument('--expect-regression', action='store_true')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--arch', choices=('arm64', 'x86_64'), default='arm64')
    parser.add_argument('--mutation', choices=('ownership', 'wake', 'reset', 'deadline'))
    args = parser.parse_args()
    if args.expect_regression and not args.revision:
        parser.error('--expect-regression requires --revision')
    if args.mutation and args.revision:
        parser.error('--mutation requires current source')
    here = Path(__file__).resolve().parent
    root = here.parents[2]
    client = (subprocess.check_output(['git', 'show', args.revision + ':dlls/ntdll/unix/msync.c'], cwd=root, text=True)
              if args.revision else (root / 'dlls/ntdll/unix/msync.c').read_text())
    structs = client[client.index('struct semaphore\n'):client.index('typedef struct\n', client.index('struct semaphore\n'))]
    helpers = '\n\n'.join(function(client, name) for name in
                           ('event_word', 'reset_event_word', 'take_event_word', 'untake_event_word', 'untake_semaphore')
                           if re.search(r'\b' + name + r'\s*\(', client))
    server = (subprocess.check_output(['git', 'show', args.revision + ':server/msync.c'], cwd=root, text=True)
              if args.revision else (root / 'server/msync.c').read_text())
    server_body = function(server, 'msync_reset_event').replace('msync_reset_event(', 'server_reset_event(').replace('msync_reset_event( ', 'server_reset_event( ')
    server_struct = re.search(r'struct msync_event\n\{.*?\n\};', server, re.S).group(0)
    bodies = '\n\n'.join(function(client, name) for name in
                          ('msync_reset_event_obj', 'do_single_wait', 'msync_wait_objs'))
    if args.mutation == 'ownership':
        old = '                if (!(taken & ((uint64_t)1 << i))) continue;'
        assert bodies.count(old) == 1
        bodies = bodies.replace(old, '                (void)taken;')
    if args.mutation == 'wake':
        old = '                signal_all( objs_shm[i], objs[i] );'
        assert bodies.count(old) == 1
        bodies = bodies.replace(old, '')
    if args.mutation == 'reset':
        old = '__atomic_compare_exchange_n( event_word( event ), &expected, expected | 1, 0,\n                                 __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST );'
        assert helpers.count(old) == 1
        helpers = helpers.replace(old, '__atomic_store_n( &event->signaled, 1, __ATOMIC_SEQ_CST );')
    if args.mutation == 'deadline':
        old = '                if (timeout && !update_timeout( end )) return STATUS_TIMEOUT;'
        assert bodies.count(old) == 1
        bodies = bodies.replace(old, '')
    deadlines = '\n\n'.join(function(client, name) for name in ('msync_get_deadline', 'update_timeout')) if 'static ULONGLONG msync_get_deadline' in client else ''
    generated = ('#define HAS_DEADLINE_DOMAIN ' + str(bool(deadlines)*1) + '\n#define HAS_GENERATION ' + str(int(bool(helpers))) + '\n' + (here / 'fixture.c.in').read_text().replace('@STRUCTS@', structs)
                 .replace('@HELPERS@', helpers).replace('@DEADLINES@', deadlines).replace('@PRODUCTION@', bodies)
                 .replace('@SERVER@', server_struct + '\n' + server_body))
    with tempfile.TemporaryDirectory(prefix='vibewine-msync-waitall-') as tmp:
        source, binary = Path(tmp) / 'fixture.c', Path(tmp) / 'fixture'
        source.write_text(client[:client.index('*/') + 2] + '\n' + generated)
        cmd = ['clang', '-arch', args.arch, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
               '-Wno-unused-function', '-Wno-unused-parameter', '-Wno-sign-compare', '-Wno-unused-variable',
               '-pthread', str(source), '-o', str(binary)]
        if args.sanitize:
            cmd += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        subprocess.run(cmd, check=True, timeout=60)
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
        print(result.stdout, end='')
        print(result.stderr, end='')
        failures = {line[5:] for line in result.stderr.splitlines() if line.startswith('FAIL ')}
        baseline = {'recursive ownership survives rollback', 'abandoned state survives rollback',
                    'rollback wakes restored event', 'rollback does not exceed semaphore maximum',
                    'reset during take is not undone', 'retry checks expired deadline', 'retry checks alert',
                    'successful abandoned WaitAll control', 'server reset during take is not undone',
                    'retry pending APC precedes expired deadline'}
        mutations = {'ownership': {'recursive ownership survives rollback'},
                     'wake': {'rollback wakes restored event'}, 'reset': {'reset during take is not undone', 'server reset during take is not undone'},
                     'deadline': {'retry checks expired deadline'}}
        expected = baseline if args.expect_regression else mutations.get(args.mutation, set())
        expected_checks = 15 if helpers else 14
        if (failures != expected or result.returncode != bool(expected)
                or len(result.stderr.splitlines()) != len(expected)
                or result.stdout != f'checks={expected_checks} failed={len(expected)}\n'):
            raise RuntimeError('unexpected result: ' + repr((result.returncode, result.stdout, result.stderr, expected)))
    print('PASS exact_source_sha256=' + hashlib.sha256(client.encode()).hexdigest()
          + ' generated_sha256=' + hashlib.sha256(generated.encode()).hexdigest()
          + ' expected_failures=' + str(len(expected)) + ' arch=' + args.arch)


if __name__ == '__main__':
    main()
