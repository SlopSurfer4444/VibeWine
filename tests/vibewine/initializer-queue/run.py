#!/usr/bin/env python3
"""Exercise complete production CommitCurrentChunk before queue publication."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-dir', type=Path, default=HERE.parents[2])
parser.add_argument('--arch', choices=('native', 'x86_64'), default='native')
parser.add_argument('--legacy-source-ref', default='3f137007699b139e21db13e060c0d9bd9a449d5a')
args = parser.parse_args()
root = args.source_dir.resolve()
relative = 'src/dxmt/dxmt_command_queue.cpp'
original = (root / relative).read_bytes()
matches = re.findall(r'^void\nCommandQueue::CommitCurrentChunk\(\) \{[\s\S]*?^\}', original.decode(), re.M)
assert len(matches) == 1
body = matches[0]
fixture = (HERE / 'fixture.cpp.in').read_bytes()
compiler = shutil.which('clang++')
assert compiler
legacy_original = subprocess.check_output(['git', '-C', str(root), 'show', f'{args.legacy_source_ref}:{relative}'])
legacy_matches = re.findall(r'^void\nCommandQueue::CommitCurrentChunk\(\) \{[\s\S]*?^\}', legacy_original.decode(), re.M)
assert len(legacy_matches) == 1
variants = {}
for lane, method in [('current', body), ('legacy', legacy_matches[0])]:
    variants[lane] = (method, lane == 'legacy')
    # Preserve the complete method; move only initializer preflight to its old
    # event-publication point. All prior legacy assertions remain in that lane.
    preflight = re.search(r'^  (?:auto|const auto) ([A-Za-z_][A-Za-z_0-9]*) = initializer\.flushToWait\(\);\n', method, re.M)
    assert preflight, lane
    name = preflight.group(1)
    assignment = f'  chunk.resource_initializer_event_id = {name};'
    assert method.count(assignment) == 1
    variants[lane + '-early-event-mutation'] = (method.replace(preflight.group(0), '', 1).replace(
        assignment, '  chunk.resource_initializer_event_id = initializer.flushToWait();', 1), lane == 'legacy')
with tempfile.TemporaryDirectory(prefix='dxmt-initializer-queue-') as directory:
    scratch = Path(directory)
    for name, (candidate, legacy) in variants.items():
        generated = fixture.decode().replace('// EXACT_COMMIT_CURRENT_CHUNK', candidate)
        cpp = scratch / (name + '.cpp')
        cpp.write_text(generated)
        for asynchronous in (0, 1):
            for debug in (0, 1):
                binary = scratch / f'{name}-{asynchronous}-{debug}'
                command = [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
                           '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                           f'-DASYNC_ENCODING={asynchronous}', f'-DLEGACY_INITIALIZER_CONTRACT={int(legacy)}', str(cpp), '-o', str(binary)]
                if debug:
                    command.insert(1, '-DDXMT_DEBUG')
                if args.arch == 'x86_64': command += ['-arch', 'x86_64']
                built = subprocess.run(command, timeout=45, capture_output=True, text=True)
                assert built.returncode == 0, built.stderr
                env = {**os.environ, 'ASAN_OPTIONS': 'halt_on_error=1', 'UBSAN_OPTIONS': 'halt_on_error=1'}
                # No GPU or external resource endpoints exist in this fixture.
                sandbox = shutil.which('sandbox-exec')
                execute = [str(binary)]
                if args.arch == 'x86_64' and platform.machine() == 'arm64':
                    execute = ['/usr/bin/arch', '-x86_64'] + execute
                if sandbox:
                    profile = '(version 1)(allow default)(deny network*)(deny file-write*)'
                    execute = [sandbox, '-p', profile] + execute
                result = subprocess.run(execute, capture_output=True, text=True, timeout=15, env=env)
                assert len(result.stdout) + len(result.stderr) < 1024 * 1024
                assert 'Sanitizer' not in result.stderr and 'runtime error:' not in result.stderr, result.stderr
                if 'mutation' not in name:
                    assert result.returncode == 0, result.stderr
                    print(result.stdout.strip())
                else:
                    assert result.returncode == 1 and 'FAIL initializer failure mutated unpublished queue state' in result.stderr, (result.returncode, result.stderr)
                    print(f'PASS rejected {name} async={asynchronous} debug={debug}')
    assert sum(p.stat().st_size for p in scratch.iterdir() if p.is_file()) < 64 * 1024 * 1024
assert (root / relative).read_bytes() == original, 'production source changed during fixture execution'
print(json.dumps({'source': relative, 'source_sha256': hashlib.sha256(original).hexdigest(),
                  'fixture_sha256': hashlib.sha256(fixture).hexdigest(),
                  'runner_sha256': hashlib.sha256((HERE / 'run.py').read_bytes()).hexdigest(),
                  'scope': 'complete production method; mocked queue/initializer endpoints; current unified admission and historical async/sync publication, no real threads or GPU',
                  'variants': list(variants), 'modes': 4, 'arch': args.arch,
                  'legacy_source_ref': args.legacy_source_ref, 'legacy_source_sha256': hashlib.sha256(legacy_original).hexdigest()}, sort_keys=True))
