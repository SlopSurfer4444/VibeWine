#!/usr/bin/env python3
"""Extract the production HDR API and presenter bodies; native/display/PSO seams are injected."""
from pathlib import Path
import argparse, hashlib, re, subprocess, tempfile
here=Path(__file__).resolve().parent
parser=argparse.ArgumentParser()
parser.add_argument('--source',type=Path,default=here.parents[2])
parser.add_argument('--baseline-ref',help='use this Git revision of the swapchain only; expect behavioral rejection')
parser.add_argument('--mutation-check',action='store_true')
args=parser.parse_args(); source=args.source.resolve()
paths=['src/d3d11/d3d11_swapchain.cpp','src/dxmt/dxmt_presenter.cpp','src/dxmt/dxmt_presenter.hpp','src/winemetal/winemetal.h','include/native/directx/dxgi1_5.h']
texts={name:(source/name).read_text() for name in paths}
if args.baseline_ref:
 texts[paths[0]]=subprocess.run(['git','show',args.baseline_ref+':'+paths[0]],cwd=source,text=True,capture_output=True,check=True).stdout
for name,text in texts.items():print('SOURCE',hashlib.sha256(text.encode()).hexdigest(),name,flush=True)
def body(text,token):
 start=text.index(token); opening=text.index('{',start); end=opening+1; depth=1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]
unit=(here/'fixture.cpp').read_text()
replacements={
 '// API_TYPE':body(texts[paths[4]],'typedef struct DXGI_HDR_METADATA_HDR10 {')+' DXGI_HDR_METADATA_HDR10;',
 '// WMT_TYPE':body(texts[paths[3]],'struct WMTHDRMetadata {')+';',
 '// PRESENT_METADATA':body(texts[paths[2]],'struct DXMTPresentMetadata {')+';',
 '// CONFIGURATION':body(texts[paths[2]],'  struct PresentationConfiguration {')+';',
 '// PRESENT_STATE':body(texts[paths[2]],'  struct PresentState {')+';',
 '// CHANGE':'void '+body(texts[paths[1]],'Presenter::changeHDRMetadata('),
 '// SYNCHRONIZE':'Presenter::PresentState '+body(texts[paths[1]],'Presenter::synchronizeLayerProperties()'),
 '// SET_HDR':body(texts[paths[0]],'  HRESULT STDMETHODCALLTYPE SetHDRMetaData(').replace(' override {',' {')}
for token,value in replacements.items():unit=unit.replace(token,value)
mutations=[('unconditional success','    if (Type == DXGI_HDR_METADATA_TYPE_NONE) {','    return S_OK;\n    if (Type == DXGI_HDR_METADATA_TYPE_NONE) {'),('NONE size ignored','if (Size != 0)','if (false)'),('HDR10 size ignored','Size != sizeof(DXGI_HDR_METADATA_HDR10)','false'),('metadata copy discarded','hdr_metadata_ = *metadata;','hdr_metadata_ = {};'),('snapshot metadata omitted','if (hdr_metadata) {','if (false) {')]
with tempfile.TemporaryDirectory(prefix='hdr-metadata-') as tmp:
 work=Path(tmp); cpp=work/'fixture.cpp'; exe=work/'fixture'
 def check(code):
  cpp.write_text(code)
  subprocess.run(['clang++','-std=c++20','-Wall','-Wextra','-Werror','-Wno-unused-variable','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(cpp),'-o',str(exe)],check=True)
  return subprocess.run([str(exe)],text=True,capture_output=True,timeout=10)
 result=check(unit)
 if args.baseline_ref:
  if result.returncode==0 or 'FAIL HDR10 silently discarded' not in result.stderr:raise RuntimeError('baseline did not fail at expected behavior: '+result.stdout+result.stderr)
  print('BASELINE REJECTED',args.baseline_ref,result.stderr.strip());raise SystemExit(0)
 print(result.stdout+result.stderr,end='')
 if result.returncode:raise SystemExit(result.returncode)
 if args.mutation_check:
  for name,old,new in mutations:
   if unit.count(old)!=1:raise RuntimeError('mutation seam changed: '+name)
   result=check(unit.replace(old,new,1))
   if result.returncode==0 or 'FAIL ' not in result.stderr:raise RuntimeError('mutation not rejected: '+name)
   print('MUTATION REJECTED',name,result.stderr.strip())
print('EVIDENCE source/control-flow; native display and PSO injected; no real HDR/GPU/window/runtime exercised')
