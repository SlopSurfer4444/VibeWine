#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Build the actual public SM50 API TU against existing LLVM14 support objects.

Run all ownership paths; --baseline-ref requires known leaks and null-output
crashes from the unpatched implementation. No runtime activation.
"""
import argparse
import os
import re
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
changed = ['dxbc_converter.cpp', 'dxbc_converter.hpp']
libs = ['LLVMPasses', 'LLVMTarget', 'LLVMObjCARCOpts', 'LLVMCoroutines', 'LLVMipo',
        'LLVMInstrumentation', 'LLVMVectorize', 'LLVMLinker', 'LLVMIRReader',
        'LLVMAsmParser', 'LLVMFrontendOpenMP', 'LLVMScalarOpts', 'LLVMInstCombine',
        'LLVMAggressiveInstCombine', 'LLVMTransformUtils', 'LLVMBitWriter',
        'LLVMAnalysis', 'LLVMProfileData', 'LLVMSymbolize', 'LLVMDebugInfoPDB',
        'LLVMDebugInfoMSF', 'LLVMDebugInfoDWARF', 'LLVMObject', 'LLVMTextAPI',
        'LLVMMCParser', 'LLVMMC', 'LLVMDebugInfoCodeView', 'LLVMBitReader',
        'LLVMCore', 'LLVMRemarks', 'LLVMBitstreamReader', 'LLVMBinaryFormat',
        'LLVMSupport', 'LLVMDemangle']
with tempfile.TemporaryDirectory(prefix='vibewine-sm50-lifetime-') as directory:
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
    modes = ['null-init', 'invalid', 'partial', 'init', 'compile-error', 'compile-success']
    modes += ['null-compile' + str(i) for i in range(5)]
    modes += [mode + '-omit' for mode in list(modes)]
    for mode in modes:
        result = subprocess.run([str(exe), mode], capture_output=True, text=True, timeout=30,
                                env={**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1:abort_on_error=0:exitcode=86:print_stacktrace=0'})
        if args.baseline_ref:
            # Ownership of explicit error objects already worked before the fix.
            expected_pass = not mode.endswith('-omit') and (mode.startswith('null-') or
                            mode in ['invalid', 'compile-error'])
            if expected_pass:
                valid = (result.returncode == 0 and not result.stderr and
                         result.stdout == 'PASS ' + mode + '\n')
            elif mode.endswith('-omit') and not mode.startswith(('init-', 'compile-success')):
                # A signal or unrelated sanitizer finding cannot prove this bug.
                # Match the precise store diagnostic, its source TU and its
                # matching summary location, with a dedicated UBSan exit status.
                location = re.escape(str(airconv / 'dxbc_converter.cpp')) + r':(?P<line>\d+):(?P<col>\d+)'
                diagnostic = (location + r": runtime error: store to null pointer of type 'sm50_error_t' \(aka 'void \*'\)\n"
                              + r'SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior '
                              + re.escape(str(airconv / 'dxbc_converter.cpp'))
                              + r':(?P=line):(?P=col)\s*')
                valid = (result.returncode == 86 and not result.stdout and
                         re.fullmatch(diagnostic, result.stderr) is not None)
            else:
                valid = (result.returncode == 2 and not result.stdout and
                         re.fullmatch(r'LEAK [1-9][0-9]* allocations in ' + re.escape(mode) + r'\n',
                                      result.stderr) is not None)
            if not valid:
                raise SystemExit('FAIL baseline oracle ' + mode + ': status=' +
                                 str(result.returncode) + '\n' + result.stdout + result.stderr)
            print('PASS baseline expected ' + ('existing error ownership' if expected_pass else 'failure') + ': ' + mode)
        else:
            if result.returncode != 0 or result.stderr or result.stdout != 'PASS ' + mode + '\n':
                raise SystemExit('FAIL ' + mode + ': status=' + str(result.returncode) + '\n' +
                                 result.stdout + result.stderr)
            print(result.stdout, end='')
