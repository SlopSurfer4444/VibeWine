#!/usr/bin/env python3
"""Compile production PSO/variant lookups, key types and pixel factory unchanged."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
BASELINE = '8cf59a01'
PATHS = ['src/d3d11/d3d11_pipeline_cache.cpp', 'src/d3d11/d3d11_shader.cpp',
         'src/d3d11/d3d11_pipeline.hpp', 'src/d3d11/d3d11_shader.hpp',
         'src/d3d11/d3d11_pso_normalization.hpp', 'src/airconv/airconv_public.h',
         'src/winemetal/winemetal.h', 'src/util/util_hash.hpp']


def block(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def generated(contents):
    cache, factory, pso, variant = [contents[p] for p in PATHS[:4]]
    variants = variant[variant.index('struct ShaderVariantVertex {'):variant.index('\nclass ThreadpoolWork')]
    enums = '\n'.join(re.search(rf'(?m)^enum {name}[^{{}}\n]*\{{.*?\n\}};', contents[PATHS[6]], re.S)[0]
                      for name in ['WMTPixelFormat', 'WMTPrimitiveTopologyClass'])
    substitutions = {
        'ENUMS': enums,
        'VARIANTS': variants,
        'VARIANT_HASH': block(variant, 'template <> struct hash<dxmt::ShaderVariant>') + ';',
        'PSO_KEY': block(pso, 'struct MTL_GRAPHICS_PIPELINE_DESC') + ';',
        'PSO_HASH_EQUAL': '\n'.join(block(pso, 'template <> struct ' + kind + '<MTL_GRAPHICS_PIPELINE_DESC>') + ';'
                                    for kind in ['hash', 'equal_to']),
        'PIXEL_FACTORY': block(factory, 'template <>\nstd::unique_ptr<CompiledShader>\nCreateVariantShader(MTLD3D11Device *pDevice, ManagedShader shader,\n                    ShaderVariantPixel variant)'),
        # Adapters substitute enclosing class inheritance; bodies stay exact.
        'GRAPHICS_LOOKUP': block(cache, '  void GetGraphicsPipeline(').replace(' override {', ' {'),
        'VARIANT_LOOKUP': block(cache, '    CompiledShader *get_shader(ShaderVariant variant, task_urgency urgency)'),
    }
    code = (HERE / 'normalization.cpp.in').read_text()
    for marker, value in substitutions.items():
        assert code.count('// ' + marker + '\n') == 1
        code = code.replace('// ' + marker + '\n', value + '\n')
    return code


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, default=REPO)
    parser.add_argument('--arch', choices=['arm64', 'x86_64'])
    args = parser.parse_args()
    source = args.source_dir.resolve()
    snapshots = {path: (source / path).read_bytes() for path in PATHS}
    fixture_snapshot = (HERE / 'normalization.cpp.in').read_bytes()
    runner_snapshot = Path(__file__).read_bytes()
    contents = {path: data.decode() for path, data in snapshots.items()}
    historical = dict(contents)
    for path in PATHS[:2]:
        historical[path] = subprocess.check_output(['git', '-C', str(source), 'show', BASELINE + ':' + path], text=True)
    variants = [('current', contents, [], None), ('current-collisions', contents, ['-DFORCE_COLLISIONS'], None),
                ('historical', historical, [], {'ordinary': 'ordinary index formats share one production PSO',
                                               'pixel': 'unused UNORM bits share production variant identity'})]
    # These connected fault controls detect losing the semantic boundary, not
    # merely whether a helper was called by name.
    unguarded = dict(contents)
    unguarded[PATHS[0]] = unguarded[PATHS[0]].replace('auto canonical_desc = canonicalize_ordinary_pipeline_desc(*pDesc);',
        'auto canonical_desc = *pDesc; canonical_desc.IndexBufferFormat = SM50_INDEX_BUFFER_FORMAT_NONE;')
    variants.append(('missing-exclusions', unguarded, [], {'ordinary': 'GS HS DS SO exclusions retain index identity'}))
    no_digest = dict(contents)
    no_digest[PATHS[1]] = no_digest[PATHS[1]].replace(
        '  variant = canonicalize_pixel_variant(variant, shader->reflection().PixelShader.ValidRenderTargets);\n', '')
    variants.append(('noncanonical-factory', no_digest, [], {'pixel': 'direct pixel factory uses canonical AIR digest'}))
    results = []
    with tempfile.TemporaryDirectory(prefix='dxmt-pso-normalization-') as directory:
        scratch = Path(directory)
        common = ['-O1', '-gline-tables-only', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                  '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer',
                  '-I', str(source / 'src/d3d11'), '-I', str(source / 'src/airconv'), '-I', str(source / 'src/util')]
        if args.arch:
            common += ['-arch', args.arch]
        sha = scratch / 'sha1.o'
        subprocess.run([os.environ.get('CC', 'clang'), '-std=c2x', *common, '-c', str(source / 'src/util/sha1/sha1.c'),
                        '-o', str(sha)], check=True, timeout=60)
        for name, sources, defines, rejections in variants:
            cpp, binary = scratch / (name + '.cpp'), scratch / name
            cpp.write_text(generated(sources))
            subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++20', *common, *defines, str(cpp),
                            str(source / 'src/util/sha1/sha1_util.cpp'), str(sha), '-o', str(binary)], check=True, timeout=60)
            launch = [str(binary)]
            if args.arch and platform.machine() != args.arch:
                launch = ['/usr/bin/arch', '-' + args.arch, *launch]
            for case in rejections or ['ordinary', 'pixel']:
                result = subprocess.run([*launch, case], capture_output=True, text=True, timeout=20)
                output = result.stdout + result.stderr
                expected = rejections[case] if rejections else None
                assert result.returncode == (1 if expected else 0), f'{name}/{case}: {result.returncode}\n{output}'
                if expected:
                    assert 'CONTRACT: ' + expected in output, output
                assert 'ERROR: AddressSanitizer' not in output and 'runtime error:' not in output, output
                record = {'variant': name, 'case': case, 'exit': result.returncode,
                          'expected_rejection': expected, 'output': output.strip()}
                results.append(record)
                print(json.dumps(record), flush=True)
        assert sum(p.stat().st_size for p in scratch.iterdir()) < 64 * 1024 * 1024, '64 MiB scratch cap'
    assert all((source / p).read_bytes() == data for p, data in snapshots.items()), 'production sources changed during verification'
    assert (HERE / 'normalization.cpp.in').read_bytes() == fixture_snapshot and Path(__file__).read_bytes() == runner_snapshot, 'test sources changed during verification'
    print(json.dumps({'arch': args.arch or platform.machine(), 'baseline_ref': BASELINE,
                      'scope': 'production methods, keys, factory and compile args; native compiler/PSO calls adapted; no GPU/game/FPS',
                      'sources_sha256': {p: hashlib.sha256(data).hexdigest() for p, data in snapshots.items()},
                      'fixture_sha256': hashlib.sha256(fixture_snapshot).hexdigest(),
                      'runner_sha256': hashlib.sha256(runner_snapshot).hexdigest(), 'results': results}, indent=2))


if __name__ == '__main__':
    main()
