#!/usr/bin/env python3
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile,shutil
r=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=r.parents[1]/'dxgi-source');p.add_argument('--output',type=Path,default=None);p.add_argument('--negative-control',action='store_true');p.add_argument('--predecessor-negative-control',action='store_true');a=p.parse_args();source=a.source.resolve();temporary=tempfile.TemporaryDirectory(prefix='dxgi-closest-');out=a.output.resolve() if a.output else Path(temporary.name);out.mkdir(parents=True,exist_ok=True)
if a.predecessor_negative_control:
 predecessor=Path(temporary.name)/'predecessor'
 for name in ['dxgi_output.cpp','dxgi_mode_limit.hpp']:
  target=predecessor/'src/dxgi'/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source/'src/dxgi'/name,target)
 subprocess.run(['patch','--batch','--reverse','-p1','-i',str(r.parents[1]/'dxgi-closest-mode.patch')],cwd=predecessor,check=True,capture_output=True,text=True)
 source=predecessor;a.negative_control=True
s=(source/'src/dxgi/dxgi_output.cpp').read_text()
def extract(token):
 start=s.index(token);begin=s.index('{',start);depth=1;i=begin+1
 while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[start:i]
f=(r/'fixture.cpp').read_text().replace('HEADER_PATH',str(source/'src/dxgi/dxgi_mode_limit.hpp'))
f=f.replace('// CONVERT',extract('DXGI_MODE_DESC1 ConvertDisplayMode(')).replace('// FILTER',extract('void FilterModesByDesc('))
f=f.replace('// LIST','HRESULT '+extract('  GetDisplayModeList1(').replace('GetDisplayModeList1(', 'GetDisplayModeList1Impl(',1).replace(' final {',' {',1))
f=f.replace('// CLOSEST','HRESULT '+extract('  FindClosestMatchingMode1(const DXGI_MODE_DESC1 *pModeToMatch,'));(out/'generated.cpp').write_text(f)
cmd=['clang++','-std=c++20','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(out/'generated.cpp'),'-o',str(out/'closest-tests')]
c=subprocess.run(cmd,capture_output=True,text=True);(out/'build.log').write_text(c.stdout+c.stderr)
if c.returncode:print(c.stderr);raise SystemExit(c.returncode)
scenarios=['stable','shrink','shrink-empty','count-empty','growth','growth-shrink','growth-retry-error','growth-retry-empty','count-error','count-more-data','fill-error','persistent-growth','success-overcapacity','fractional','fractional-shrink','cap-growth','stereo-empty','invalid-pair','unknown-format']
results=[]
for name in scenarios:
 for cap in [0,1]:
  proc=subprocess.run([str(out/'closest-tests'),name,str(cap)],capture_output=True,text=True,timeout=10)
  results.append({'scenario':name,'cap':bool(cap),'exitCode':proc.returncode,'stdout':proc.stdout,'stderr':proc.stderr})
h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
failures=[x for x in results if x['exitCode']]
negative_ok=all(any(x['scenario']==name for x in failures) for name in ['shrink-empty','growth','count-error','fill-error','persistent-growth'])
result={'passed':negative_ok if a.negative_control else not failures,'negativeControl':a.negative_control,'source':str(source),'sourceSha256':{f:h(source/f) for f in ['src/dxgi/dxgi_output.cpp','src/dxgi/dxgi_mode_limit.hpp']},'fixtureSha256':h(r/'fixture.cpp'),'compileCommand':cmd,'total':len(results),'passedCases':len(results)-len(failures),'failedCases':len(failures),'results':results,'wineStarted':False,'installedChanges':False}
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');(out/'run.log').write_text(''.join(x['stdout']+x['stderr'] for x in results));print(json.dumps({k:result[k] for k in ['passed','negativeControl','total','passedCases','failedCases']}));raise SystemExit(0 if result['passed'] else 1)
