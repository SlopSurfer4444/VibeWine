#!/usr/bin/env python3
"""CPU capture validity checks. Exact current state/queue code; temporary artifacts only."""
from pathlib import Path
import hashlib
import json
import platform
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]

def block(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

def main():
    state = (REPO / 'src/dxmt/dxmt_capture.cpp').read_text()
    state = re.sub(r'^#include [^\n]*\n', '', state, flags=re.M)
    queue = block((REPO / 'src/dxmt/dxmt_command_queue.cpp').read_text(),
                  'switch (capture_state.getNextAction(chunk.frame_))')
    source = (HERE / 'state_fixture.cpp.in').read_text().replace('@CAPTURE_SOURCE@', state).replace('@QUEUE_SWITCH@', queue)
    results = []
    with tempfile.TemporaryDirectory(prefix='dxmt-capture-state-') as scratch:
        cpp = Path(scratch) / 'state.cpp'
        command = ['xcrun', 'clang++', '-std=c++20', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                   '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                   '-Wno-unused-private-field',  # Windows-only key field is absent from host code path.
                   '-I', str(REPO / 'src/winemetal'), '-I', str(REPO / 'src/dxmt')]
        def run(program, name):
            cpp.write_text(program)
            binary = Path(scratch) / name
            subprocess.run([*command, str(cpp), '-o', str(binary)], check=True, timeout=60)
            return subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
        result = run(source, 'state')
        if result.returncode or result.stderr:
            raise RuntimeError(result.stdout + result.stderr)
        results.append(result.stdout.strip())
        other = 'x86_64' if platform.machine() == 'arm64' else 'arm64'
        cpp.write_text(source)
        subprocess.run([*command, '-arch', other, '-c', str(cpp), '-o', str(Path(scratch) / 'other.o')], check=True, timeout=60)
        controls = [
            ('failed_capture_claims_ownership', 'owns_capture_ = success;', 'owns_capture_ = true;'),
            ('failed_capture_stops_other_owner', 'if (capture_state.takeCaptureOwnership())', 'if (true)'),
            ('success_capture_never_stopped', 'capture_state.captureStarted(capture_mgr.startCapture(info));',
             '(void)capture_mgr.startCapture(info); capture_state.captureStarted(false);'),
        ]
        for name, before, after in controls:
            assert source.count(before) == 1, name
            result = run(source.replace(before, after), name)
            if result.returncode != 1 or ': ' not in result.stderr or 'runtime error:' in result.stderr or 'sanitizer' in result.stderr.lower():
                raise RuntimeError('control not rejected cleanly: ' + name + '\n' + result.stdout + result.stderr)
            results.append('PASS rejected mutation ' + name + ': ' + result.stderr.strip())
    print(json.dumps({'scope': 'CPU stubs; no Metal device, trace capture, Wine runtime or game',
                      'sanitizers': ['address', 'undefined'], 'other_architecture_compile': other,
                      'state_sha256': hashlib.sha256(state.encode()).hexdigest(),
                      'queue_switch_sha256': hashlib.sha256(queue.encode()).hexdigest(), 'results': results}, indent=2))
    subprocess.run(['python3', str(HERE / 'test_native.py')], check=True, timeout=180)

if __name__ == '__main__':
    main()
