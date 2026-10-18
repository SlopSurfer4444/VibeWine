#!/usr/bin/env python3
"""Compile D3D11/DXGI translation units and matching Metal helper, without linking.

This is an ABI/source check, NOT a distributable build: the matched AIRCONV and
native bridge must also build before a runtime can be packaged.
Required: LLVM_MINGW (bin directory), Apple Metal toolchain. Output stays outside
source by default. Does not execute game binaries or touch installed Wine.
"""
import concurrent.futures, hashlib, json, os, re, subprocess, sys
from pathlib import Path
src=Path(__file__).resolve().parents[1]
out=Path(sys.argv[1]).resolve(); out.mkdir(parents=True, exist_ok=True)
tc=Path(os.environ['LLVM_MINGW'])
def run(args):
 p=subprocess.run([str(x) for x in args],capture_output=True,text=True)
 if p.returncode: raise RuntimeError(' '.join(map(str,args))+'\n'+p.stdout+p.stderr)
 return p.stdout
metal=src/'src/dxmt/dxmt_command.metal'
run(['xcrun','-sdk','macosx','metal','-o',out/'dxmt_command.air','-c',metal,'-fmodules-cache-path='+str(out/'module-cache')])
run(['xcrun','-sdk','macosx','metallib','-o',out/'dxmt_command.metallib',out/'dxmt_command.air'])
(out/'dxmt_command.h').write_text(run(['xxd','-n','dxmt_command','-i',out/'dxmt_command.metallib']))
(out/'version.h').write_text('#pragma once\n#define DXMT_VERSION "'+run(['git','-C',src,'describe','--always','--dirty']).strip()+'"\n')
def items(folder,name):
 path=src/'src'/folder/'meson.build'; txt=path.read_text()
 body=re.search(r'\b'+name+r'\s*=\s*(?:files\()?\[(.*?)\]',txt,re.S).group(1)
 return [path.parent/x for x in re.findall(r"^\s*'([^']+\.(?:cpp|c))'",body,re.M)]
files=[]
for folder,names in [('d3d11',['d3d11_src','d3d10_src']),('dxmt',['dxmt_src']),('util',['util_src']),('dxgi',['dxgi_src'])]:
 for name in names:files+=items(folder,name)
files += [src/'src/util'/x for x in ['wsi_monitor_win32.cpp','wsi_window_win32.cpp','wsi_platform_win32.cpp']]
inc=[src/'include',src/'libs',out]+[src/'src'/x for x in ['util','airconv','winemetal','dxmt','dxgi']]
flags=['-O0','-DNDEBUG','-DNOMINMAX','-D_WIN32_WINNT=0xa00','-DDXMT_PAGE_SIZE=4096','-fblocks','-Wno-ignored-attributes','-Wno-microsoft-exception-spec','-Wno-unknown-pragmas','-Wno-extern-c-compat']+['-I'+str(p) for p in inc]
def compile(p):
 obj=out/(str(p.relative_to(src)).replace('/','_')+'.o')
 run([tc/('x86_64-w64-mingw32-clang++' if p.suffix=='.cpp' else 'x86_64-w64-mingw32-clang'),*(['-std=c++20'] if p.suffix=='.cpp' else []),*flags,'-c',p,'-o',obj])
 print('PASS '+str(p.relative_to(src)),flush=True)
 return str(p.relative_to(src))
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool: compiled=list(pool.map(compile,files))
(out/'result.json').write_text(json.dumps({'compiled':compiled,'metal_sha256':hashlib.sha256((out/'dxmt_command.metallib').read_bytes()).hexdigest(),'linked':False,'runtime_tested':False},indent=2)+'\n')
