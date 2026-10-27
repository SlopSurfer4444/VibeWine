#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run actual GetDXGIUsage and IDXGIResource forwarding bodies with real D3D headers."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
RESOURCE = 'src/d3d11/d3d11_resource.hpp'
FORWARD = 'src/dxgi/dxgi_resource.hpp'
MODES = ('shader', 'render', 'unordered', 'combined', 'cpu-bindings', 'controls', 'shared')
CLASSES = ('buffer', 'texture1d', 'texture2d1', 'texture3d1', 'texture2d', 'texture3d')


def block(text, marker):
    if text.count(marker) != 1:
        raise RuntimeError('production method marker changed: ' + marker)
    start = text.index(marker)
    cursor = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[cursor] == '{') - (text[cursor] == '}')
        cursor += 1
    return text[start:cursor]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-ref')
    parser.add_argument('--arch', choices=('arm64', 'x86_64'), default='arm64')
    args = parser.parse_args()
    production = (ROOT / RESOURCE).read_text()
    if args.baseline_ref:
        production = subprocess.check_output(['git', 'show', args.baseline_ref + ':' + RESOURCE],
                                             cwd=ROOT, text=True)
    forward = (ROOT / FORWARD).read_text()
    resource_method = block(production, 'virtual HRESULT GetDXGIUsage(')
    forward_method = block(forward, 'HRESULT\n  STDMETHODCALLTYPE\n  GetUsage(').replace(' final {', ' {')
    fixture = (HERE / 'fixture.cpp').read_text()
    fixture = fixture.replace('/* RESOURCE_USAGE */', resource_method).replace('/* DXGI_FORWARD */', forward_method)
    with tempfile.TemporaryDirectory(prefix='dxmt-dxgi-usage-') as directory:
        work = Path(directory)
        cpp, exe = work / 'fixture.cpp', work / 'fixture'
        cpp.write_text(fixture)
        command = ['clang++', '-std=c++20', '-arch', args.arch, '-O1', '-g',
                   '-Wall', '-Wextra', '-Werror', '-Wno-extern-c-compat',
                   '-fsanitize=undefined', '-fno-sanitize-recover=all',
                   '-I', str(ROOT / 'include/native/windows'),
                   '-I', str(ROOT / 'include/native/directx'), str(cpp), '-o', str(exe)]
        subprocess.run(command, check=True, timeout=60)
        for mode in MODES:
            for resource in CLASSES:
                result = subprocess.run([str(exe), mode, resource], capture_output=True, text=True, timeout=10)
                negative = args.baseline_ref and mode != 'controls'
                if negative:
                    if result.returncode != 2 or result.stdout or result.stderr != 'FAIL usage bits\n':
                        raise RuntimeError('unrelated baseline failure: ' + mode + ' ' + resource + '\n' + result.stdout + result.stderr)
                    print('PASS exact baseline rejection ' + mode + ' ' + resource)
                else:
                    if result.returncode or result.stderr or not result.stdout.startswith('PASS ' + mode + ' ' + resource + ' '):
                        raise RuntimeError('candidate failed: ' + mode + ' ' + resource + '\n' + result.stdout + result.stderr)
                    print(result.stdout, end='')
    print(json.dumps({'arch': args.arch, 'baseline_ref': args.baseline_ref,
        'source_sha256': {RESOURCE: hashlib.sha256(production.encode()).hexdigest(),
                          FORWARD: hashlib.sha256(forward.encode()).hexdigest()},
        'scope': 'actual method bodies, real project D3D/DXGI headers; resource construction/device/GPU injected'}, sort_keys=True))


if __name__ == '__main__':
    main()
