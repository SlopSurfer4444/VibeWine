#!/usr/bin/env python3
"""Exact production headers with CPU-only platform/native cache shims.

No runtime, game, GPU or live database access. Scratch is private and removed.
"""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--tsan', action='store_true')
args = parser.parse_args()
here = Path(__file__).resolve().parent
source = here.parents[2]
with tempfile.TemporaryDirectory(prefix='dxmt-compiler-persistence-') as directory:
    work = Path(directory)
    for name in ['thread.hpp', 'util_win32_compat.h', 'Metal.hpp']:
        (work / name).write_text('#include "mocks.hpp"\n')
    (work / 'compiler_cache_compat.hpp').write_text(
        '#pragma once\nconstexpr uint64_t kDXMTCompilerCacheCompatibility = 0x1a00000002ull;\n')
    sanitizer = 'thread' if args.tsan else 'address,undefined'
    environment = {**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1',
                   'TSAN_OPTIONS': 'symbolize=0:abort_on_error=0:halt_on_error=1:exitcode=66'}
    shader_source = (source / 'src/d3d11/d3d11_shader.cpp').read_text()
    begin = shader_source.index('template <typename Proc>')
    end = shader_source.index('\ntemplate <>', begin)
    shader_fixture = work / 'shader_fixture.cpp'
    shader_fixture.write_text((here / 'shader_fixture.cpp.in').read_text().replace(
        '// EXACT_GENERAL_SHADER_TASK', shader_source[begin:end]))
    for fixture in [here / 'fixture.cpp', shader_fixture]:
        binary = work / fixture.stem
        command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-O1', '-g', '-Wno-inconsistent-missing-override',
                   '-pthread', '-fsanitize=' + sanitizer,
                   '-I', str(work), '-I', str(here), '-I', str(source / 'src/dxmt'),
                   str(fixture), '-o', str(binary)]
        subprocess.run(command, check=True, timeout=30)
        subprocess.run([str(binary)], check=True, timeout=20, env=environment)
