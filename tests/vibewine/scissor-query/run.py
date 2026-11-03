#!/usr/bin/env python3
"""Extract actual PreDraw/setters; compare real wire packet snapshots without a GPU."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SOURCE = 'src/d3d11/d3d11_context_impl.cpp'
BASELINE = 'a422a9324e6c655e43cfeb53423497e3ca390a0e'


def block(text, marker):
    start = text.index(marker)
    cursor = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[cursor] == '{') - (text[cursor] == '}')
        cursor += 1
    return text[start:cursor]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--arch', choices=['arm64', 'x86_64'])
    args = parser.parse_args()
    source = (ROOT / SOURCE).read_text()
    baseline = subprocess.check_output(['git', 'show', BASELINE + ':' + SOURCE], cwd=ROOT, text=True)
    marker = 'template <bool IndexedDraw>\n  DrawCallStatus\n  PreDraw()'
    current = block(source, marker)
    old = block(baseline, marker).replace('PreDraw()', 'PreDrawBaseline()', 1)
    setters = '\n'.join(block(source, 'void\n  STDMETHODCALLTYPE\n  ' + name + '(').replace(' override', '')
                        for name in ['RSSetState', 'RSSetViewports', 'RSSetScissorRects'])
    # Exact production dirty-state statement used on encoder open/reopen. Native
    # render-pass creation, PSO finalization and resource uploads are adapters.
    reopen = block(source, 'bool\n  SwitchToRenderEncoder()')
    start = reopen.index('    dirty_state.set(')
    dirty = reopen[start:reopen.index(';', start) + 1]
    template = (HERE / 'fixture.cpp').read_text()
    common = {'ENUM': block(source, 'enum class DirtyState') + ';',
              'REOPEN_DIRTY': dirty, 'SETTERS': setters, 'BASELINE': old}
    for name, value in common.items():
        template = template.replace('/* ' + name + ' */', value)
    variants = [('baseline', current, ['-DPRIMARY_BASELINE'], 'FAIL stable getter economy'),
                ('candidate', current, [], None),
                ('disabled-scissors', current.replace('bool allow_scissor = current_rs->IsScissorEnabled();',
                                                     'bool allow_scissor = (current_rs->IsScissorEnabled(), false);'),
                 [], 'FAIL enabled scissors')]
    results = {}
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1')
    with tempfile.TemporaryDirectory(prefix='dxmt-scissor-query-') as temp:
        temp = Path(temp)
        for name, predraw, flags, expected in variants:
            generated = temp / (name + '.cpp')
            binary = temp / name
            generated.write_text(template.replace('/* PREDRAW */', predraw))
            command = ['clang++', '-std=c++20', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                       '-Wno-unused-private-field', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                       '-I' + str(ROOT / 'src/winemetal'), '-I' + str(ROOT / 'src/util'), *flags]
            if args.arch:
                command += ['-arch', args.arch]
            subprocess.run(command + [str(generated), '-o', str(binary)], check=True, timeout=60)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20, env=env)
            if expected:
                assert result.returncode == 3 and result.stderr.strip() == expected, (name, result)
            else:
                assert result.returncode == 0 and not result.stderr, (name, result)
            results[name] = {'exit': result.returncode, 'stdout': result.stdout.strip(),
                             'expected_failure': expected}
    paths = [SOURCE, 'src/winemetal/winemetal.h', 'src/util/util_flags.hpp']
    print(json.dumps({'scope': 'actual full PreDraw, actual RS setters, real wire structs; CPU boundaries',
                      'baseline_ref': BASELINE, 'results': results,
                      'source_sha256': {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in paths},
                      'baseline_sha256': hashlib.sha256(baseline.encode()).hexdigest(),
                      'fixture_sha256': hashlib.sha256((HERE / 'fixture.cpp').read_bytes()).hexdigest(),
                      'runner_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}, indent=2))


if __name__ == '__main__':
    main()
