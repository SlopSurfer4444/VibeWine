#!/usr/bin/env python3
"""Production compute admission/constructor with exact scheduler CPU controls."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import resource
import subprocess
import tempfile


def block(text, marker):
    start = text.index(marker)
    opened = text.index('{', start)
    depth = 1
    end = opened + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, help='Run these tests against another source tree')
    parser.add_argument('--case', choices=['all', 'scheduler', 'compute', 'promotion', 'retry'], default='all')
    parser.add_argument('--tsan', action='store_true')
    parser.add_argument('--arch', choices=['arm64', 'x86_64'])
    parser.add_argument('--keep', type=Path, help='Keep logs and exact source hashes only')
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    here = Path(__file__).resolve().parent
    source = (args.source_root or here.parents[2]).resolve()
    shared = here.parent / 'compiler-persistence'
    paths = [source / 'src/d3d11/d3d11_pipeline_cache.cpp', source / 'src/d3d11/d3d11_pipeline.cpp',
             source / 'src/d3d11/d3d11_pipeline.hpp', source / 'src/d3d11/d3d11_shader.hpp',
             source / 'src/d3d11/d3d11_pso_normalization.hpp', source / 'src/dxmt/dxmt_tasks.hpp',
             source / 'src/dxmt/dxmt_task_policy.hpp', source / 'src/dxmt/dxmt_persistence.hpp',
             source / 'src/airconv/airconv_public.h',
             here / 'fixture.cpp', Path(__file__).resolve(), here.parent / 'demand-scheduler/fixture.cpp',
             shared / 'mocks.hpp']
    snapshot = {path: path.read_bytes() for path in paths}
    cache = paths[0].read_text()
    pipeline = paths[1].read_text()
    shader = paths[3].read_text()
    begin = cache.index('    virtual CompiledShader *get_shader(ShaderVariant variant) {')
    end = cache.index('    virtual const Sha1Digest &sha1()', begin)
    get_compute_start = cache.index('  void GetComputePipeline(MTL_COMPUTE_PIPELINE_DESC *pDesc,')
    get_compute_end = cache.index('\npublic:\n', get_compute_start)
    receipt = {'scope': 'CPU exact production methods/constructor/header; no Wine/Metal/GPU/install',
               'case': args.case, 'arch': args.arch or platform.machine(),
               'sanitizer': 'thread' if args.tsan else 'address,undefined',
               'source_sha256': {str(path): hashlib.sha256(content).hexdigest() for path, content in snapshot.items()}}
    logs = {}
    with tempfile.TemporaryDirectory(prefix='dxmt-compute-prewarm-') as directory:
        work = Path(directory)
        for name in ['thread.hpp', 'util_win32_compat.h']:
            (work / name).write_text('#include "mocks.hpp"\n')
        (work / 'cached_lookup.inc').write_text(cache[begin:end])
        (work / 'pixel_variant.inc').write_text(block(shader, 'struct ShaderVariantPixel {') + ';\n')
        (work / 'compute_add.inc').write_text(block(cache, '  virtual HRESULT AddComputeShader('))
        (work / 'compute_lookup.inc').write_text(cache[get_compute_start:get_compute_end])
        (work / 'compute_constructor.inc').write_text(block(pipeline, '  MTLCompiledComputePipelineImpl('))
        factory = block(pipeline, 'std::unique_ptr<MTLCompiledComputePipeline>\nCreateComputePipeline(')
        (work / 'compute_factory.inc').write_text(factory)
        declaration_header = paths[2].read_text()
        declaration_start = declaration_header.index('std::unique_ptr<MTLCompiledComputePipeline> CreateComputePipeline(')
        declaration_end = declaration_header.index(';', declaration_start) + 1
        (work / 'compute_factory_decl.inc').write_text(declaration_header[declaration_start:declaration_end])
        binary = work / 'fixture'
        command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-O1', '-g', '-pthread',
                   '-fsanitize=' + receipt['sanitizer'], '-fno-omit-frame-pointer',
                   '-I', str(work), '-I', str(shared), '-I', str(source / 'src/dxmt'),
                   '-I', str(source / 'src/d3d11'), '-I', str(source / 'src/airconv'),
                   str(here / 'fixture.cpp'), '-o', str(binary)]
        if 'CompiledShader *compiled_shader' not in factory:
            command.append('-DFIXTURE_BASELINE_FACTORY=1')
        if args.arch:
            command += ['-arch', args.arch]
        built = subprocess.run(command, capture_output=True, text=True, timeout=60)
        logs['build.txt'] = built.stdout + built.stderr
        print(logs['build.txt'], end='')
        receipt['build_exit'] = built.returncode
        if built.returncode == 0:
            scratch_bytes = sum(path.stat().st_size for path in work.rglob('*') if path.is_file())
            if scratch_bytes > 64 * 1024 * 1024:
                raise RuntimeError('fixture scratch exceeds 64 MiB')
            launch = [str(binary), args.case]
            if args.arch and args.arch != platform.machine():
                launch = ['/usr/bin/arch', '-' + args.arch, *launch]
            result = subprocess.run(launch, capture_output=True, text=True, timeout=45,
                                    env={**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1',
                                         'TSAN_OPTIONS': 'symbolize=0:abort_on_error=0:halt_on_error=1:exitcode=66'})
            logs['stdout.txt'], logs['stderr.txt'] = result.stdout, result.stderr
            print(result.stdout, end='')
            print(result.stderr, end='')
            receipt['test_exit'] = result.returncode
        else:
            receipt['test_exit'] = None
    if not all(path.read_bytes() == contents for path, contents in snapshot.items()):
        raise RuntimeError('tested sources changed during test; discard receipt')
    if args.keep:
        args.keep.mkdir(parents=True, exist_ok=True)
        for name, content in logs.items():
            (args.keep / name).write_text(content)
        (args.keep / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt, sort_keys=True))
    return receipt['build_exit'] or receipt['test_exit']


if __name__ == '__main__':
    raise SystemExit(main())
