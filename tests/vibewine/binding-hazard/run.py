#!/usr/bin/env python3
"""Compile actual BindingSet/Com with production SRV/VB definitions and tiny resources."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tempfile

CASES = ['srv-false-true', 'srv-true-false', 'vb-false-true', 'vb-true-false',
         'srv-false-false', 'srv-true-true', 'vb-false-false', 'vb-true-true',
         'srv-redundant', 'vb-redundant', 'sparse-masks-unbind', 'move', 'default-hazard', 'incoming-reference-move',
         'srv-slot127-false-true', 'srv-slot127-true-false']
NEGATIVES = set(CASES[:4] + ['sparse-masks-unbind'] + CASES[-2:])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch', choices=('native', 'x86_64'), default='native')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--baseline-ref', help='Require transition failures against this immutable old header')
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    source = tests.parents[2]
    paths = ['src/dxmt/dxmt_binding_set.hpp', 'src/d3d11/d3d11_context_state.hpp',
             'src/util/com/com_pointer.hpp', 'src/util/util_bit.hpp']
    contents = {path: (source / path).read_text() for path in paths}
    if args.baseline_ref:
        contents[paths[0]] = subprocess.check_output(
            ['git', '-C', str(source), 'show', f'{args.baseline_ref}:{paths[0]}'], text=True)
    state = contents[paths[1]]
    declarations = []
    # Exact production fields, typedef and redundancy traits, without unrelated
    # D3D11/Metal classes. Fail closed if either source section changes shape.
    for name in ('SRV_B', 'VERTEX_BUFFER_B'):
        pattern = (rf'struct {name} \{{.*?\n\}};\n(?:\ntypedef [^\n]+;\n)?\n'
                   rf'template <> struct redundant_binding_trait<{name}> \{{.*?\n\}};')
        matches = re.findall(pattern, state, re.S)
        if len(matches) != 1:
            raise RuntimeError(f'Expected exactly one production declaration/trait for {name}')
        declarations.append(matches[0])
    with tempfile.TemporaryDirectory(prefix='dxmt-binding-hazard-') as directory:
        scratch = Path(directory)
        (scratch / 'dxmt_binding_set.hpp').write_text(contents[paths[0]])
        (scratch / 'production-bindings.hpp').write_text('namespace dxmt {\n' + '\n'.join(declarations) + '\n}\n')
        # Production Com is included unchanged. Native-only fixture does not
        # exercise QueryInterface/public COM; only private AddRef/Release methods.
        (scratch / 'unknwn.h').write_text('#pragma once\nstruct IUnknown { long QueryInterface(int, void **); };\n'
            '#define SUCCEEDED(value) ((value) >= 0)\n#define __uuidof(...) 0\n')
        command = [shutil.which('clang++') or 'c++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                   '-Wno-missing-field-initializers', '-O1', '-g',
                   '-I', str(scratch), '-I', str(source / 'src/util'),
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
            if expected == 2 and not result.stderr.startswith(f'FAIL {case}: hazard_slots(set) == '):
                raise RuntimeError(f'{case}: baseline failed for an unrelated reason')
        print(json.dumps({'arch': args.arch, 'sanitize': args.sanitize,
              'baseline_ref': args.baseline_ref, 'cases': len(CASES),
              'expected_negative_cases': len(NEGATIVES) if args.baseline_ref else 0,
              'source_sha256': {path: hashlib.sha256(value.encode()).hexdigest()
                                for path, value in contents.items()}}, sort_keys=True))


if __name__ == '__main__':
    main()
