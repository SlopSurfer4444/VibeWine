#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Extract real MSync sends and exercise native private Mach queue pressure."""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import sys
import tempfile


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


def declaration(source, pattern):
    match = re.search(pattern, source, re.S)
    if not match:
        raise RuntimeError('production declaration missing: ' + pattern)
    return match.group()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch', choices=('native', 'arm64', 'x86_64', 'all'), default='native')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('native private Mach pressure fixture requires Darwin')
    here = Path(__file__).resolve().parent
    root = here.parents[2]
    client = (root / 'dlls/ntdll/unix/msync.c').read_text()
    server = (root / 'server/msync.c').read_text()
    wrapper_end = client.index('\n/* Optional duration attribution:') if '/* Optional duration attribution:' in client else client.index('\nstruct semaphore')
    wrapper = client[client.index('#define LIBMACH_OPTIONS64'):wrapper_end]
    server_wrapper = server[server.index('#define LIBMACH_OPTIONS64'):server.index('\nstatic mach_port_name_t receive_port;')]
    if wrapper.strip() != server_wrapper.strip():
        raise RuntimeError('client/server Mach wrappers diverged; audit both before updating fixture')
    bodies = '\n\n'.join(function(client, name) for name in
                          ('server_register_wait', 'server_remove_wait', 'signal_all'))
    server_signal = function(server, 'signal_all').replace(' signal_all(', ' server_signal_all(', 1)
    declarations = '\n'.join((
        declaration(client, r'struct event\s*\{[^}]+\};'),
        declaration(server, r'struct msync_shm\s*\{[^}]+\};'),
        declaration(client, r'typedef struct\s*\{\s*mach_msg_header_t header;\s*unsigned int shm_idx\[MAXIMUM_WAIT_OBJECTS \+ 1\];\s*\} mach_register_message_t;'),
        declaration((root / 'dlls/ntdll/unix/msync.h').read_text(), r'enum msync_type\s*\{[^}]+\};'),
    ))
    # Only symbol renaming of the extracted wrapper permits an entry/return seam.
    wrapped = re.sub(r'\bmach_msg2\(', 'production_mach_msg2(', wrapper)
    source_hash = hashlib.sha256((declarations + wrapper + bodies + server_signal).encode()).hexdigest()
    print('production SHA256=' + source_hash, flush=True)
    print('client file SHA256=' + hashlib.sha256(client.encode()).hexdigest(), flush=True)
    print('server file SHA256=' + hashlib.sha256(server.encode()).hexdigest(), flush=True)
    generated = ((here / 'fixture.c.in').read_text().replace('@DECLARATIONS@', declarations)
                 .replace('@WRAPPER@', wrapped).replace('@CLIENT@', bodies)
                 .replace('@SERVER_SIGNAL@', server_signal))
    license_header = client[:client.index('*/') + 2]
    arches = ('arm64', 'x86_64') if args.arch == 'all' else (args.arch,)
    with tempfile.TemporaryDirectory(prefix='vibewine-msync-pressure-') as tmp:
        scratch = Path(tmp)
        for arch in arches:
            for mutation, expected in (('none', 'native pressure PASS'),
                                       ('bypass', 'FAIL sender returned before gated drain'),
                                       ('timeout', 'FAIL production send argument contract changed'),
                                       ('missing-wake', 'FAIL native ulock wake was not delivered before timeout')):
                candidate = generated
                if mutation == 'bypass':
                    candidate = candidate.replace('ret = production_mach_msg2(data, options, send_size, rcv_size, rcv_name, timeout, priority);',
                                                  'ret = MACH_MSG_SUCCESS;')
                elif mutation == 'timeout':
                    # Mutate actual callers, not the fixture's audited syscall wrapper.
                    changed = bodies.replace('MACH_SEND_MSG,', 'MACH_SEND_MSG | MACH_SEND_TIMEOUT,')
                    changed_server = server_signal.replace('MACH_SEND_MSG,', 'MACH_SEND_MSG | MACH_SEND_TIMEOUT,')
                    candidate = candidate.replace(bodies, changed).replace(server_signal, changed_server)
                elif mutation == 'missing-wake':
                    candidate = candidate.replace(bodies, bodies.replace('__ulock_wake(', 'missing_wake('))
                    candidate = candidate.replace(server_signal, server_signal.replace('__ulock_wake(', 'missing_wake('))
                if mutation != 'none' and candidate == generated:
                    raise RuntimeError('negative control made no mutation: ' + mutation)
                source, binary = scratch / f'{arch}-{mutation}.c', scratch / f'{arch}-{mutation}'
                source.write_text(license_header + '\n' + candidate)
                command = ['clang', '-std=gnu11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                           '-Wno-unused-parameter', '-Wno-unused-function', '-pthread', str(source), '-o', str(binary)]
                if arch != 'native':
                    command += ['-arch', arch]
                if args.sanitize:
                    command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
                subprocess.run(command, check=True, timeout=60)
                for backend in ('trap', 'fallback'):
                    # Timeout kills/reaps only this owned process; process exit releases Mach rights.
                    result = subprocess.run([str(binary), backend], text=True, capture_output=True, timeout=15)
                    if mutation == 'none':
                        summary = (f'native pressure PASS: cases=4 backend={backend} qlimit=1 kernel_wait=4 '
                                   'drained=4 zero_waiter=2 real_ulock_wakes=2 rights=released\n')
                        if result.returncode or result.stderr or result.stdout != summary:
                            raise RuntimeError(f'{arch}/{backend} failed: {result!r}')
                    elif result.returncode != 1 or expected not in result.stderr:
                        raise RuntimeError(f'{arch}/{backend}/{mutation} control not rejected: {result!r}')
                    print(f'{arch}/{backend}/{mutation}: ' + (result.stdout.strip() or result.stderr.strip()), flush=True)


if __name__ == '__main__':
    main()
