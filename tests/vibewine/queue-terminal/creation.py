#!/usr/bin/env python3
"""Execute complete production extern-C creation bodies with constructor failure seams."""
import argparse,hashlib,json,subprocess,tempfile,shutil
from pathlib import Path
from run import body
p=argparse.ArgumentParser();p.add_argument('--source-ref');p.add_argument('--expect-baseline',action='store_true');a=p.parse_args()
here=Path(__file__).resolve().parent;root=here.parents[2];path='src/d3d11/d3d11.cpp'
data=subprocess.check_output(['git','show',a.source_ref+':'+path],cwd=root) if a.source_ref else (root/path).read_bytes();source=data.decode()
code='\n'.join(body(source,m) for m in ['extern "C" HRESULT WINAPI\nD3D11CoreCreateDevice(', 'extern "C" HRESULT WINAPI D3D11CreateDeviceAndSwapChain('])
print(json.dumps({'source_ref':a.source_ref,'source_sha256':hashlib.sha256(data).hexdigest(),'generated_sha256':hashlib.sha256(code.encode()).hexdigest(),'fixture_sha256':{f:hashlib.sha256((here/f).read_bytes()).hexdigest() for f in ['creation.py','creation.cpp','run.py']}}),flush=True)
with tempfile.TemporaryDirectory(prefix='queue-create-') as tmp:
 work=Path(tmp);(work/'creation.hpp').write_text(code);exe=work/'fixture'
 subprocess.run([shutil.which('clang++'),'-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I',str(work),str(here/'creation.cpp'),'-o',str(exe)],check=True,timeout=60)
 r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10);print(r.stdout,end='');print(r.stderr,end='')
 if a.expect_baseline:
  assert r.returncode!=0 and 'std::bad_alloc' in r.stderr
  print('OLDFAIL actual extern-C CoreCreateDevice escapes constructor bad_alloc, exit',r.returncode)
 else:assert r.returncode==0,r.returncode
