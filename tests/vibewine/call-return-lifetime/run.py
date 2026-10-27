#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Compile the production CFG TU and exercise actual DXBC parsing and LLVM lowering."""
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
libs = ['LLVMPasses', 'LLVMTarget', 'LLVMObjCARCOpts', 'LLVMCoroutines', 'LLVMipo',
        'LLVMInstrumentation', 'LLVMVectorize', 'LLVMLinker', 'LLVMIRReader',
        'LLVMAsmParser', 'LLVMFrontendOpenMP', 'LLVMScalarOpts', 'LLVMInstCombine',
        'LLVMAggressiveInstCombine', 'LLVMTransformUtils', 'LLVMBitWriter',
        'LLVMAnalysis', 'LLVMProfileData', 'LLVMSymbolize', 'LLVMDebugInfoPDB',
        'LLVMDebugInfoMSF', 'LLVMDebugInfoDWARF', 'LLVMObject', 'LLVMTextAPI',
        'LLVMMCParser', 'LLVMMC', 'LLVMDebugInfoCodeView', 'LLVMBitReader',
        'LLVMCore', 'LLVMRemarks', 'LLVMBitstreamReader', 'LLVMBinaryFormat',
        'LLVMSupport', 'LLVMDemangle']
modes = ['no-call', 'direct', 'nested', 'repeated', 'conditional-nz-0',
         'conditional-nz-1', 'conditional-z-0', 'conditional-z-1', 'mixed']
with tempfile.TemporaryDirectory(prefix='vibewine-call-return-') as directory:
    work = Path(directory)
    for sentinel in [False, True]:
        airconv = work / ('lifetime' if sentinel else 'normal')
        shutil.copytree(source / 'src/airconv', airconv)
        cfg = airconv / 'dxbc_converter_cfg.cpp'
        if args.baseline_ref:
            cfg.write_bytes(subprocess.check_output(
                ['git', 'show', args.baseline_ref + ':src/airconv/dxbc_converter_cfg.cpp'], cwd=source))
        if sentinel:
            # Fault injection into a live object keeps all type definitions
            # identical to the retained support library. Snapshot must already
            # be independent of this continuation before variant replacement.
            needle = '      bb->target = BasicBlockUnconditionalBranch{clone(func_entries.at(call->func_id))};'
            text = cfg.read_text()
            if text.count(needle) != 1:
                raise SystemExit('FAIL continuation injection no longer binds call replacement')
            cfg.write_text(text.replace(needle, '      call->return_point = nullptr;\n' + needle))
        exe = work / ('injected-fixture' if sentinel else 'fixture')
        command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-arch', 'x86_64',
                   '-O1', '-g', '-DNDEBUG', '-fno-rtti', '-fblocks',
                   '-fsanitize=undefined', '-fno-sanitize-recover=undefined',
                   '-Wno-return-type', '-Wno-extern-c-compat']
        for include in [airconv, source / 'include', source / 'libs',
                        source / 'include/native/windows', source / 'include/native/directx', llvm / 'include']:
            command += ['-I', str(include)]
        command += [str(Path(__file__).with_name('fixture.cpp')), str(cfg),
                    str(support / 'src/airconv/darwin/libairconv.a'),
                    str(support / 'libs/DXBCParser/libDXBCParserNative.a'),
                    '-L' + str(llvm / 'lib'), '-lm', '-lz', '-lcurses', '-lxml2']
        command += ['-l' + lib for lib in libs] + ['-o', str(exe)]
        subprocess.run(command, check=True, timeout=180)
        for mode in modes:
            result = subprocess.run([str(exe), mode], capture_output=True, text=True, timeout=15,
                                    env={**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1'})
            conditional = mode.startswith('conditional-') or mode == 'mixed'
            lifetime_failure = args.baseline_ref and sentinel and mode in ['direct', 'nested', 'repeated']
            if args.baseline_ref and conditional:
                valid = result.returncode == 42 and result.stderr == 'FAIL conditional-call targets wrong variant\n' and not result.stdout
            elif lifetime_failure:
                valid = result.returncode == 43 and result.stderr == 'FAIL return continuation is outside owned CFG\n' and not result.stdout
            else:
                valid = result.returncode == 0 and not result.stderr and result.stdout == 'PASS ' + mode + ' CFG path and LLVM lowering\n'
            if not valid:
                raise SystemExit(f'FAIL {mode} injected={sentinel} status={result.returncode}:\n' + result.stdout + result.stderr)
            label = 'baseline expected failure' if args.baseline_ref and (conditional or lifetime_failure) else 'production behavior'
            print(f'PASS {label}: {mode} injected={sentinel}')
