#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compile the lifecycle probe, inspect imports; never execute PE or Wine."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolchain', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    root = here.parents[2]
    production = root / 'src/winemetal/winemetal_thunks.c'
    compiler = args.toolchain.resolve() / 'x86_64-w64-mingw32-clang'
    objdump = args.toolchain.resolve() / 'llvm-objdump'
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report = {'scope': 'compile and import inspection only, not executed', 'files': {},
              'seam_sha256': hashlib.sha256((here / 'wineunixlib.h').read_bytes()).hexdigest(),
              'tools': {str(path): {'resolved': str(path.resolve(strict=True)),
                                    'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
                        for path in [compiler, objdump, compiler.parent / 'clang', compiler.parent / 'ld.lld']}}
    for name, sources, flags in [
        ('lifecycle.dll', [production, here / 'lifecycle_dll.c'], ['-shared', '-lntdll']),
        ('lifecycle.exe', [here / 'lifecycle_guest.c'], []),
    ]:
        command = [str(compiler), '-std=c2x', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                   '-I', str(here), '-I', str(root / 'src/winemetal'),
                   *map(str, sources), *flags, '-o', str(output / name)]
        result = subprocess.run(command, capture_output=True, timeout=30)
        (output / (name + '.build.txt')).write_bytes(result.stdout + result.stderr)
        if result.returncode or len(result.stdout + result.stderr) > 65536:
            raise RuntimeError(result.stderr.decode(errors='replace'))
        inspected = subprocess.run([str(objdump), '-p', str(output / name)],
                                   capture_output=True, timeout=15, check=True)
        if inspected.stderr or len(inspected.stdout) > 131072:
            raise RuntimeError('unexpected PE inspection output')
        (output / (name + '.imports.txt')).write_bytes(inspected.stdout)
        if name.endswith('.dll') and b'RtlDllShutdownInProgress' not in inspected.stdout:
            raise RuntimeError('direct NTDLL phase import missing')
        report['files'][name] = {
            'sha256': hashlib.sha256((output / name).read_bytes()).hexdigest(),
            'bytes': (output / name).stat().st_size,
            'imports': re.findall(r'DLL Name:\s*(\S+)', inspected.stdout.decode()),
            'sources': {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in sources},
            'command': command}
    (output / 'build-receipt.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
