#!/usr/bin/env python3
"""CPU-only production scheduler/header checks; no runtime, cache, GPU or game."""
from pathlib import Path
import argparse
import os
import platform
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--tsan', action='store_true')
parser.add_argument('--arch', choices=['arm64', 'x86_64'])
args = parser.parse_args()
here = Path(__file__).resolve().parent
source = here.parents[2]
shared = here.parent / 'compiler-persistence'
# Guard the actual production config-to-scheduler connection as well as the
# compiled headers. Config's existing string lookup is deliberately unchanged.
cache = (source / 'src/d3d11/d3d11_pipeline_cache.cpp').read_text()
assert 'parse_compiler_worker_limit(Config::getInstance().getOption<std::string>("d3d11.maxCompilerThreads", ""))' in cache
with tempfile.TemporaryDirectory(prefix='dxmt-compiler-budget-') as directory:
    work = Path(directory)
    for name in ['thread.hpp', 'util_win32_compat.h']:
        (work / name).write_text('#include "mocks.hpp"\n')
    binary = work / 'fixture'
    sanitizer = 'thread' if args.tsan else 'address,undefined'
    command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-O1', '-g',
               '-pthread', '-fsanitize=' + sanitizer,
               '-I', str(work), '-I', str(shared), '-I', str(source / 'src/dxmt'),
               str(here / 'fixture.cpp'), '-o', str(binary)]
    if args.arch:
        command += ['-arch', args.arch]
    subprocess.run(command, check=True, timeout=60)
    if sum(path.stat().st_size for path in work.rglob('*') if path.is_file()) > 64 * 1024 * 1024:
        raise RuntimeError('fixture scratch exceeds 64 MiB budget')
    launch = [str(binary)]
    if args.arch and args.arch != platform.machine():
        launch = ['/usr/bin/arch', '-' + args.arch, str(binary)]
    subprocess.run(launch, check=True, timeout=30, env={
        **os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1',
        'TSAN_OPTIONS': 'symbolize=0:abort_on_error=0:halt_on_error=1:exitcode=66'})
