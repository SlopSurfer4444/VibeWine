#!/usr/bin/env python3
"""Compile the production optimization pipeline and link a retained converter.

Build baseline, candidate and optional production-body mutation controls against
an existing LLVM15 support build. No renderer/runtime activation. The retained
archive supplies unchanged parser/converter/builder objects and generated AIR
headers; airconv_context.cpp is always rebuilt from the current source.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--llvm', required=True, type=Path)
parser.add_argument('--support-build', required=True, type=Path)
parser.add_argument('--mutations', action='store_true')
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
with tempfile.TemporaryDirectory(prefix='vibewine-shader-cleanup-') as directory:
    work = Path(directory)
    airconv = source / 'src/airconv'
    common = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-arch', 'x86_64',
              '-O1', '-g', '-DNDEBUG', '-fno-rtti', '-fno-exceptions', '-fblocks',
              '-Wno-return-type', '-Wno-extern-c-compat']
    for include in [airconv, source / 'include', source / 'libs',
                    source / 'include/native/windows', source / 'include/native/directx', llvm / 'include',
                    support / 'src/airconv/darwin/libairconv.a.p']:
        common += ['-I', str(include)]
    fixture_obj = work / 'fixture.o'
    subprocess.run(common + ['-c', str(Path(__file__).with_name('fixture.cpp')), '-o', str(fixture_obj)], check=True, timeout=180)
    production = (airconv / 'airconv_context.cpp').read_text()
    cases = [('off', 0, production, None), ('on', 1, production, None)]
    if args.mutations:
        cases += [('mutant-no-cse', 1, production.replace('GlobalCleanupPM.addPass(LateHalfwordCSEPass());', ''), 'extracts =='),
                  ('mutant-no-barrier', 1, production.replace('GlobalCleanupPM.addPass(air::SimdgroupImplicitMemBarrierPass());', ''), 'barrierBeforeLoad')]
    outputs = {}
    for label, enabled, content, expected_failure in cases:
        context = work / (label + '.cpp'); context.write_text(content + '\n' + Path(__file__).with_name('contract.inc').read_text())
        obj = work / (label + '.o'); exe = work / label
        subprocess.run(common + ['-DDXMT_SHADER_CLEANUP=' + str(enabled), '-c', str(context), '-o', str(obj)], check=True, timeout=180)
        command = common + [str(fixture_obj), str(obj), str(support / 'src/airconv/darwin/libairconv.a'),
                           str(support / 'libs/DXBCParser/libDXBCParserNative.a'),
                           '-L' + str(llvm / 'lib'), '-lm', '-lz', '-lcurses', '-lxml2']
        command += ['-l' + lib for lib in libs] + ['-o', str(exe)]
        subprocess.run(command, check=True, timeout=180)
        result = subprocess.run([str(exe), 'off' if label == 'off' else 'on', str(work / (label + '.ll'))],
                                capture_output=True, text=True, timeout=180)
        if expected_failure:
            if result.returncode == 0 or expected_failure not in result.stderr:
                raise SystemExit('FAIL mutation not rejected at expected check: ' + label + '\n' + result.stderr)
            print('PASS rejected ' + label + ' at ' + expected_failure)
        else:
            if result.returncode:
                raise SystemExit(result.stdout + result.stderr)
            print(result.stdout, end='')
            outputs[label] = (work / (label + '.ll')).read_text()
    # Material sideeffect and precision functions must be byte-identical in
    # disassembled baseline/candidate IR, including call attrs and operands.
    import re
    for name in ['effects', 'precise']:
        pattern = r'define [^\n]*@' + name + r'\(.*?^}'
        before = re.search(pattern, outputs['off'], re.M | re.S)
        after = re.search(pattern, outputs['on'], re.M | re.S)
        if not before or not after or before.group() != after.group():
            raise SystemExit('FAIL safety IR changed: ' + name)
    strip_halfwords = r'define [^\n]*@halfwords[0-3]\(.*?^}\n'
    if re.sub(strip_halfwords, '', outputs['off'], flags=re.M | re.S) != re.sub(strip_halfwords, '', outputs['on'], flags=re.M | re.S):
        raise SystemExit('FAIL unrelated module IR/declarations/attributes changed')
    print('PASS baseline/candidate remaining module IR identical; 4 duplicate integer extracts removed')
