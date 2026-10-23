#!/usr/bin/env python3
"""Compile actual parser/converter TUs against an existing LLVM14 support build.

No renderer/runtime activation. The support build supplies unchanged utility
objects and LLVM libraries, not either production TU under test. --baseline-ref
exports the four pre-port files and requires the same runnable fixture to fail.
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
changed = ['dxbc_instructions.cpp', 'dxbc_instructions.hpp',
           'nt/dxbc_converter_base.cpp', 'nt/dxbc_converter_base.hpp']
libs = ['LLVMPasses', 'LLVMTarget', 'LLVMObjCARCOpts', 'LLVMCoroutines', 'LLVMipo',
        'LLVMInstrumentation', 'LLVMVectorize', 'LLVMLinker', 'LLVMIRReader',
        'LLVMAsmParser', 'LLVMFrontendOpenMP', 'LLVMScalarOpts', 'LLVMInstCombine',
        'LLVMAggressiveInstCombine', 'LLVMTransformUtils', 'LLVMBitWriter',
        'LLVMAnalysis', 'LLVMProfileData', 'LLVMSymbolize', 'LLVMDebugInfoPDB',
        'LLVMDebugInfoMSF', 'LLVMDebugInfoDWARF', 'LLVMObject', 'LLVMTextAPI',
        'LLVMMCParser', 'LLVMMC', 'LLVMDebugInfoCodeView', 'LLVMBitReader',
        'LLVMCore', 'LLVMRemarks', 'LLVMBitstreamReader', 'LLVMBinaryFormat',
        'LLVMSupport', 'LLVMDemangle']
with tempfile.TemporaryDirectory(prefix='vibewine-control-point-') as directory:
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
                str(airconv / changed[0]), str(airconv / changed[2]),
                str(support / 'src/airconv/darwin/libairconv.a'),
                str(support / 'libs/DXBCParser/libDXBCParserNative.a'),
                '-L' + str(llvm / 'lib'), '-lm', '-lz', '-lcurses', '-lxml2']
    command += ['-l' + lib for lib in libs] + ['-o', str(exe)]
    subprocess.run(command, check=True, timeout=180)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10,
                            env={**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1'})
    if args.baseline_ref:
        if result.returncode == 0:
            raise SystemExit('FAIL baseline unexpectedly passed')
        if 'execution reached the end of a value-returning function' not in result.stderr:
            raise SystemExit('FAIL baseline failed for an unexpected reason:\n' + result.stderr)
        print('PASS baseline rejected valid hull relative index via missing-return UB')
    else:
        print(result.stdout, end='')
        if result.returncode:
            raise SystemExit(result.stderr)
