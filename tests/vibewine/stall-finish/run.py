#!/usr/bin/env python3
"""Extract the complete finish method; verify call order with boundary spies."""
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
args = parser.parse_args()
root = args.source_dir.resolve()
relative = 'src/dxmt/dxmt_command_queue.cpp'
original = (root / relative).read_bytes()
matches = re.findall(r'^uint32_t\nCommandQueue::WaitForFinishThread\(\) \{[\s\S]*?^\}', original.decode(), re.M)
assert len(matches) == 1, 'complete finish method binding'
body = matches[0]
fixture = (HERE / 'fixture.cpp.in').read_bytes()
compiler = shutil.which('clang++')
assert compiler

def replace_once(text, old, new):
    assert text.count(old) == 1, old
    return text.replace(old, new, 1)

variants = {'current': (body, (0, 1))}
variants['removed-ring-scope'] = (replace_once(body,
    '      cs2trace::StallScope trace(109, traceSerial(), internal_seq, trace_frame);\n', ''), (0, 1))
variants['widened-idle-scope'] = (replace_once(body,
    '''    const auto idle_sample = [&] {
      cs2trace::StallScope retirement_trace(108, traceSerial(), internal_seq, trace_frame);
      return idle_retirement_->sweep(internal_seq);
    }();''',
    '''    cs2trace::StallScope retirement_trace(108, traceSerial(), internal_seq, trace_frame);
    const auto idle_sample = idle_retirement_->sweep(internal_seq);'''), (1,))
variants['reset-before-readback'] = (replace_once(body,
    '    chunk.readback = {};', '    chunk.reset();\n    chunk.readback = {};'), (0, 1))
variants['early-cpu-fence'] = (replace_once(replace_once(body,
    '    cpu_coherent.signal(internal_seq);\n', ''),
    '    chunk.readback = {};', '    cpu_coherent.signal(internal_seq);\n    chunk.readback = {};'), (0, 1))
counter = '''    chunk_ongoing.fetch_sub(1, std::memory_order_release);
    chunk_ongoing.notify_one();'''
variants['early-chunk-counter'] = (replace_once(replace_once(body, counter, ''),
    '    cpu_coherent.signal(internal_seq);', counter + '\n    cpu_coherent.signal(internal_seq);'), (0, 1))

with tempfile.TemporaryDirectory(prefix='dxmt-stall-finish-') as directory:
    scratch = Path(directory)
    for name, (candidate, modes) in variants.items():
        cpp = scratch / (name + '.cpp')
        cpp.write_text(fixture.decode().replace('// EXACT_WAIT_FOR_FINISH_THREAD', candidate))
        for idle in modes:
            binary = scratch / f'{name}-{idle}'
            command = [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
                       '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                       f'-DDXMT_IDLE_RETIREMENT={idle}', str(cpp), '-o', str(binary)]
            if args.arch == 'x86_64':
                command += ['-arch', 'x86_64']
            built = subprocess.run(command, capture_output=True, text=True, timeout=45)
            assert built.returncode == 0, built.stderr
            env = {**os.environ, 'ASAN_OPTIONS': 'halt_on_error=1', 'UBSAN_OPTIONS': 'halt_on_error=1'}
            execute = [str(binary)]
            if args.arch == 'x86_64' and platform.machine() == 'arm64':
                execute = ['/usr/bin/arch', '-x86_64'] + execute
            sandbox = shutil.which('sandbox-exec')
            if sandbox:
                execute = [sandbox, '-p', '(version 1)(allow default)(deny network*)(deny file-write*)'] + execute
            result = subprocess.run(execute, capture_output=True, text=True, timeout=15, env=env)
            assert len(result.stdout) + len(result.stderr) < 64 * 1024
            assert 'Sanitizer' not in result.stderr and 'runtime error:' not in result.stderr, result.stderr
            if name == 'current':
                assert result.returncode == 0, result.stderr
                print(result.stdout.strip())
            else:
                assert result.returncode == 1 and 'FAIL ' in result.stderr, (result.returncode, result.stderr)
                print(f'PASS rejected {name} idle={idle}')
            assert sum(path.stat().st_size for path in scratch.iterdir() if path.is_file()) < 64 * 1024 * 1024
            binary.unlink()
assert (root / relative).read_bytes() == original, 'production source changed during execution'
print(json.dumps({'source': relative, 'source_sha256': hashlib.sha256(original).hexdigest(),
                  'fixture_sha256': hashlib.sha256(fixture).hexdigest(),
                  'runner_sha256': hashlib.sha256((HERE / 'run.py').read_bytes()).hexdigest(),
                  'arch': args.arch, 'variants': list(variants), 'idle_modes': [0, 1],
                  'scope': 'complete production method; mocked native/queue endpoints and StallScope spy; no real threads, trace producer or GPU'}, sort_keys=True))
