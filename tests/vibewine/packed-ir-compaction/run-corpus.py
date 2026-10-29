#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Fresh CPU converter TUs; compare unmodified historical CFG body and compaction.

An existing configured support build contributes flags/generated AIR headers,
never stale converter objects. Existing Wine DXBC fixtures are read-only.
"""
import argparse,concurrent.futures,hashlib,json,os
from pathlib import Path
import shlex,shutil,subprocess,tempfile
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--support-build',required=True,type=Path)
p.add_argument('--fixtures',required=True,type=Path)
p.add_argument('--growth',choices=['0','1'],default='0')
a=p.parse_args();here=Path(__file__).resolve().parent;source=here.parents[2]
support=a.support_build.resolve();fixtures=a.fixtures.resolve()
manifest=json.loads((here/'corpus-manifest.json').read_text())
assert {x.name for x in fixtures.glob('*.dxbc')}=={x['name'] for x in manifest}
for item in manifest:assert hashlib.sha256((fixtures/item['name']).read_bytes()).hexdigest()==item['sha256'],item['name']
entries=json.loads((support/'compile_commands.json').read_text())
entries=[e for e in entries if '/libairconv.a.p/' in e['output'] or '/libDXBCParserNative.a.p/' in e['output']]
assert len(entries)==21
libs=['LLVMPasses','LLVMTarget','LLVMObjCARCOpts','LLVMCoroutines','LLVMipo','LLVMInstrumentation','LLVMVectorize','LLVMLinker','LLVMIRReader','LLVMAsmParser','LLVMFrontendOpenMP','LLVMScalarOpts','LLVMInstCombine','LLVMAggressiveInstCombine','LLVMTransformUtils','LLVMBitWriter','LLVMAnalysis','LLVMProfileData','LLVMSymbolize','LLVMDebugInfoPDB','LLVMDebugInfoMSF','LLVMDebugInfoDWARF','LLVMObject','LLVMTextAPI','LLVMMCParser','LLVMMC','LLVMDebugInfoCodeView','LLVMBitReader','LLVMCore','LLVMRemarks','LLVMBitstreamReader','LLVMBinaryFormat','LLVMSupport','LLVMDemangle']
loop='  for (auto &bb : all_bb)\n    bb->instructions.compact();\n'
with tempfile.TemporaryDirectory(prefix='dxmt-ir-corpus-') as td:
 work=Path(td);air=work/'airconv';shutil.copytree(source/'src/airconv',air)
 def bounded():
  n=sum(x.stat().st_size for x in work.rglob('*') if x.is_file())
  assert n<128*1024*1024,'128MiB temporary build cap'
  return n
 cfg=air/'dxbc_converter_cfg.cpp';candidate=cfg.read_text();assert candidate.count(loop)==1
 # Baseline is the actual pre-change production CFG body with the same class
 # definition as every fresh object, avoiding mixed header definitions/ODR.
 baseline=subprocess.check_output(['git','-C',str(source),'show','a6637cfe:src/airconv/dxbc_converter_cfg.cpp'],text=True)
 def compile_entry(item):
  i,e=item;old=shlex.split(e['command']);cmd=[];j=0
  while j<len(old):
   x=old[j]
   if x=='-MD':j+=1;continue
   if x in ['-MQ','-MF']:j+=2;continue
   if x=='-o':cmd+=['-o',str(work/f'{i}.o')];j+=2;continue
   if x.startswith('-DDXMT_PACKED_IR_GROWTH='):j+=1;continue
   if x=='-c':
    path=(Path(e['directory'])/old[j+1]).resolve()
    assert path.is_relative_to(source), 'support build belongs to another source checkout'
    if path.is_relative_to(source/'src/airconv'):path=air/path.relative_to(source/'src/airconv')
    cmd+=['-c',str(path)];j+=2;continue
   if x.startswith('-I'):
    path=(Path(e['directory'])/x[2:]).resolve()
    if path.is_relative_to(source/'src/airconv'):path=air/path.relative_to(source/'src/airconv')
    x='-I'+str(path)
   cmd.append(x);j+=1
  cmd+=['-DDXMT_PACKED_IR_GROWTH='+a.growth]
  r=subprocess.run(cmd,cwd=e['directory'],capture_output=True,text=True,timeout=180)
  assert r.returncode==0,r.stderr
  bounded()
  return work/f'{i}.o'
 # All TUs share the exact current PackedVariantList definition.
 with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:objects=list(pool.map(compile_entry,enumerate(entries)))
 cfgindex=next(i for i,e in enumerate(entries) if e['file'].endswith('dxbc_converter_cfg.cpp'))
 sample=shlex.split(entries[cfgindex]['command']);llvm=next(Path(x[2:]).parent for x in sample if x.startswith('-I') and 'llvm' in x and x.endswith('/include'))
 common=['clang++','-std=c++20','-arch','x86_64','-O3','-DNDEBUG','-fno-rtti','-fno-exceptions','-fblocks','-Wno-extern-c-compat','-DDXMT_PACKED_IR_GROWTH='+a.growth]
 for path in [air,source/'include',source/'libs',source/'include/native/windows',source/'include/native/directx',llvm/'include']:common+=['-I',str(path)]
 subprocess.run([*common,'-c',str(here/'corpus.cpp'),'-o',str(work/'fixture.o')],check=True,timeout=180)
 results={};digests={}
 for label,text in [('baseline',baseline),('candidate',candidate)]:
  cfg.write_text(text);compile_entry((cfgindex,entries[cfgindex]))
  binary=work/label
  subprocess.run([*common,str(work/'fixture.o'),*map(str,objects),'-L'+str(llvm/'lib'),'-lm','-lz','-lcurses','-lxml2',*['-l'+x for x in libs],'-o',str(binary)],check=True,timeout=180)
  bounded()
  dest=work/(label+'-outputs');r=subprocess.run([str(binary),str(fixtures),str(dest)],capture_output=True,text=True,timeout=90)
  assert r.returncode==0,r.stderr
  results[label]=[json.loads(x) for x in r.stdout.splitlines()]
  digests[label]={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in dest.iterdir()}
  # Repeated late output generation uses retained IR without mutation.
  for path in dest.glob('*.repeat'):
   original=path.with_suffix('.bitcode') if path.name.endswith('.dxbc.repeat') else path.with_suffix('')
   assert path.read_bytes()==original.read_bytes(),path
  binary.unlink()
 # Reuse the production CALL/CALLC lifetime/CFG fixture, adding a NOP to
 # each function body so cloned instruction backing is eligible for compaction.
 call_source=(source/'tests/vibewine/call-return-lifetime/fixture.cpp').read_text()
 call_source=call_source.replace('    label(code, 7);','    label(code, 7); code.push_back(op(D3D10_SB_OPCODE_NOP));').replace('label(code, 9);','label(code, 9); code.push_back(op(D3D10_SB_OPCODE_NOP));')
 (work/'calls.cpp').write_text(call_source)
 call_common=[x for x in common if x!='-fno-exceptions']
 subprocess.run([*call_common,str(work/'calls.cpp'),*map(str,objects),'-L'+str(llvm/'lib'),'-lm','-lz','-lcurses','-lxml2',*['-l'+x for x in libs],'-o',str(work/'calls')],check=True,timeout=180)
 bounded()
 call_modes=['no-call','direct','nested','repeated','conditional-nz-0','conditional-nz-1','conditional-z-0','conditional-z-1','mixed']
 for mode in call_modes:
  r=subprocess.run([str(work/'calls'),mode],capture_output=True,text=True,timeout=20)
  assert r.returncode==0 and r.stdout=='PASS '+mode+' CFG path and LLVM lowering\n',r.stdout+r.stderr
 assert digests['baseline']==digests['candidate'],'output/reflection/arguments changed'
 before=sum(x['backing'] for x in results['baseline']);after=sum(x['backing'] for x in results['candidate'])
 assert after<before,'actual allocator backing did not decrease'
 for b,c in zip(results['baseline'],results['candidate']):
  assert b['file']==c['file'] and b['used']==c['used'] and c['backing']<=b['backing']
  if c['file'] in ['7208-simple_vs.dxbc','7344-simple_ps.dxbc']:
   assert c['backing']<=512 and b['backing']>512,'candidate must pass and old production fail the same fixed retained-backing oracle'
 bounded()
 print(json.dumps({'growth':int(a.growth),'baseline':'a6637cfe','llvm':str(llvm),'fresh_translation_units':len(entries),'backing_before':before,'backing_after':after,'saved':before-after,'call_cfg_llvm_cases':len(call_modes),'outputs_identical':len(digests['baseline']),'results':results,'output_sha256':digests['candidate'],'source_sha256':{p:hashlib.sha256((source/p).read_bytes()).hexdigest() for p in ['src/airconv/packed_variant_list.hpp','src/airconv/dxbc_converter_cfg.cpp']}},indent=2))
