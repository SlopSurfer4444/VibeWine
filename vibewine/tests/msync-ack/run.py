#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Compile actual MSync client/pump bodies and Darwin server ACK helpers."""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import sys
import tempfile

BASELINE = 'bee453b09460e0c3eb49b7c491a6d04e65cc38a5'


def function(source, name):
    match = re.search(r'(?m)^static[^;\n]*\b' + re.escape(name) + r'\s*\(', source)
    if not match:
        raise RuntimeError('production function missing: ' + name)
    start = source.index('{', match.start())
    depth, end = 1, start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--revision', help='read source from component Git without checkout')
    parser.add_argument('--expect-regression', action='store_true')
    parser.add_argument('--native', action='store_true', help='also run actual Darwin cross-process ulock fixture')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--mutation', choices=('missing-ack-reset',), help='require the named candidate mutation to fail')
    args = parser.parse_args()
    if args.expect_regression and not args.revision:
        parser.error('--expect-regression requires --revision')
    if args.native and sys.platform != 'darwin':
        parser.error('--native requires Darwin')
    here = Path(__file__).resolve().parent
    root = here.parents[2]

    def read(path):
        if args.revision:
            return subprocess.check_output(['git', 'show', f'{args.revision}:{path}'], cwd=root, text=True)
        return (root / path).read_text()

    client = read('dlls/ntdll/unix/msync.c')
    server = read('server/msync.c')
    if args.mutation:
        reset = '    /* ACK slices must not become a deadline for an infinite object wait. */\n    ns_timeleft = 0;\n'
        if args.revision or args.native or client.count(reset) != 1:
            raise RuntimeError('mutation requires current source with exactly one ACK reset, without --native')
        client = client.replace(reset, '')
    constants = ''
    if '#define MSYNC_ACK_PENDING' in client:
        constants = client[client.index('#define MSYNC_ACK_PENDING'):client.index('static NTSTATUS msync_wait_multiple')]
    timeout_helper = function(client, 'msync_timeout_ns') + '\n' if 'static ULONGLONG msync_timeout_ns' in client else ''
    clients = timeout_helper + '\n\n'.join(function(client, name) for name in (
        'server_register_wait', 'server_remove_wait', 'check_shm_contention', 'msync_wait_multiple'))
    names = ['grow_tid_map', 'get_tid_list', 'add_tid', 'remove_tid', 'unregister_wait']
    if 'static inline void acknowledge_wait' in server:
        names += ['wake_tid_word', 'acknowledge_wait']
    names += ['wake_tid', 'signal_all_internal', 'decode_msgh_id', 'check_bit', 'mach_message_pump']
    servers = '\n\n'.join(function(server, name) for name in names)
    checks = 20 if 'ret == -ETIMEDOUT && end && update_timeout' in client else 19
    generated = ('#define HAS_TIMEOUT_CHUNKS ' + str(int(checks == 20)) + '\n' + (here / 'fixture.c.in').read_text()
                 .replace('/* PRODUCTION_ACK_CONSTANTS */', constants)
                 .replace('/* PRODUCTION_CLIENT */', clients)
                 .replace('/* PRODUCTION_SERVER */', servers))
    expected_failures = {
        'park before ack', 'interrupted ack wake is retried',
        'delayed ack slices are not object timeout', 'deadline while ack pending',
        'expired deadline while ack pending', 'interrupted ack park retries without unregister',
        'faulted ack park rechecks', 'spurious ack wake rechecks state',
        'alert cancels parked registration once', 'unexpected ack park failure cleans registration',
        'dead server before ack park aborts thread', 'dead server after ack park aborts thread',
        'dead server during registration aborts thread', 'compare mismatch rechecks ack state',
        'finite deadline preserved after ack',
    }

    def compile_run(text, label, scratch):
        source, binary = scratch / (label + '.c'), scratch / label
        source.write_text(client[:client.index('*/')+2] + '\n' + text)
        command = ['clang', '-std=gnu11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                   '-Wno-unused-parameter', '-Wno-unused-function', '-Wno-unused-value', '-Wno-sign-compare',
                   '-pthread', str(source), '-o', str(binary)]
        if args.sanitize:
            command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        subprocess.run(command, check=True, timeout=60)
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
        print(result.stdout, end='')
        print(result.stderr, end='')
        return result

    with tempfile.TemporaryDirectory(prefix='vibewine-msync-ack-') as tmp:
        scratch = Path(tmp)
        result = compile_run(generated, 'deterministic', scratch)
        if args.mutation:
            if (result.returncode != 1 or result.stdout != f'checks={checks} failed=2\n'
                    or result.stderr != 'FAIL park before ack\nFAIL ACK slice never limits infinite object wait\n'):
                raise RuntimeError('unexpected ACK reset mutation result: ' + repr(result))
            print('EXPECTED_MISSING_ACK_RESET_CONFIRMED')
        elif args.expect_regression:
            failures = result.stderr.splitlines()
            if (result.returncode != 1 or result.stdout != f'checks={checks} failed=15\n'
                    or len(failures) != len(expected_failures)
                    or set(failures) != {'FAIL '+name for name in expected_failures}):
                raise RuntimeError('unexpected baseline result: ' + repr(result))
        elif result.returncode or result.stderr or result.stdout != f'checks={checks} failed=0\n':
            raise RuntimeError('candidate deterministic check failed: ' + repr(result))
        print('production SHA256=' + hashlib.sha256((constants+clients+servers).encode()).hexdigest())
        if args.native:
            if args.expect_regression:
                raise RuntimeError('run native candidate and its store-only negative control without --expect-regression')
            native = ((here / 'native.c.in').read_text()
                      .replace('@WAKE_TID_WORD@', function(server, 'wake_tid_word'))
                      .replace('@ACKNOWLEDGE_WAIT@', function(server, 'acknowledge_wait')))
            result = compile_run(native, 'native', scratch)
            native_summary = (r'native ACK PASS: iterations=2000 early=500 parked=500 race=500 signalled=500 '
                              r'waits=\d+ wait_EINTR=\d+ real_wakes=[1-9]\d* wake_EINTR=\d+ '
                              r'deadline=PASS dead_server=PASS reaped=PASS elapsed_ms=\d+\n')
            if result.returncode or result.stderr or not re.fullmatch(native_summary, result.stdout):
                raise RuntimeError('native check failed: ' + repr(result))
            negative = ((here / 'native.c.in').read_text()
                        .replace('@WAKE_TID_WORD@', function(server, 'wake_tid_word'))
                        .replace('@ACKNOWLEDGE_WAIT@', 'static inline void acknowledge_wait(int tid) { '
                                 '(void)wake_tid_word; __atomic_store_n(shm_tid_map+tid,1,__ATOMIC_RELEASE); }'))
            result = compile_run(negative, 'native-missing-wake', scratch)
            if (result.returncode != 1 or result.stdout
                    or not re.fullmatch(r'native ACK FAIL: ACK observed only after ulock timeout: missing server wake '
                                        r'\(waits=\d+, EINTR=\d+, wakes=0\)\n', result.stderr)):
                raise RuntimeError('native missing-wake control failed: ' + repr(result))
            print('EXPECTED_NATIVE_MISSING_WAKE_CONFIRMED')
            liveness = ((here / 'liveness.c.in').read_text()
                        .replace('/* PRODUCTION_LIVENESS */', function(client, 'check_msync_server')))
            result = compile_run(liveness, 'native-liveness', scratch)
            if (result.returncode or result.stderr or result.stdout !=
                    'native Mach liveness PASS: live=continue dead-name=abort invalid-name=abort rights=released\n'):
                raise RuntimeError('native Mach liveness check failed: ' + repr(result))


if __name__ == '__main__':
    main()
