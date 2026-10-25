#!/usr/bin/env python3
"""Compile exact production graphics/SO descriptors and hash/equality bodies."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tempfile

CASES = ['layout-canonicalization', 'cache-separation', 'every-semantic-field',
         'equivalent-default', 'padding-inactive']
NEGATIVES = {
    'cache-separation': 'first != second && first == again && cache.size() == 2',
    'every-semantic-field': '!std::equal_to<Key>{}(key(a), key(b))',
    'equivalent-default': '!std::equal_to<Key>{}(no_so, with_so)',
}


def section(source, pattern):
    matches = re.findall(pattern, source, re.S)
    if len(matches) != 1:
        raise RuntimeError(f'Expected one production section for {pattern}, got {len(matches)}')
    return matches[0]


def structure(source, name):
    return section(source, rf'struct {name} \{{.*?\n\}};')


def specialization(source, kind, name):
    return section(source, rf'template <> struct {kind}<{name}> \{{.*?\n\}};')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch', choices=('native', 'x86_64'), default='native')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--baseline-ref', help='Require old key cache-aliasing failures at this immutable ref')
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    source = tests.parents[2]
    paths = ['src/d3d11/d3d11_pipeline.hpp', 'src/d3d11/d3d11_input_layout.hpp', 'src/util/util_hash.hpp',
             'src/winemetal/winemetal.h', 'src/airconv/airconv_public.h']
    contents = {path: (source / path).read_text() for path in paths}
    if args.baseline_ref:
        contents[paths[0]] = subprocess.check_output(
            ['git', '-C', str(source), 'show', f'{args.baseline_ref}:{paths[0]}'], text=True)
    pso, so = contents[paths[0]], contents[paths[1]]
    with tempfile.TemporaryDirectory(prefix='dxmt-pso-solayout-') as directory:
        scratch = Path(directory)
        enums = '\n'.join(section(contents[path], rf'(?m)^enum {name}[^{{}}\n]*\{{.*?\n\}};')
                          for path, name in [(paths[3], 'WMTPixelFormat'),
                                             (paths[3], 'WMTPrimitiveTopologyClass'),
                                             (paths[4], 'SM50_INDEX_BUFFER_FORMAT')])
        (scratch / 'production-enums.hpp').write_text(enums + '\n')
        for header, text, name, prefix in (
            ('production-pso-key.hpp', pso, 'MTL_GRAPHICS_PIPELINE_DESC', ''),
            ('production-so-desc.hpp', so, 'MTL_STREAM_OUTPUT_DESC',
             structure(so, 'MTL_SHADER_STREAM_OUTPUT_ELEMENT_DESC') + '\n')):
            generated = prefix + structure(text, name) + '\nnamespace std {\n'
            generated += '\n'.join(specialization(text, kind, name) for kind in ('hash', 'equal_to'))
            (scratch / header).write_text(generated + '\n}\n')
        command = [shutil.which('clang++') or 'c++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                   '-O1', '-g', '-I', str(scratch), '-I', str(source / 'src/util'),
                   str(tests / 'fixture.cpp'), '-o', str(scratch / 'fixture')]
        if args.arch == 'x86_64':
            command += ['-arch', 'x86_64']
        if args.sanitize:
            command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        subprocess.run(command, check=True, timeout=60)
        run = [str(scratch / 'fixture')]
        if args.arch == 'x86_64' and platform.machine() == 'arm64':
            run = ['/usr/bin/arch', '-x86_64'] + run
        for case in CASES:
            result = subprocess.run(run + [case], timeout=20, capture_output=True, text=True)
            print(result.stdout, end='')
            print(result.stderr, end='')
            expected = 2 if args.baseline_ref and case in NEGATIVES else 0
            if result.returncode != expected:
                raise RuntimeError(f'{case}: exit {result.returncode}, expected {expected}')
            if expected == 2 and result.stderr != f'FAIL {case}: {NEGATIVES[case]}\n':
                raise RuntimeError(f'{case}: baseline failed for an unrelated reason')
        print(json.dumps({'arch': args.arch, 'sanitize': args.sanitize,
              'baseline_ref': args.baseline_ref, 'cases': len(CASES),
              'expected_negative_cases': len(NEGATIVES) if args.baseline_ref else 0,
              'source_sha256': {path: hashlib.sha256(value.encode()).hexdigest()
                                for path, value in contents.items()}}, sort_keys=True))


if __name__ == '__main__':
    main()
