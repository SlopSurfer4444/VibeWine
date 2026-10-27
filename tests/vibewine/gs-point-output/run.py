#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Build the production GS converter against readonly LLVM14 support libraries.

The GS TU is rebuilt in temporary scratch; nothing is installed or activated.
--baseline-ref requires all three point regressions, while line/triangle controls pass.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--llvm', required=True, type=Path)
parser.add_argument('--support-build', required=True, type=Path)
parser.add_argument('--baseline-ref')
args = parser.parse_args()
source = Path(__file__).resolve().parents[3]
llvm, support = args.llvm.resolve(), args.support_build.resolve()
changed = ['dxbc_converter_gs.cpp']
libs = ['LLVMPasses', 'LLVMTarget', 'LLVMObjCARCOpts', 'LLVMCoroutines', 'LLVMipo',
        'LLVMInstrumentation', 'LLVMVectorize', 'LLVMLinker', 'LLVMIRReader',
        'LLVMAsmParser', 'LLVMFrontendOpenMP', 'LLVMScalarOpts', 'LLVMInstCombine',
        'LLVMAggressiveInstCombine', 'LLVMTransformUtils', 'LLVMBitWriter',
        'LLVMAnalysis', 'LLVMProfileData', 'LLVMSymbolize', 'LLVMDebugInfoPDB',
        'LLVMDebugInfoMSF', 'LLVMDebugInfoDWARF', 'LLVMObject', 'LLVMTextAPI',
        'LLVMMCParser', 'LLVMMC', 'LLVMDebugInfoCodeView', 'LLVMBitReader',
        'LLVMCore', 'LLVMRemarks', 'LLVMBitstreamReader', 'LLVMBinaryFormat',
        'LLVMSupport', 'LLVMDemangle']
with tempfile.TemporaryDirectory(prefix='vibewine-gs-point-') as directory:
    work = Path(directory)
    airconv = source / 'src/airconv'
    if args.baseline_ref:
        airconv = work / 'airconv'
        shutil.copytree(source / 'src/airconv', airconv)
        for name in changed:
            (airconv / name).write_bytes(subprocess.check_output(
                ['git', 'show', args.baseline_ref + ':src/airconv/' + name], cwd=source))
    exe = work / 'fixture'
    command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-arch', 'x86_64',
               '-O1', '-g', '-DNDEBUG', '-fno-rtti', '-fno-exceptions', '-fblocks',
               '-fsanitize=undefined', '-fno-sanitize-recover=undefined',
               '-Wno-return-type', '-Wno-extern-c-compat']
    for include in [airconv, source / 'include', source / 'libs',
                    source / 'include/native/windows', source / 'include/native/directx', llvm / 'include']:
        command += ['-I', str(include)]
    command += [str(Path(__file__).with_name('fixture.cpp')),
                str(airconv / changed[0]),
                str(support / 'src/airconv/darwin/libairconv.a'),
                str(support / 'libs/DXBCParser/libDXBCParserNative.a'),
                '-L' + str(llvm / 'lib'), '-lm', '-lz', '-lcurses', '-lxml2']
    command += ['-l' + lib for lib in libs] + ['-o', str(exe)]
    subprocess.run(command, check=True, timeout=180)
    for mode in ['metadata', 'size', 'count', 'controls']:
        result = subprocess.run([str(exe), mode], capture_output=True, text=True, timeout=15,
                                env={**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1'})
        if args.baseline_ref and mode != 'controls':
            if result.returncode != 2 or result.stdout or result.stderr != 'FAIL point ' + mode + '\n':
                raise SystemExit('FAIL baseline oracle ' + mode + ':\n' + result.stdout + result.stderr)
            print('PASS baseline exposes missing point ' + mode)
        else:
            if result.returncode or result.stderr or result.stdout != 'PASS ' + mode + '\n':
                raise SystemExit('FAIL ' + mode + ':\n' + result.stdout + result.stderr)
            print(result.stdout, end='')
