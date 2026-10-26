#!/usr/bin/env python3
"""Exercise complete production CommitCurrentChunk before queue publication."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-dir', type=Path, default=HERE.parents[2])
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
variants = {'current': body}
# This mutation reinstates the observed ordering defect while preserving the
# full method, its publication operations and both preprocessor branches.
preflight = re.search(r'^  (?:auto|const auto) ([A-Za-z_][A-Za-z_0-9]*) = initializer\.flushToWait\(\);\n', body, re.M)
if preflight:
    name = preflight.group(1)
    assignment = f'  chunk.resource_initializer_event_id = {name};'
    assert assignment in body
    variants['early-event-mutation'] = body.replace(preflight.group(0), '', 1).replace(
        assignment, '  chunk.resource_initializer_event_id = initializer.flushToWait();', 1)
with tempfile.TemporaryDirectory(prefix='dxmt-initializer-queue-') as directory:
    scratch = Path(directory)
    for name, candidate in variants.items():
        generated = fixture.decode().replace('// EXACT_COMMIT_CURRENT_CHUNK', candidate)
        cpp = scratch / (name + '.cpp')
        cpp.write_text(generated)
        for asynchronous in (0, 1):
            for debug in (0, 1):
                binary = scratch / f'{name}-{asynchronous}-{debug}'
                command = [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
                           '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                           f'-DASYNC_ENCODING={asynchronous}', str(cpp), '-o', str(binary)]
                if debug:
                    command.insert(1, '-DDXMT_DEBUG')
                subprocess.run(command, check=True, timeout=45, capture_output=True, text=True)
                env = {**os.environ, 'ASAN_OPTIONS': 'halt_on_error=1', 'UBSAN_OPTIONS': 'halt_on_error=1'}
                # No GPU or external resource endpoints exist in this fixture.
                sandbox = shutil.which('sandbox-exec')
                execute = [str(binary)]
                if sandbox:
                    profile = '(version 1)(allow default)(deny network*)(deny file-write*)'
                    execute = [sandbox, '-p', profile, str(binary)]
                result = subprocess.run(execute, capture_output=True, text=True, timeout=15, env=env)
                assert len(result.stdout) + len(result.stderr) < 1024 * 1024
                assert 'Sanitizer' not in result.stderr and 'runtime error:' not in result.stderr, result.stderr
                if name == 'current':
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
                  'scope': 'complete production method; mocked queue/initializer endpoints; logical async/sync publication, no real threads or GPU',
                  'variants': list(variants), 'modes': 4}, sort_keys=True))
