#!/usr/bin/env python3
"""Bounded CPU check of extracted production ShaderVariant declarations/hash.

No runtime, installed files, cache, GPU, game, or Git index changes. Sources and
binaries use a private temporary directory, removed after one invocation.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
HEADER = 'src/d3d11/d3d11_shader.hpp'
BASELINE = '96940d73ac28f57fe12df832d246d3e84965f2cd'


def block(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end] + ';'


def generate(header, hash_override=None):
    start = header.index('struct ShaderVariantVertex {')
    end = header.index('\nclass ThreadpoolWork', start)
    declarations = header[start:end]
    code = (HERE / 'fixture.cpp.in').read_text()
    code = code.replace('// DECLARATIONS', declarations)
    code = code.replace('// HASH', hash_override or block(header, 'template <> struct hash<dxmt::ShaderVariant>'))
    return code


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, default=REPO)
    parser.add_argument('--arch', choices=['arm64', 'x86_64'])
    args = parser.parse_args()
    source = args.source_dir.resolve()
    header = (source / HEADER).read_text()
    old = subprocess.check_output(['git', '-C', str(source), 'show', f'{BASELINE}:{HEADER}'], text=True)
    old_hash = block(old, 'template <> struct hash<dxmt::ShaderVariant>')
    # Historical baseline retains correctness; its index-only collision work is
    # rejected separately with the exact same production declarations/corpus.
    variants = [
        ('current', generate(header), [], None),
        ('historical-correctness', generate(old), ['-DCORRECTNESS_ONLY'], None),
        ('index-only-work-control', generate(header, old_hash), ['-DSKIP_FIELD_HASH'],
         'same-alternative bucket distribution'),
        ('missing-field-control', generate(header.replace('fields(key.sample_mask, key.dual_source_blending,',
                                                         'fields(key.dual_source_blending,')), [],
         'single-field sentinel must affect hash'),
        ('raw-padding-control', generate(header, '''template <> struct hash<dxmt::ShaderVariant> {
          size_t operator()(const dxmt::ShaderVariant &v) const noexcept {
            return std::hash<std::string_view>{}({reinterpret_cast<const char *>(&v), sizeof(v)});
          }
        };'''), [], 'padding must not enter hash'),
    ]
    results = []
    with tempfile.TemporaryDirectory(prefix='dxmt-shader-variant-hash-') as directory:
        scratch = Path(directory)
        for name, code, defines, rejection in variants:
            cpp, binary = scratch / (name + '.cpp'), scratch / name
            cpp.write_text(code)
            command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-O1', '-gline-tables-only',
                       '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                       '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                       '-fno-omit-frame-pointer', '-ftrivial-auto-var-init=pattern',
                       '-I', str(source / 'src/util'), '-I', str(source / 'src/airconv'),
                       *defines, str(cpp), '-o', str(binary)]
            if args.arch:
                command += ['-arch', args.arch]
            subprocess.run(command, check=True, timeout=60)
            launch = [str(binary)]
            if args.arch and platform.machine() != args.arch:
                launch = ['/usr/bin/arch', '-' + args.arch, *launch]
            result = subprocess.run(launch, capture_output=True, text=True, timeout=20,
                                    env={**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1'})
            output = result.stdout + result.stderr
            if rejection:
                assert result.returncode == 1 and 'CONTRACT: ' + rejection in output, f'{name}: wrong rejection {result.returncode}\n{output}'
            else:
                assert result.returncode == 0, f'{name}: {result.returncode}\n{output}'
            assert 'ERROR: AddressSanitizer' not in output and 'runtime error:' not in output, output
            results.append({'variant': name, 'exit': result.returncode, 'expected_rejection': rejection, 'output': output.strip()})
            print(json.dumps(results[-1]), flush=True)
        assert sum(p.stat().st_size for p in scratch.iterdir()) < 64 * 1024 * 1024, '64 MiB scratch cap'
    paths = [HEADER, 'src/util/util_hash.hpp', 'src/airconv/airconv_public.h']
    print(json.dumps({'arch': args.arch or platform.machine(), 'baseline_ref': BASELINE,
                      'scope': 'production hash/equality declarations; CPU unordered_map work counts; no time or FPS claim',
                      'sources_sha256': {p: hashlib.sha256((source / p).read_bytes()).hexdigest() for p in paths},
                      'fixture_sha256': hashlib.sha256((HERE / 'fixture.cpp.in').read_bytes()).hexdigest(),
                      'runner_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                      'results': results}, indent=2))


if __name__ == '__main__':
    main()
