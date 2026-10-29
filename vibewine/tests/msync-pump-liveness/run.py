#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Source-bound Darwin cross-process MSync reply-pump progress checks."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

BASELINE = '271e5cdaa93c5a49c7cb219479cff36ead7cd8c8'


def function(source, name):
    match = re.search(r'(?m)^static[^;\n]*\b' + re.escape(name) + r'\s*\(', source)
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
    parser.add_argument('--arch', choices=('arm64', 'x86_64'), default='arm64')
    parser.add_argument('--revision')
    parser.add_argument('--expect-blocked', action='store_true')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--generate-only', action='store_true')
    parser.add_argument('--mutation', choices=('missing-failed-send-cleanup',))
    parser.add_argument('--output', type=Path, help='private bounded evidence directory')
    args = parser.parse_args()
    if args.expect_blocked and not args.revision:
        parser.error('--expect-blocked requires --revision')
    if args.mutation and args.revision:
        parser.error('mutation requires current source')
    if not args.generate_only and sys.platform != 'darwin':
        parser.error('native fixture requires Darwin')
    here = Path(__file__).resolve().parent
    root = here.parents[2]

    def read(path):
        if args.revision:
            return subprocess.check_output(['git', 'show', f'{args.revision}:{path}'], cwd=root, text=True)
        return (root / path).read_text()

    client, server = (read(path) for path in ('dlls/ntdll/unix/msync.c', 'server/msync.c'))
    wrapper = server[server.index('#define LIBMACH_OPTIONS64'):server.index('\nstatic mach_port_name_t receive_port;')]
    client_end = client.index('\n/* Optional duration attribution:')
    if wrapper.strip() != client[client.index('#define LIBMACH_OPTIONS64'):client_end].strip():
        raise RuntimeError('production Mach wrappers diverged')
    # Renaming only the outer entry permits argument/result observation while
    # preserving the real trap and fallback implementations and all arguments.
    wrapper = re.sub(r'\bmach_msg2\(', 'production_mach_msg2(', wrapper)
    names = ('msync_get_deadline', 'update_timeout', 'msync_timeout_ns', 'ulock_wait',
             'server_register_wait', 'server_remove_wait', 'check_shm_contention', 'msync_wait_multiple')
    constants = client[client.index('#define MSYNC_ACK_PENDING'):client.index('static NTSTATUS msync_wait_multiple')]
    clients = '\n\n'.join(function(client, name) for name in names)
    servers = '\n\n'.join(function(server, name) for name in (
        'grow_tid_map', 'get_tid_list', 'add_tid', 'remove_tid', 'send_shm_to_client',
        'unregister_wait', 'wake_tid_word', 'wake_tid', 'signal_all_internal',
        'receive_mach_msg', 'decode_msgh_id', 'check_bit', 'mach_message_pump'))
    if args.mutation:
        cleanup = '        mach_msg_destroy( &reply.header );'
        if servers.count(cleanup) != 1:
            raise RuntimeError('failed-send mutation requires exactly one cleanup')
        servers = servers.replace(cleanup, '        /* Deliberate missing-cleanup negative control. */')
    ack = function(server, 'acknowledge_wait').replace(' acknowledge_wait(', ' production_acknowledge_wait(', 1)
    generated = ((here / 'fixture.c.in').read_text()
                 .replace('@WRAPPER@', wrapper).replace('@CLIENT_CONSTANTS@', constants)
                 .replace('@CLIENT@', clients).replace('@SERVER_ACK@', ack).replace('@SERVER@', servers))
    if '@' in generated:
        raise RuntimeError('unresolved fixture marker')
    binding = {
        'revision': args.revision or 'working-tree', 'arch': args.arch, 'mutation': args.mutation,
        'client_sha256': hashlib.sha256(client.encode()).hexdigest(),
        'server_sha256': hashlib.sha256(server.encode()).hexdigest(),
        'extracted_sha256': hashlib.sha256((wrapper + constants + clients + ack + servers).encode()).hexdigest(),
        'generated_sha256': hashlib.sha256(generated.encode()).hexdigest(),
    }
    if args.output:
        args.output.mkdir(parents=True, exist_ok=False)
        (args.output / 'generated.c').write_text(generated)
        (args.output / 'binding.json').write_text(json.dumps(binding, indent=2) + '\n')
    print(json.dumps(binding), flush=True)
    if args.generate_only:
        print('GENERATE_ONLY no compiler or fixture execution', flush=True)
        return
    with tempfile.TemporaryDirectory(prefix='vibewine-msync-pump-') as tmp:
        scratch = Path(tmp)
        source, binary = scratch / 'fixture.c', scratch / 'fixture'
        source.write_text(client[:client.index('*/') + 2] + '\n' + generated)
        command = ['clang', '-arch', args.arch, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                   '-Wno-unused-parameter', '-Wno-unused-function', '-Wno-sign-compare',
                   '-Wno-deprecated-declarations', '-pthread', '-I', str(root / 'include'),
                   str(source), '-o', str(binary)]
        if args.sanitize:
            command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-sanitize-recover=all']
        subprocess.run(command, check=True, timeout=60)
        results = []
        for backend in ('trap', 'fallback'):
            for case in (('full-timeout',) if args.mutation else ('full-timeout', 'full-alert', 'dead-client', 'normal-map')):
                result = subprocess.run([str(binary), backend, case], text=True, capture_output=True, timeout=12)
                observation = {'backend': backend, 'case': case, 'returncode': result.returncode,
                               'stdout': result.stdout, 'stderr': result.stderr}
                results.append(observation)
                if args.output:
                    (args.output / 'results.json').write_text(json.dumps({'binding': binding, 'checks': results}, indent=2) + '\n')
                print(json.dumps(observation), flush=True)
                expected = args.expect_blocked and case.startswith('full-')
                label = 'FAIL independent ACK blocked behind full reply queue'
                if args.mutation:
                    if result.returncode != 2 or 'invocation entry send right released' not in result.stderr:
                        raise RuntimeError('missing-cleanup control did not fail owned-right oracle')
                elif expected:
                    if result.returncode != 1 or label not in result.stdout:
                        raise RuntimeError('baseline did not fail for intended reason')
                elif result.returncode or 'PASS' not in result.stdout:
                    raise RuntimeError('native pump check failed')
                if 'AddressSanitizer' in result.stderr or 'runtime error:' in result.stderr:
                    raise RuntimeError('sanitizer failure')
        if args.output:
            (args.output / 'results.json').write_text(json.dumps({'binding': binding, 'checks': results}, indent=2) + '\n')


if __name__ == '__main__':
    main()
