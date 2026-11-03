#!/usr/bin/env python3
"""Production gate/pass and full pipeline, exact retained AIR, CPU only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import selectors
import shutil
import subprocess
import tempfile
import time

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
CAP=128*1024**2
HEAVY_SHA='36548ba5a5b2dc091165ad6eb3b1de927cdc8b4ed3a1c579a6a3e70bfda6cb6d'
LIBS=['LLVMPasses','LLVMTarget','LLVMObjCARCOpts','LLVMCoroutines','LLVMipo',
      'LLVMInstrumentation','LLVMVectorize','LLVMLinker','LLVMIRReader','LLVMAsmParser',
      'LLVMFrontendOpenMP','LLVMScalarOpts','LLVMInstCombine','LLVMAggressiveInstCombine',
      'LLVMTransformUtils','LLVMBitWriter','LLVMAnalysis','LLVMProfileData','LLVMSymbolize',
      'LLVMDebugInfoPDB','LLVMDebugInfoMSF','LLVMDebugInfoDWARF','LLVMObject','LLVMTextAPI',
      'LLVMMCParser','LLVMMC','LLVMDebugInfoCodeView','LLVMBitReader','LLVMCore','LLVMRemarks',
      'LLVMBitstreamReader','LLVMBinaryFormat','LLVMSupport','LLVMDemangle']
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def size(root):return sum(p.stat().st_size for p in root.rglob('*') if p.is_file() and not p.is_symlink())

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--llvm',required=True,type=Path)
    p.add_argument('--support-build',required=True,type=Path)
    p.add_argument('--output-dir',required=True,type=Path,help='fresh task-local evidence directory; 128 MiB/30 days')
    p.add_argument('--sanitize',action='store_true')
    p.add_argument('--mutations',action='store_true')
    args=p.parse_args(); llvm=args.llvm.resolve(); support=args.support_build.resolve()
    output=args.output_dir.absolute()
    assert not output.exists() and not output.is_symlink(),'fresh output required'
    assert shutil.disk_usage(output.parent).free>1024**3
    output.mkdir(mode=0o700)
    assert sha(HERE/'heavy.ll')==HEAVY_SHA,'actual retained heavy AIR drift'
    assert re.search(r"option\('vibewine_constant_loop_motion', type : 'boolean', value : false",(ROOT/'meson.options').read_text())
    assert "'vibewine_constant_loop_motion' : 'DXMT_CONSTANT_LOOP_MOTION'" in (ROOT/'meson.build').read_text()
    assert "'transforms/constant_loop_motion.cpp'" in (ROOT/'src/airconv/meson.build').read_text()
    report={'scope':'production gate/pass/full pipeline on actual AIR; no GPU/Wine/worker build',
            'retention_days':30,'byte_cap':CAP,'commands':[],'results':{}}
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1')
    def run(command,name,timeout=60,expected=None):
        start=time.monotonic()
        process=subprocess.Popen(list(map(str,command)),stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                                 cwd=ROOT,env=env,start_new_session=True)
        selector=selectors.DefaultSelector();selector.register(process.stdout,selectors.EVENT_READ);used=0
        try:
            with (output/(name+'.log')).open('xb') as log:
                while selector.get_map():
                    if time.monotonic()-start>timeout:raise RuntimeError(name+' timeout')
                    if size(output)>CAP:raise RuntimeError('output cap')
                    for key,_ in selector.select(.1):
                        data=os.read(key.fd,16384)
                        if not data:selector.unregister(key.fileobj);continue
                        used+=len(data)
                        if used>256*1024:raise RuntimeError('log cap')
                        log.write(data)
            code=process.wait(timeout=2)
        finally:
            selector.close();process.stdout.close()
            if process.poll() is None:
                # Exact task-owned CPU compiler/fixture process only. Never a
                # Wine process, server, worker, service or installed runtime.
                process.terminate()
                try:process.wait(timeout=3)
                except subprocess.TimeoutExpired:process.kill();process.wait(timeout=3)
        text=(output/(name+'.log')).read_text()
        report['commands'].append({'name':name,'argv':list(map(str,command)),'exit':code,
                                   'seconds':time.monotonic()-start,'log_bytes':used})
        if expected:assert code==3 and text.strip()=='FAIL '+expected,(name,code,text)
        else:assert code==0,(name,code,text)
        return text.strip()
    common=['clang++','-std=c++20','-arch','x86_64','-O1','-DNDEBUG','-fno-rtti','-fno-exceptions',
            '-fblocks','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-return-type',
            '-Wno-extern-c-compat','-Wno-deprecated-declarations',
            '-Wno-unnecessary-virtual-specifier',
            '-I'+str(ROOT/'src/airconv'),'-I'+str(ROOT/'include'),'-I'+str(ROOT/'libs'),
            '-I'+str(ROOT/'include/native/windows'),'-I'+str(ROOT/'include/native/directx'),
            '-I'+str(llvm/'include'),'-I'+str(support/'src/airconv/darwin/libairconv.a.p')]
    if args.sanitize:common+=['-fsanitize=address,undefined','-fno-sanitize-recover=all']
    source=ROOT/'src/airconv/transforms/constant_loop_motion.cpp'
    context=ROOT/'src/airconv/airconv_context.cpp'
    variants=[('default',[],False,None),('off',['-DDXMT_CONSTANT_LOOP_MOTION=0'],False,None),
              ('on',['-DDXMT_CONSTANT_LOOP_MOTION=1'],True,None)]
    if args.mutations:variants += [('bypass-admission',['-DDXMT_CONSTANT_LOOP_MOTION=1'],True,'rejected-as1 changed IR'),
                                 ('omit-licm',['-DDXMT_CONSTANT_LOOP_MOTION=1'],True,'heavy direct hoist'),
                                 ('scalable-type',['-DDXMT_CONSTANT_LOOP_MOTION=1'],True,'rejected-scalable-vector changed IR')]
    with tempfile.TemporaryDirectory(prefix='compile-',dir=output) as temp:
        temp=Path(temp);fixture=temp/'fixture.o';production=temp/'pass.o'
        run(common+['-c',HERE/'fixture.cpp','-o',fixture],'compile-fixture')
        for name,flags,on,expected in variants:
            # One binary at a time keeps peak output below the explicit cap.
            selected=source
            if name in ('bypass-admission','omit-licm','scalable-type'):
                text=source.read_text()
                if name=='bypass-admission':text=text.replace('if (!isAdmittedFragment(F))','if (false && !isAdmittedFragment(F))')
                elif name=='omit-licm':text=text.replace('PM.addPass(createFunctionToLoopPassAdaptor(LICMPass(LICMOptions()), true /* UseMemorySSA */));','')
                else:
                    start=text.index('  unsigned Width = 1;')
                    end=text.index('  const std::string Suffix',start)
                    text=text[:start]+'''  unsigned Width = 1;
  if (const auto *Vector = dyn_cast<FixedVectorType>(ValueType)) {
    Width = Vector->getNumElements();
    if (Width < 2 || Width > 4)
      return false;
  }
  if (!ValueType->getScalarType()->isFloatTy())
    return false;
'''+text[end:]
                selected=temp/(name+'.cpp');selected.write_text(text)
            run(common+['-I'+str(source.parent),'-c',selected,'-o',production],'compile-pass-'+name)
            obj=temp/'context.o'
            run(common+['-DDXMT_SHADER_CLEANUP=1',*flags,'-c',context,'-o',obj],'compile-context-'+name)
            binary=temp/'fixture'
            link=common+[fixture,production,obj,support/'src/airconv/darwin/libairconv.a',
                         support/'libs/DXBCParser/libDXBCParserNative.a','-L'+str(llvm/'lib')]
            link+=['-l'+lib for lib in LIBS]+['-lm','-lz','-lcurses','-lxml2','-Wl,-dead_strip','-o',binary]
            run(link,'link-'+name)
            report['results'][name]={'binary_sha256':sha(binary),
                'output':run([binary,HERE/'heavy.ll','on' if on else 'off',output/name],'run-'+name,30,expected),
                'expected_failure':expected}
        assert (output/'default.full.ll').read_bytes()==(output/'off.full.ll').read_bytes()
    inputs=[source,source.with_suffix('.hpp'),context,HERE/'fixture.cpp',HERE/'heavy.ll',HERE/'run.py',
            ROOT/'meson.build',ROOT/'meson.options',ROOT/'src/airconv/meson.build']
    report['source_sha256']={str(x.relative_to(ROOT)):sha(x) for x in inputs}
    report['artifacts']={str(x.relative_to(output)):{'bytes':x.stat().st_size,'sha256':sha(x)} for x in output.rglob('*') if x.is_file()}
    report['retained_bytes']=size(output);report['terminal']='passed'
    (output/'receipt.json').write_text(json.dumps(report,indent=2)+'\n')
    assert size(output)<=CAP
    print(json.dumps({'terminal':report['terminal'],'results':report['results'],
                      'retained_bytes':size(output),'receipt':str(output/'receipt.json')},indent=2))

if __name__=='__main__':main()
