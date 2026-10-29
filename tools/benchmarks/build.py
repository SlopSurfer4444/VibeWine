#!/usr/bin/env python3
"""Build only the finite D3D11 PE guest with an existing LLVM-MinGW compiler.
No installation, download, Wine, native runtime, or GPU execution occurs here.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
HUB = HERE.parents[1]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler', type=Path, required=True, help='existing x86_64-w64-mingw32-clang++')
    p.add_argument('--output', type=Path, required=True, help='fresh private build directory')
    a = p.parse_args()
    compiler = a.compiler.expanduser().absolute()
    if not compiler.is_file():
        p.error("compiler not found")
    if compiler.name != 'x86_64-w64-mingw32-clang++':
        p.error('expected the x86_64 LLVM-MinGW compiler entry point')
    out = a.output.expanduser().resolve()
    out.mkdir(parents=True, exist_ok=True)
    exe = out / 'dx11-benchmark.exe'
    receipt = out / 'build-receipt.json'
    if exe.exists() or receipt.exists():
        p.error('output already contains this build; use a fresh directory')
    source = HERE / 'guest/main.cpp'
    cmd = [str(compiler), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
           '-static', '-static-libgcc', '-static-libstdc++', '-Wl,--no-insert-timestamp',
           str(source), '-o', str(exe), '-ld3d11', '-ldxgi', '-luser32', '-lgdi32', '-lkernel32']
    version = subprocess.run([str(compiler), '--version'], capture_output=True, text=True, check=True, timeout=10).stdout
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    (out / 'build.log').write_text(result.stdout + result.stderr)
    if result.returncode:
        print(result.stdout + result.stderr, file=sys.stderr)
        return result.returncode
    objdump = compiler.parent / 'llvm-objdump'
    if not objdump.is_file():
        raise SystemExit('matching llvm-objdump unavailable; imports remain unverified')
    imports = subprocess.run([str(objdump), '-p', str(exe)], capture_output=True, text=True, check=True, timeout=20).stdout
    (out / 'pe-imports.txt').write_text(imports)
    dlls = [line.split('DLL Name:', 1)[1].strip() for line in imports.splitlines() if 'DLL Name:' in line]
    forbidden = [n for n in dlls if any(x in n.lower() for x in ('vulkan', 'dxvk', 'opengl'))]
    if forbidden:
        raise SystemExit('unexpected graphics imports: ' + repr(forbidden))
    revision = subprocess.run(['git', '-C', str(HUB), 'rev-parse', 'HEAD'], capture_output=True, text=True, check=True, timeout=10).stdout.strip()
    dxmt = HUB / 'components/dxmt'
    foundation = ['tests/dx11/dx11_cube.cpp', 'tests/dx11/shader_cube.hlsl', 'LICENSE', 'COPYING.LIB']
    inputs = {str(source.relative_to(HUB)): sha(source),
              str((HERE / 'guest/PROVENANCE.md').relative_to(HUB)): sha(HERE / 'guest/PROVENANCE.md'),
              str(Path(__file__).resolve().relative_to(HUB)): sha(Path(__file__).resolve())}
    for item in foundation:
        f = dxmt / item
        if f.is_file():
            inputs[str(f.relative_to(HUB))] = sha(f)
    data = dict(schema=1, status='built-imports-checked-not-launched', hubRevision=revision,
                compiler=str(compiler), compilerSha256=sha(compiler), compilerVersion=version,
                command=cmd, sourceSha256=inputs, executable=str(exe), executableSha256=sha(exe),
                sizeBytes=exe.stat().st_size, importedDlls=dlls,
                limitations=['Compile/import checks do not establish runtime compatibility, presentation, or FPS.',
                              'Probe EVENT plus verified staging readback is outside measurement. In inspected DXMT, staging READ waits CPU coherence after trace23/24 publication; exact runtime binding and host strict admission remain required. Synchronous Map is bounded by the 15-second guest guard and host deadline, not the 5-second EVENT poll deadline.'])
    receipt.write_text(json.dumps(data, indent=2) + '\n')
    print(json.dumps(data, indent=2))
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
