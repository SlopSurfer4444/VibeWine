#!/usr/bin/env python3
"""Production CB range reset, wrappers, encoding and replay with CPU adapters."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BASELINE = 'e3845197978e3fde46a07b584073ba499fd0b25b'
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('precise_bindings', HERE.parent / 'precise-bindings/run.py')
precise = importlib.util.module_from_spec(spec)
spec.loader.exec_module(precise)


def generate(contents):
    source = precise.generate(contents)
    impl = contents['src/d3d11/d3d11_context_impl.cpp']
    methods = [precise.block(impl, '  template <PipelineStage Stage>\n  void\n  GetConstantBuffer(')]
    for prefix in ('VS', 'PS', 'HS', 'DS', 'GS', 'CS'):
        for name in ('SetConstantBuffers', 'SetConstantBuffers1', 'GetConstantBuffers', 'GetConstantBuffers1'):
            methods.append(precise.block(impl, '  void\n  STDMETHODCALLTYPE\n  ' + prefix + name + '(').replace(' override', ''))
    restore_loop = precise.block(impl, '      for (const auto &[slot, entry] : ShaderStage.ConstantBuffers)')
    methods += ['template <PipelineStage Stage> void RestoreEncodingContextStageState() {\n'
                'auto &ShaderStage = state_.ShaderStages[Stage];\n' + restore_loop + '\n}',
                'void RestoreEncodingContextState() {\n' + '\n'.join(
                    f'RestoreEncodingContextStageState<PipelineStage::{stage}>();'
                    for stage in ('Vertex', 'Pixel', 'Hull', 'Domain', 'Geometry', 'Compute')) + '\n}',
                'void ResetD3D11ContextState() { state_ = ContextState{}; }']
    for file, flag, name in (('imm', 'RestoreContextState', 'execute_boundary'),
                             ('def', 'RestoreDeferredContextState', 'finish_boundary')):
        text = contents[f'src/d3d11/d3d11_context_{file}.cpp']
        start = text.index('    if (' + flag + ')')
        end = text.index('ResetD3D11ContextState();', start) + len('ResetD3D11ContextState();')
        methods.append(f'void {name}(bool {flag}) {{\n' + text[start:end] + '\n}')
    source = source.replace('  template <PipelineStage stage> void bind(', '\n'.join(methods) + '\n  template <PipelineStage stage> void bind(', 1)
    source = source.replace('#define ERR', '#define STDMETHODCALLTYPE\n#define IID_PPV_ARGS(p) p\n#define ERR', 1)
    source = source.replace('  void GetDesc(D3D11_BUFFER_DESC *desc)',
                            '  void QueryInterface(D3D11ResourceCommon **out) { incRef(); *out = this; }\n  void GetDesc(D3D11_BUFFER_DESC *desc)', 1)
    # Adapter-only storage changes: 32 constants, including the valid 16/16 view.
    for old, new in [('UINT ByteWidth = 256', 'UINT ByteWidth = 512'),
                     ('uint8_t, 768', 'uint8_t, 1536'), ('sub * 256', 'sub * 512'),
                     ('suballocation * 256', 'suballocation * 512'),
                     ('unsigned length() { return 256;', 'unsigned length() { return 512;'),
                     ('*length = 256;', '*length = 512;'),
                     ('allocation->gpuAddress() + 768', 'allocation->gpuAddress() + 1536')]:
        assert old in source, old
        source = source.replace(old, new)
    # Keep inherited adapters and production extractions, replace its test main.
    source = source[:source.index('int main() {')]
    return source + (HERE / 'fixture.cpp').read_text()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch', choices=('native', 'x86_64'), default='native')
    parser.add_argument('--source-dir', type=Path, default=ROOT)
    parser.add_argument('--baseline-ref', default=BASELINE)
    args = parser.parse_args()
    compiler = shutil.which('clang++') or shutil.which('c++')
    if not compiler:
        parser.error('C++ compiler unavailable')
    current = precise.sources(args.source_dir)
    baseline = dict(current)
    path = 'src/d3d11/d3d11_context_impl.cpp'
    baseline[path] = subprocess.check_output(['git', '-C', str(args.source_dir), 'show', f'{args.baseline_ref}:{path}'], text=True)
    variants = [('default', current, [], True, [], ''),
                ('explicit-off', current, ['-DDXMT_PRECISE_BINDINGS=0'], True, [], ''),
                ('enabled', current, ['-DDXMT_PRECISE_BINDINGS=1'], True, [], ''),
                ('baseline', baseline, ['-DDXMT_PRECISE_BINDINGS=1'], False, [],
                 'same-buffer whole-range reset metadata')]
    for name, before, after, mode, expected in (
        ('offset-drop', 'enc.bindConstantBufferOffset<Stage>(slot, offset);',
         '(void)enc; (void)slot; (void)offset;', [], 'stale CB data/address/offset'),
        ('count-update-drop', 'entry.NumConstants = num_constants;', '(void)num_constants;',
         ['count-only'], 'count-only stored count'),
        ('count-dirty-drop', 'ShaderStage.ConstantBuffers.set_dirty(slot);\n            entry.NumConstants = num_constants;',
         'entry.NumConstants = num_constants;', ['count-only'], 'count-only update dirty bit')):
        mutated = dict(current)
        setter = precise.block(mutated[path], '  template <PipelineStage Stage>\n  void\n  SetConstantBuffer(')
        assert before in setter, name
        changed = setter.replace(before, after, 1)
        mutated[path] = mutated[path].replace(setter, changed, 1)
        variants.append((name, mutated, ['-DDXMT_PRECISE_BINDINGS=1'], False, mode, expected))
    results = []
    with tempfile.TemporaryDirectory(prefix='dxmt-cb-range-reset-') as directory:
        scratch = Path(directory)
        for name, contents, defines, accepted, mode, expected in variants:
            cpp, binary = scratch / (name + '.cpp'), scratch / name
            cpp.write_text(generate(contents))
            command = [compiler, '-std=c++20', '-O0', '-gline-tables-only', '-Wall', '-Wextra', '-Werror',
                       '-Wno-unused-variable', '-Wno-unused-parameter', '-Wno-missing-field-initializers',
                       '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer',
                       '-pthread', '-DEXPECT_PRECISE=1', *defines]
            for folder in ('util', 'dxmt', 'airconv'):
                command += ['-I', str(args.source_dir / 'src' / folder)]
            command += [str(cpp), '-o', str(binary)]
            if args.arch == 'x86_64':
                command += ['-arch', 'x86_64']
            subprocess.run(command, check=True, timeout=60)
            run = [str(binary), *mode]
            if args.arch == 'x86_64' and platform.machine() == 'arm64':
                run = ['/usr/bin/arch', '-x86_64', *run]
            result = subprocess.run(run, capture_output=True, text=True, timeout=20)
            output = result.stdout + result.stderr
            assert (result.returncode == 0) == accepted, f'{name}: unexpected exit {result.returncode}\n{output}'
            if not accepted:
                assert result.returncode == 1 and 'CONTRACT: ' + expected in output, output
                assert 'ERROR: AddressSanitizer' not in output and 'runtime error:' not in output, output
            results.append({'variant': name, 'accepted': accepted, 'exit': result.returncode, 'output': output.strip()})
            print(json.dumps(results[-1]), flush=True)
    print(json.dumps({'scope': 'CPU production CB wrappers, setter/getter, encoding, replay and restore branch/loop',
                      'arch': args.arch, 'sanitizers': ['address', 'undefined'], 'baseline_ref': args.baseline_ref,
                      'sources_sha256': {path: hashlib.sha256(text.encode()).hexdigest() for path, text in current.items()},
                      'fixture_sha256': hashlib.sha256((HERE / 'fixture.cpp').read_bytes()).hexdigest(),
                      'runner_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                      'adapter_fixture_sha256': hashlib.sha256((HERE.parent / 'precise-bindings/fixture.cpp').read_bytes()).hexdigest(),
                      'adapter_runner_sha256': hashlib.sha256((HERE.parent / 'precise-bindings/run.py').read_bytes()).hexdigest(),
                      'results': results}, indent=2))


if __name__ == '__main__':
    main()
