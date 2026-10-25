#!/usr/bin/env python3
"""CPU-only exact-header scheduler fixture; private temporary binaries only."""
from pathlib import Path
import argparse
import os
import platform
import resource
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--tsan', action='store_true')
parser.add_argument('--arch', choices=['arm64', 'x86_64'])
args = parser.parse_args()
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
here = Path(__file__).resolve().parent
source = here.parents[2]
shared = here.parent / 'compiler-persistence'
with tempfile.TemporaryDirectory(prefix='dxmt-demand-scheduler-') as directory:
    work = Path(directory)
    for name in ['thread.hpp', 'util_win32_compat.h']:
        (work / name).write_text('#include "mocks.hpp"\n')
    cache_source = (source / 'src/d3d11/d3d11_pipeline_cache.cpp').read_text()
    begin = cache_source.index('    virtual CompiledShader *get_shader(ShaderVariant variant) {')
    end = cache_source.index('    virtual const Sha1Digest &sha1()', begin)
    (work / 'cached_lookup.inc').write_text(cache_source[begin:end])
    sanitizer = 'thread' if args.tsan else 'address,undefined'
    binary = work / 'fixture'
    command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-O1', '-g',
               '-pthread', '-fsanitize=' + sanitizer,
               '-I', str(work), '-I', str(shared), '-I', str(source / 'src/dxmt'),
               str(here / 'fixture.cpp'), '-o', str(binary)]
    if args.arch:
        command += ['-arch', args.arch]
    subprocess.run(command, check=True, timeout=60)
    scratch_bytes = sum(path.stat().st_size for path in work.rglob('*') if path.is_file())
    if scratch_bytes > 64 * 1024 * 1024:
        raise RuntimeError('fixture scratch exceeds the 64 MiB budget')
    environment = {**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1',
                   'TSAN_OPTIONS': 'symbolize=0:abort_on_error=0:halt_on_error=1:exitcode=66'}
    launch = [str(binary)]
    if args.arch and args.arch != platform.machine():
        launch = ['/usr/bin/arch', '-' + args.arch, str(binary)]
    subprocess.run(launch, check=True, timeout=45, env=environment)
