#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Compile actual Wine functions and require specific baseline regressions.

CPU/CoreFoundation tests only; no Wine process, network, game or installed file.
Generated sources and executables are bounded and removed on exit.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import tempfile


def block(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--baseline-ref', default='7469fcf53c4f537dfe305e961bffb0495d7b750d')
    parser.add_argument('--cc', default='clang')
    args = parser.parse_args()
    if platform.system() != 'Darwin':
        parser.error('CoreFoundation ownership fixture requires macOS')
    root, tests = args.source_dir.resolve(), Path(__file__).resolve().parent
    paths = ('dlls/ntdll/sync.c', 'server/handle.c', 'dlls/winemac.drv/clipboard.c')
    sources = {
        'candidate': {p: (root / p).read_text() for p in paths},
        'baseline': {p: subprocess.check_output(
            ['git', '-C', str(root), 'show', args.baseline_ref + ':' + p],
            text=True, timeout=20) for p in paths},
    }
    results = []
    with tempfile.TemporaryDirectory(prefix='vibewine-backports-') as directory:
        work = Path(directory)
        for generation, files in sources.items():
            sync, handle, clipboard = (files[p] for p in paths)
            snippets = {
                'srw': {
                    'PRODUCTION_STRUCTURE': block(sync, 'struct srw_lock\n{') + ';',
                    'PRODUCTION_FUNCTIONS': '\n\n'.join(block(sync, 'void WINAPI ' + name + '(')
                        for name in ('RtlAcquireSRWLockExclusive', 'RtlAcquireSRWLockShared',
                                     'RtlReleaseSRWLockExclusive', 'RtlReleaseSRWLockShared')),
                },
                'handle': {'PRODUCTION_FUNCTION': block(handle, 'unsigned int close_handle(')},
                'clipboard': {'PRODUCTION_FUNCTION': block(clipboard, 'struct format_entry *get_format_entries(')},
            }
            for fixture, replacements in snippets.items():
                code = (tests / ('backport_' + fixture + '_fixture.c.in')).read_text()
                for marker, body in replacements.items():
                    token = '/* ' + marker + ' */'
                    if code.count(token) != 1:
                        raise ValueError('missing/duplicate fixture insertion point: ' + token)
                    code = code.replace(token, body)
                # Preserve the original production source's license header.
                original = files[paths[('srw', 'handle', 'clipboard').index(fixture)]]
                code = original[:original.index('*/') + 2] + '\n' + code
                source = work / (generation + '-' + fixture + '.c')
                exe = source.with_suffix('.test')
                source.write_text(code)
                command = [args.cc, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra',
                           '-Wno-unused-function', '-Wno-unused-variable',
                           '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                           str(source), '-o', str(exe), '-pthread']
                if fixture == 'clipboard':
                    command += ['-framework', 'CoreFoundation']
                compiled = subprocess.run(command, capture_output=True, text=True, timeout=60)
                if compiled.returncode:
                    raise RuntimeError('fixture compile failed: ' + compiled.stderr)
                cases = ('controls', 'reentrant') if fixture == 'handle' else ('',)
                for case in cases:
                    command = [str(exe)] + ([case] if case else [])
                    run = subprocess.run(command, capture_output=True, text=True, timeout=10)
                    if generation == 'candidate' or case == 'controls':
                        marker = {'srw': 'SRW_PASS', 'clipboard': 'CLIPBOARD_PASS',
                                  'handle': 'HANDLE_' + case.upper() + '_PASS'}[fixture]
                        valid = run.returncode == 0 and marker in run.stdout and not run.stderr
                    elif fixture == 'srw':
                        valid = (run.returncode == 42
                                 and run.stderr.strip() == 'REGRESSION_REDUNDANT_SRW_WAKE count=128'
                                 and 'blocked exclusive waiter acquired=1' in run.stdout)
                    elif fixture == 'clipboard':
                        valid = (run.returncode == 43
                                 and run.stderr.count('REGRESSION_CF_DATA_OWNERSHIP') == 2
                                 and all(line.startswith('REGRESSION_CF_DATA_OWNERSHIP ')
                                         for line in run.stderr.splitlines())
                                 and run.stdout.count('CLIPBOARD_CASE ') == 3)
                    else:
                        valid = (run.returncode != 0
                                 and 'ERROR: AddressSanitizer: heap-use-after-free' in run.stderr
                                 and 'SUMMARY: AddressSanitizer: heap-use-after-free' in run.stderr)
                    if not valid:
                        raise RuntimeError(f'{generation}/{fixture}/{case} unexpected outcome: '
                                           + repr((run.returncode, run.stdout, run.stderr)))
                    results.append({'generation': generation, 'fixture': fixture, 'case': case,
                                    'exit': run.returncode, 'expected_outcome': 'PASS',
                                    'generated_source_sha256': hashlib.sha256(code.encode()).hexdigest()})
    print(json.dumps({'result': 'PASS', 'baseline_ref': args.baseline_ref,
                      'checks': results, 'limits': 'CPU function/CoreFoundation fixtures; '
                      'no Wine address-queue ABI, whole server, game or performance acceptance'}, indent=2))


if __name__ == '__main__':
    main()
