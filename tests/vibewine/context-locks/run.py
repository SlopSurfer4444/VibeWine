#!/usr/bin/env python3
"""Exercise verbatim context entries and the production recursive device mutex.

Metal, COM, resource and queue dependencies are observable CPU doubles. No Wine,
renderer, installed files or GPU are exercised. All generated files are temporary.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile


def block(source, marker):
    start = source.index(marker)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path)
    parser.add_argument('--source-ref', help='Read sources from Git without checkout')
    parser.add_argument('--expect-unlocked-baseline', action='store_true')
    parser.add_argument('--arch', choices=('native', 'x86_64'), default='native')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    source = (args.source_dir or tests.parents[2]).resolve()
    paths = ['src/d3d11/d3d11_context_imm.cpp', 'src/d3d11/d3d11_context_impl.cpp',
             'src/d3d11/d3d11_multithread.hpp', 'src/d3d11/d3d11_multithread.cpp', 'src/util/thread.hpp']
    contents = {path: (subprocess.check_output(['git', '-C', str(source), 'show', f'{args.source_ref}:{path}'], text=True)
                       if args.source_ref else (source / path).read_text()) for path in paths}
    imm, ext, header, mutex, thread = (contents[path] for path in paths)
    replacements = {
        'MUTEX_DECL': block(header, 'class d3d11_device_mutex {') + ';',
        'MUTEX_IMPL': '\n'.join(block(mutex, marker) for marker in [
            'void d3d11_device_mutex::lock()', 'void d3d11_device_mutex::unlock()',
            'bool d3d11_device_mutex::set_protected(', 'bool d3d11_device_mutex::get_protected()']),
        'NULL_MUTEX': block(thread, 'struct null_mutex {') + ';',
        'WAIT_IDLE': block(imm, '  virtual void\n  WaitUntilGPUIdle() override'),
        'CACHE_DECL': block(ext, '  class CachedTemporalScaler {') + ';',
        'FORMAT_METHOD': block(ext, '  WMTPixelFormat GetCorrectMotionVectorFormat('),
        'EXT_METHODS': '\n'.join(block(ext, marker) for marker in [
            '  void STDMETHODCALLTYPE TemporalUpscale(', '  void STDMETHODCALLTYPE BeginUAVOverlap()',
            '  void STDMETHODCALLTYPE EndUAVOverlap()']),
    }
    generated = (tests / 'fixture.cpp').read_text()
    for marker, replacement in replacements.items():
        token = '// ' + marker
        assert generated.count(token) == 1
        generated = generated.replace(token, replacement)
    compiler = shutil.which('clang++') or shutil.which('c++')
    if not compiler:
        parser.error('C++ compiler unavailable')
    with tempfile.TemporaryDirectory(prefix='dxmt-context-locks-') as directory:
        scratch = Path(directory)
        (scratch / 'fixture.cpp').write_text(generated)
        command = [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-O1', '-g', '-pthread',
                   str(scratch / 'fixture.cpp'), '-o', str(scratch / 'fixture')]
        if args.arch == 'x86_64':
            command += ['-arch', 'x86_64']
        if args.sanitize:
            command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        subprocess.run(command, check=True, timeout=60)
        run = [str(scratch / 'fixture')]
        if args.expect_unlocked_baseline:
            run += ['baseline']
        if args.arch == 'x86_64' and platform.machine() == 'arm64':
            run = ['/usr/bin/arch', '-x86_64'] + run
        completed = subprocess.run(run, text=True, capture_output=True, timeout=20)
        print(completed.stdout, end='')
        print(completed.stderr, end='')
        completed.check_returncode()
    print(json.dumps({'scope': 'CPU context lock contract; no Wine/Metal/GPU/installed changes',
                      'source': str(source), 'source_ref': args.source_ref, 'arch': args.arch,
                      'sanitizers': args.sanitize, 'expected_unlocked_baseline': args.expect_unlocked_baseline,
                      'fixture_sha256': hashlib.sha256((tests / 'fixture.cpp').read_bytes()).hexdigest(),
                      'runner_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                      'sources_sha256': {p: hashlib.sha256(c.encode()).hexdigest() for p, c in contents.items()}}, sort_keys=True))


if __name__ == '__main__':
    main()
