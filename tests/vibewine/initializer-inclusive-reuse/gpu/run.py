#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Bounded offscreen real Metal check of the extracted production upload ring/boundary."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import platform
import shutil
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
PATHS = ('src/dxmt/dxmt_ring_bump_allocator.hpp', 'src/dxmt/dxmt_resource_initializer.hpp')


def extract_function(text, marker):
    start = text.index(marker)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, default=ROOT)
    args = parser.parse_args()
    if platform.system() != 'Darwin' or not shutil.which('sandbox-exec'):
        parser.error('macOS and sandbox-exec required; no unsandboxed fallback')
    bounded_path = ROOT / 'tests/vibewine/gpu-wave/build.py'
    spec = importlib.util.spec_from_file_location('bounded_gpu', bounded_path)
    bounded = importlib.util.module_from_spec(spec)
    previous_bytecode = sys.dont_write_bytecode
    try:
        sys.dont_write_bytecode = True
        spec.loader.exec_module(bounded)
    finally:
        sys.dont_write_bytecode = previous_bytecode
    source = {name: (args.source_dir / name).read_bytes() for name in PATHS}
    ring = source[PATHS[0]].decode()
    # Native concrete block allocators are replaced by an actual Metal shared-
    # buffer allocator. The entire ring class and method definitions are exact.
    start = ring.index('constexpr size_t kStagingBlockSize')
    allocators = ring.index('class GpuPrivateBufferBlockAllocator')
    methods = ring.index('template <typename Allocator, size_t BlockSize, class mutex>\nbool\n')
    production_ring = 'namespace dxmt {\n' + ring[start:allocators] + ring[methods:]
    boundary = extract_function(source[PATHS[1]].decode(), '  static uint64_t uploadReuseBoundary(')
    template = (HERE / 'fixture.mm.in').read_bytes()
    generated = template.decode().replace('// PRODUCTION_RING', production_ring).replace('// PRODUCTION_BOUNDARY', boundary)
    report = {
        'scope': 'real Metal blits/events/readback with complete production ring template and initializer boundary; custom native buffer allocator; no full initializer or Wine bridge execution',
        'source_sha256': {name: hashlib.sha256(data).hexdigest() for name, data in source.items()},
        'fixture_sha256': hashlib.sha256(template).hexdigest(),
        'runner_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'bounded_helper_sha256': hashlib.sha256(bounded_path.read_bytes()).hexdigest(),
        'generated_sha256': hashlib.sha256(generated.encode()).hexdigest(),
        'results': {},
    }
    with tempfile.TemporaryDirectory(prefix='dxmt-upload-reuse-metal-') as directory:
        scratch = Path(directory).resolve()
        cpp = scratch / 'fixture.mm'
        executable = scratch / 'fixture'
        cpp.write_text(generated)
        command = ['xcrun', 'clang++', '-std=c++20', '-O1', '-g', '-fobjc-arc',
                   '-Wall', '-Wextra', '-Werror', '-DDXMT_INITIALIZER_INCLUSIVE_REUSE=1',
                   str(cpp), '-framework', 'Foundation', '-framework', 'Metal', '-o', str(executable)]
        result = bounded.bounded_command(command, 60)
        print(result.pop('output').decode(errors='replace'), end='')
        report['compile'] = result
        if result['returncode'] or result['timedOut'] or result['outputTruncated']:
            print(json.dumps(report, sort_keys=True))
            return 1
        report['executable_sha256'] = hashlib.sha256(executable.read_bytes()).hexdigest()
        profile = '(version 1)(allow default)(deny network*)(deny file-write*)' + \
            '(allow file-write* (subpath ' + json.dumps(str(scratch)) + '))'
        for name, flags, expected in [('normal', [], 0), ('premature-reuse', ['--premature-reuse'], 20)]:
            result = bounded.bounded_command(['/usr/bin/sandbox-exec', '-p', profile, str(executable), *flags], 30)
            output = result.pop('output').decode(errors='replace')
            print(output, end='')
            report['results'][name] = {**result, 'output': output}
            if result['returncode'] != expected or result['timedOut'] or result['outputTruncated']:
                print(json.dumps(report, sort_keys=True))
                return 1
            if name == 'normal' and ('NATIVE_METAL_RING_BOUNDARY_PASSED' not in output or output.count('PASS planes=') != 2):
                raise RuntimeError('normal completion evidence missing')
            if name == 'premature-reuse' and 'EXPECTED_NEGATIVE premature source reuse corrupted delayed A' not in output:
                raise RuntimeError('negative did not reach its intended oracle')
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
