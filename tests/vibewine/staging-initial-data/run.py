#!/usr/bin/env python3
"""Run exact staging creation/layout bodies with CPU Metal/COM adapters."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
BASELINE = 'e3845197978e3fde46a07b584073ba499fd0b25b'
PATHS = ['src/d3d11/d3d11_resource_staging.cpp',
         'src/d3d11/d3d11_resource_helper.cpp', 'src/d3d11/d3d11_enumerable.cpp']
TYPES = ['D3D11_TEXTURE1D_DESC', 'D3D11_TEXTURE2D_DESC1', 'D3D11_TEXTURE3D_DESC1']


def block(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    depth, cursor = 1, opening + 1
    while depth:
        depth += (text[cursor] == '{') - (text[cursor] == '}')
        cursor += 1
    return text[start:cursor]


def sources(root, ref=None):
    return {path: (subprocess.check_output(['git', '-C', str(root), 'show', f'{ref}:{path}'], text=True)
                   if ref else (root / path).read_text()) for path in PATHS}


def generate(contents):
    staging, helper, enumerable = (contents[path] for path in PATHS)
    replacements = {
        'MIPMAPS': '\n'.join(block(helper, f'template <>\nvoid GetMipmapSize(const {kind} *') for kind in TYPES),
        'LAYOUTS': '\n'.join(block(helper, 'template <>\nHRESULT\nGetLinearTextureLayout(\n'
                                  f'    MTLD3D11Device *pDevice, const {kind} &') for kind in TYPES),
        'ENUMERATORS': '\n'.join(block(enumerable, 'generator<SUBRESOURCE_ENTRY>\n'
                                      f'EnumerateSubresources(const {kind} &') for kind in TYPES),
        'CREATE': block(staging, 'template <typename tag>\nHRESULT CreateStagingTextureInternal('),
    }
    result = (HERE / 'fixture.cpp').read_text()
    for key, value in replacements.items():
        token = '// ' + key + '\n'
        assert result.count(token) == 1, key
        result = result.replace(token, value + '\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, default=REPO)
    parser.add_argument('--baseline-ref', default=BASELINE)
    parser.add_argument('--arch', choices=['native', 'arm64', 'x86_64'], default='native')
    args = parser.parse_args()
    compiler = shutil.which('clang++')
    if not compiler:
        parser.error('clang++ unavailable')
    current = sources(args.source_dir)
    baseline = sources(args.source_dir, args.baseline_ref)
    variants = [('current', current, True), ('baseline-1d', baseline, False),
                ('baseline-full', baseline, False)]
    # This deliberately incorrect repair substitutes aligned length for logical
    # length; ASan must catch the tight 1D source overread, not just a sentinel diff.
    bad = dict(current)
    before = 'copy_bpi, copy_len, false)'
    assert bad[PATHS[0]].count(before) == 1
    bad[PATHS[0]] = bad[PATHS[0]].replace(before, 'copy_bpi, copy_len, true)')
    variants.append(('aligned-overread', bad, False))
    results = []
    with tempfile.TemporaryDirectory(prefix='dxmt-staging-initial-data-') as directory:
        scratch = Path(directory)
        for name, contents, accept in variants:
            cpp, binary = scratch / (name + '.cpp'), scratch / name
            cpp.write_text(generate(contents))
            command = [compiler, '-std=c++20', '-O0', '-gline-tables-only', '-Wall', '-Wextra', '-Werror',
                       '-Wno-unused-variable', '-Wno-unknown-pragmas', '-fsanitize=address,undefined',
                       '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-I', str(args.source_dir / 'include'),
                       str(cpp), '-o', str(binary)]
            if args.arch != 'native':
                command.extend(['-arch', args.arch])
            subprocess.run(command, check=True, timeout=60)
            run = [str(binary)]
            if args.arch == 'x86_64' and platform.machine() == 'arm64':
                run = ['/usr/bin/arch', '-x86_64', *run]
            if name == 'baseline-1d':
                run.append('--one-only')
            result = subprocess.run(run, text=True, capture_output=True, timeout=30)
            output = result.stdout + result.stderr
            assert (result.returncode == 0) == accept, f'{name}: unexpected exit={result.returncode}\n{output}'
            if name == 'baseline-1d':
                assert result.returncode == 1 and 'CONTRACT:' in output, output
                assert 'AddressSanitizer' not in output and 'runtime error:' not in output, output
            elif name in ('aligned-overread', 'baseline-full'):
                assert 'AddressSanitizer: heap-buffer-overflow' in output, output
                if name == 'baseline-full':
                    assert 'CONTRACT: 1D tight' in output, output
            record = {'variant': name, 'exit': result.returncode, 'expected_accept': accept, 'output': output.strip()}
            results.append(record)
            print(json.dumps(record), flush=True)
    print(json.dumps({'scope': 'Production CreateStagingTextureInternal, mip/layout and enumeration bodies; CPU adapters',
                      'arch': args.arch, 'host_arch': platform.machine(), 'sanitizers': ['address', 'undefined'],
                      'baseline_ref': args.baseline_ref,
                      'source_sha256': {path: hashlib.sha256(value.encode()).hexdigest() for path, value in current.items()},
                      'fixture_sha256': hashlib.sha256((HERE / 'fixture.cpp').read_bytes()).hexdigest(),
                      'runner_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                      'results': results}, indent=2))


if __name__ == '__main__':
    main()
