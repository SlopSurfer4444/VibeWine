"""Build only DXMT's Windows D3D11 layer; reuse the verified v0.80 Metal bridge."""
from pathlib import Path
import os, subprocess, re, concurrent.futures, json, hashlib, struct
root=Path(__file__).resolve().parent
src=root/'source'; out=root/'build'; out.mkdir(exist_ok=True)
tc=Path(os.environ['LLVM_MINGW'])
cc=tc/'x86_64-w64-mingw32-clang'; cpp=tc/'x86_64-w64-mingw32-clang++'
def run(cmd):
 r=subprocess.run([str(x) for x in cmd],capture_output=True,text=True)
 if r.returncode: raise RuntimeError(' '.join(map(str,cmd))+'\n'+r.stdout+r.stderr)
 return r.stdout
for name in ['dxgi','winemetal']:
 dll=Path(os.environ['DXMT_RELEASE'])/'x86_64-windows'/f'{name}.dll'
 exports=re.findall(r'Name: (\S+)',run([tc/'llvm-readobj','--coff-exports',dll]))
 definition=out/f'{name}.def'; definition.write_text(f'LIBRARY {name}.dll\nEXPORTS\n'+'\n'.join(exports)+'\n')
 run([tc/'llvm-dlltool','-m','i386:x86-64','-d',definition,'-l',out/f'lib{name}.a'])
# Preserve the official release's exact internal Metal helper library.
# The first 64-bit size at header+16 is MTLBHeader.FileSize.
binary=(Path(os.environ['DXMT_RELEASE'])/'x86_64-windows/d3d11.dll').read_bytes()
libraries=[]
for match in re.finditer(b'MTLB',binary):
 offset=match.start()
 if offset+88>len(binary): continue
 size=struct.unpack_from('<Q',binary,offset+16)[0]
 if 88<size<len(binary)-offset:
  blob=binary[offset:offset+size]
  if b'clear_buffer_uint' in blob and b'gs_draw_arguments_marshal' in blob:
   libraries.append((offset,blob))
assert len(libraries)==1, 'Expected one exact helper Metal library'
offset,blob=libraries[0]
(out/'dxmt_command.metallib').write_bytes(blob)
(out/'dxmt_command.h').write_text(run(['xxd','-n','dxmt_command','-i',out/'dxmt_command.metallib']))
(out/'helper-library.json').write_text(json.dumps({'source_dll_sha256':hashlib.sha256(binary).hexdigest(),'offset':offset,'size':len(blob),'sha256':hashlib.sha256(blob).hexdigest()},indent=2))
(out/'version.h').write_text('#pragma once\n#define DXMT_VERSION "v0.80-cs2-early-compile-v1-prewarm1"\n')
def meson_list(path,name):
 txt=path.read_text(); body=re.search(r'\b'+name+r'\s*=\s*(?:files\()?\[(.*?)\]',txt,re.S).group(1)
 return [path.parent/x for x in re.findall(r"^\s*'([^']+\.(?:cpp|c))'",body,re.M)]
files=[]
for folder,names in [('d3d11',['d3d11_src','d3d10_src']),('dxmt',['dxmt_src']),('util',['util_src'])]:
 for name in names: files+=meson_list(src/'src'/folder/'meson.build',name)
files += [src/'src/util'/x for x in ['wsi_monitor_win32.cpp','wsi_window_win32.cpp','wsi_platform_win32.cpp']]
files += meson_list(src/'libs/DXBCParser/meson.build','dxbc_parser_src')
includes=[src/'include',src/'libs',out]+[src/'src'/p for p in ['util','airconv','winemetal','dxmt','dxgi']]
flags=['-O2','-DNDEBUG','-DNOMINMAX','-D_WIN32_WINNT=0xa00','-DDXMT_PAGE_SIZE=4096','-fblocks','-Wno-ignored-attributes','-Wno-microsoft-exception-spec','-Wno-unknown-pragmas','-Wno-extern-c-compat']
flags += ['-I'+str(p) for p in includes]
headers_latest=max(p.stat().st_mtime for p in (src/'src').rglob('*.hpp'))
def compile(p):
 obj=out/(str(p.relative_to(src)).replace('/','_')+'.o')
 if obj.exists() and obj.stat().st_mtime>max(p.stat().st_mtime,headers_latest): return obj
 run([cpp if p.suffix=='.cpp' else cc,*(['-std=c++20'] if p.suffix=='.cpp' else []),*flags,'-c',p,'-o',obj])
 print('Compiled '+p.name,flush=True);return obj
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
 objects=list(pool.map(compile,files))
dll=out/'d3d11.dll'
run([cpp,'-shared','-static','-Wl,--file-alignment=4096','-Wl,--no-insert-timestamp',*objects,src/'src/d3d11/d3d11.def','-L'+str(out),'-ldxgi','-lwinemetal','-luser32','-lgdi32','-lole32','-luuid','-lsetupapi','-lshlwapi','-ladvapi32','-o',dll])
run([os.environ['WINEBUILD'],'--builtin',dll])
(out/'build.json').write_text(json.dumps({'dll':str(dll),'sha256':hashlib.sha256(dll.read_bytes()).hexdigest(),'compiler':str(cpp),'source_files':len(files)},indent=2))
print('Built '+str(dll),flush=True)
