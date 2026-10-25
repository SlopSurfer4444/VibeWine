#!/usr/bin/env python3
"""CPU-only production packed-IR allocation/copy/layout/fail-fast checks."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
BASELINE = '96940d73ac28f57fe12df832d246d3e84965f2cd'
HEADER = 'src/airconv/packed_variant_list.hpp'
ADAPTER = '''public:
  struct TestState { const void *data; size_t capacity; size_t used; };
  TestState test_state() const { return {data, capacity, size}; }
  void test_set_size(size_t value) { size = value; }
  void test_set_capacity(size_t value) { capacity = value; }
  void test_ensure(size_t value) { ensure_capacity(value); }
  using variant'''


def instrument(header):
    assert header.count('public:\n  using variant') == 1
    return (header.replace('public:\n  using variant', ADAPTER)
            .replace('std::malloc(', 'test_malloc(')
            .replace('std::memcpy(', 'test_memcpy(')
            .replace('std::free(', 'test_free('))


def run(binary, arch, mode=None):
    command = [str(binary)] + ([mode] if mode else [])
    if arch and platform.machine() != arch:
        command = ['/usr/bin/arch', '-' + arch, *command]
    result = subprocess.run(command, capture_output=True, text=True, timeout=30,
                            env={**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1'})
    output = result.stdout + result.stderr
    assert result.returncode == (77 if mode else 0), f'{binary.name}/{mode}: {result.returncode}\n{output}'
    assert 'runtime error:' not in output and 'ERROR: AddressSanitizer' not in output, output
    if mode:
        assert 'PASS failure observed intact old visitor data before termination' in output
        return {'mode': mode, 'exit': result.returncode, 'observation': output.strip()}
    metric = next(line[len('METRICS '):] for line in output.splitlines() if line.startswith('METRICS '))
    return {'metrics': json.loads(metric),
            'allocations': [int(line[len('ALLOC '):]) for line in output.splitlines() if line.startswith('ALLOC ')]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, default=REPO)
    parser.add_argument('--directx', type=Path, help='Existing populated mingw-directx-headers directory; read-only')
    parser.add_argument('--arch', choices=['arm64', 'x86_64'])
    args = parser.parse_args()
    source = args.source_dir.resolve()
    directx = (args.directx or source / 'include/native/directx').resolve()
    if not (directx / 'd3dcommon.h').is_file():
        parser.error('populate the existing DirectX header dependency or pass --directx; this runner never installs it')
    header = (source / HEADER).read_text()
    historical = subprocess.check_output(['git', '-C', str(source), 'show', f'{BASELINE}:{HEADER}'], text=True)
    meson = (source / 'meson.build').read_text()
    assert "'vibewine_packed_ir_growth' : 'DXMT_PACKED_IR_GROWTH'" in meson
    assert "add_project_arguments(experiment_define, language: ['c', 'cpp'], native: true)" in meson
    assert "option('vibewine_packed_ir_growth', type : 'boolean', value : false," in (source / 'meson.options').read_text()
    results = []
    with tempfile.TemporaryDirectory(prefix='dxmt-packed-ir-growth-') as directory:
        scratch = Path(directory)
        shutil.copy2(source / 'src/airconv/dxbc_instructions.hpp', scratch / 'dxbc_instructions.hpp')
        shutil.copy2(HERE / 'fixture.cpp', scratch / 'fixture.cpp')
        for name, text, defines in [('default', header, []), ('off', header, ['-DDXMT_PACKED_IR_GROWTH=0']),
                                     ('on', header, ['-DDXMT_PACKED_IR_GROWTH=1']), ('historical', historical, [])]:
            (scratch / 'packed_variant_list.hpp').write_text(instrument(text))
            binary = scratch / name
            command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-O1', '-gline-tables-only',
                       '-fno-exceptions', '-fno-rtti', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                       '-fno-omit-frame-pointer', '-ftrivial-auto-var-init=pattern', '-Wall', '-Wextra',
                       '-Werror', '-Wno-missing-braces', '-Wno-unknown-pragmas', '-Wno-unused-private-field', '-Wno-missing-field-initializers',
                       '-I', str(scratch), '-I', str(source / 'src/airconv'), '-I', str(source / 'libs'),
                       '-I', str(source / 'include/native/windows'), '-I', str(directx), *defines,
                       str(scratch / 'fixture.cpp'), '-o', str(binary)]
            if args.arch:
                command += ['-arch', args.arch]
            subprocess.run(command, check=True, timeout=120)
            observation = run(binary, args.arch)
            if name != 'historical':
                observation['failure_cases'] = [run(binary, args.arch, mode) for mode in
                    ['oom-empty', 'oom-growth', 'overflow-tag', 'overflow-payload-alignment',
                     'overflow-payload-size', 'overflow-trailing-alignment', 'overflow-growth', 'growth-largest-safe']]
            result = {'variant': name, **observation}; results.append(result)
            print(json.dumps({'variant': name, 'metrics': observation['metrics'],
                              'failure_case_count': len(observation.get('failure_cases', []))}), flush=True)
        for key in ['metrics', 'allocations']:
            assert results[0][key] == results[1][key] == results[3][key], 'default/off must match historical actual work and layout'
        legacy, enabled = results[0]['metrics'], results[2]['metrics']
        assert enabled['count'] == legacy['count'] and enabled['used'] == legacy['used']
        assert enabled['allocations'] * 8 < legacy['allocations'], 'historical allocation-work discriminator'
        assert enabled['copied'] * 16 < legacy['copied'], 'historical copied-byte-work discriminator'
        # Apply the same improvement admission to independently executed old
        # production growth: correctness passes, work improvement rejects it.
        historical_metrics = results[3]['metrics']
        historical_admitted = (historical_metrics['allocations'] * 8 < legacy['allocations'] and
                               historical_metrics['copied'] * 16 < legacy['copied'])
        assert not historical_admitted, 'historical growth negative control'
        assert sum(p.stat().st_size for p in scratch.iterdir() if p.is_file()) < 64 * 1024 * 1024, '64 MiB scratch cap'
    paths = [HEADER, 'src/airconv/dxbc_instructions.hpp', 'meson.build', 'meson.options']
    print(json.dumps({'arch': args.arch or platform.machine(), 'baseline_ref': BASELINE,
                      'scope': 'actual production template and InstructionList; test-only allocation/copy interception and state injection; no compiler output or FPS',
                      'sources_sha256': {p: hashlib.sha256((source / p).read_bytes()).hexdigest() for p in paths},
                      'directx_d3dcommon_sha256': hashlib.sha256((directx / 'd3dcommon.h').read_bytes()).hexdigest(),
                      'fixture_sha256': hashlib.sha256((HERE / 'fixture.cpp').read_bytes()).hexdigest(),
                      'runner_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                      'results': results}, indent=2))


if __name__ == '__main__':
    main()
