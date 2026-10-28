#!/usr/bin/env python3
"""Actual CreateShader and full C AIRCONV thunk TU at a mocked UNIX_CALL edge."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
HERE=Path(__file__).resolve().parent
REPO=HERE.parents[2]
BASELINE='a21579fb2a03aef5fbe93d7bcba1ea882e63a192'
CALLER='src/d3d11/d3d11_pipeline_cache.cpp'
PATHS=[CALLER,'src/d3d11/d3d11_shader_singleflight.hpp','src/winemetal/airconv_thunks.c','src/winemetal/airconv_thunks.h',
       'src/airconv/airconv_public.h','src/util/sha1/sha1.c','src/util/sha1/sha1_util.cpp','src/util/sha1/sha1_util.hpp']


def block(text,marker):
 start=text.index(marker);opening=text.index('{',start);depth=1;end=opening+1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]


def generated(caller):
 replacements={
  'CREATE_SHADER':block(caller,'  CachedSM50Shader *CreateShader('),
  'CONSTRUCTOR':block(caller,'    CachedSM50Shader(PipelineCache *cache,'),
  'DESTRUCTOR':block(caller,'    ~CachedSM50Shader()'),
  'INITIALIZED':block(caller,'    bool initialized() const') if 'bool initialized() const' in caller else '',
  'STORAGE':('  FixtureShaderStorage<Sha1Digest, CachedSM50Shader> shaders_;' if '.GetOrCreate(sha1' in caller else
             '  std::unordered_map<Sha1Digest,std::unique_ptr<CachedSM50Shader>> shaders_;\n  std::shared_mutex mutex_shares;'),
 }
 fixture=(HERE/'fixture.cpp.in').read_text()
 for marker,code in replacements.items():
  assert fixture.count('// '+marker+'\n')==1
  fixture=fixture.replace('// '+marker+'\n',code+'\n')
 return fixture


def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--source-dir',type=Path,default=REPO)
 parser.add_argument('--arch',choices=['arm64','x86_64'])
 args=parser.parse_args();source=args.source_dir.resolve()
 snapshots={p:(source/p).read_bytes() for p in PATHS}
 fixture_snapshot=(HERE/'fixture.cpp.in').read_bytes()
 runner_snapshot=Path(__file__).read_bytes()
 current=snapshots[CALLER].decode()
 old=subprocess.check_output(['git','-C',str(source),'show',f'{BASELINE}:{CALLER}'],text=True)
 init_only=old.replace('    sm50_error_t err;','    sm50_error_t err = nullptr;')
 if '~ErrorOwner() { if (value) SM50FreeError(value); }' in current:
  missing_free=current.replace('~ErrorOwner() { if (value) SM50FreeError(value); }','~ErrorOwner() { (void)value; }')
 else:
  missing_free=current.replace('        SM50FreeError(err);','        (void)err;')
 assert missing_free != current, 'missing-free control must mutate the actual owned-error release'
 variants=[('current',current,None),('historical',old,'message access requires a live owned error object'),
           ('initialize-only-control',init_only,'message access requires a live owned error object'),
           ('missing-free-control',missing_free,'owned error is released exactly once')]
 results=[]
 with tempfile.TemporaryDirectory(prefix='dxmt-shader-init-failure-') as directory:
  scratch=Path(directory)
  # Copy the entire actual C thunk TU unchanged. Only its Wine transport header
  # is replaced in this private directory; protocol/public headers stay actual.
  shutil.copy2(source/'src/winemetal/airconv_thunks.c',scratch/'airconv_thunks.c')
  (scratch/'wineunixlib.h').write_text('#include <stdint.h>\ntypedef int32_t NTSTATUS;\nextern int32_t mock_unix(unsigned int, void *);\n#define WINE_UNIX_CALL(code,args) mock_unix(code,args)\n')
  common=['-O1','-gline-tables-only','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-private-field',
          '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-ftrivial-auto-var-init=pattern',
          '-I',str(scratch),'-I',str(source/'src/winemetal'),'-I',str(source/'src/airconv'),'-I',str(source/'src/util'),
          '-I',str(source/'src/d3d11')]
  if args.arch:common+=['-arch',args.arch]
  objects=[]
  for name,cfile in [('thunks',scratch/'airconv_thunks.c'),('sha1',source/'src/util/sha1/sha1.c')]:
   obj=scratch/(name+'.o');objects.append(str(obj))
   subprocess.run([os.environ.get('CC','clang'),'-std=c2x',*common,'-c',str(cfile),'-o',str(obj)],check=True,timeout=60)
  for name,caller,rejection in variants:
   cpp=scratch/(name+'.cpp');binary=scratch/name;cpp.write_text(generated(caller))
   subprocess.run([os.environ.get('CXX','clang++'),'-std=c++20','-fno-exceptions','-fno-rtti',*common,
                   str(cpp),str(source/'src/util/sha1/sha1_util.cpp'),*objects,'-o',str(binary)],check=True,timeout=60)
   launch=[str(binary)]
   if args.arch and platform.machine()!=args.arch:launch=['/usr/bin/arch','-'+args.arch,*launch]
   run=subprocess.run(launch,capture_output=True,text=True,timeout=20,env={**os.environ,'UBSAN_OPTIONS':'halt_on_error=1'})
   output=run.stdout+run.stderr
   if rejection:assert run.returncode==1 and 'CONTRACT: '+rejection in output,f'{name}: {run.returncode}\n{output}'
   else:assert run.returncode==0,f'{name}: {run.returncode}\n{output}'
   assert 'ERROR: AddressSanitizer' not in output and 'runtime error:' not in output,output
   record={'variant':name,'exit':run.returncode,'expected_rejection':rejection,'output':output.strip()};results.append(record)
   print(json.dumps(record),flush=True)
  assert sum(p.stat().st_size for p in scratch.iterdir() if p.is_file())<64*1024*1024,'64 MiB scratch cap'
 assert all((source/p).read_bytes()==data for p,data in snapshots.items()),'production sources changed during verification'
 assert (HERE/'fixture.cpp.in').read_bytes()==fixture_snapshot and Path(__file__).read_bytes()==runner_snapshot,'test sources changed during verification'
 print(json.dumps({'arch':args.arch or platform.machine(),'baseline_ref':BASELINE,
                   'scope':'production CreateShader, wrapper constructor/destructor, SHA1 and complete C thunk TU; mock UNIX_CALL; no Wine/native parser/GPU/game',
                   'sources_sha256':{p:hashlib.sha256(data).hexdigest() for p,data in snapshots.items()},
                   'fixture_sha256':hashlib.sha256(fixture_snapshot).hexdigest(),
                   'runner_sha256':hashlib.sha256(runner_snapshot).hexdigest(),'results':results},indent=2))


if __name__=='__main__':main()
