#!/usr/bin/env python3
"""Compile candidate's exact resize method bodies with Win32/resource mocks."""
from pathlib import Path
import argparse, hashlib, json, subprocess
ROOT=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=ROOT.parent/'source');p.add_argument('--label',default='candidate');a=p.parse_args();source=a.source.resolve()
swap=source/'src/d3d11/d3d11_swapchain.cpp';policy=source/'src/util/ds3_borderless.hpp'
s=swap.read_text()
def method(name,rettype="HRESULT"):
    start=s.index('\n  '+name+'(')+1;begin=s.index('{',start);depth=1;i=begin+1
    while depth:
        depth+=(s[i]=='{')-(s[i]=='}');i+=1
    return '  '+rettype+'\n'+s[start:i]
old=(ROOT.parents[1]/'wave6/tests/policy.cpp').read_text()
win32=old[:old.index('using R=')]
win32=win32.replace('#include "../source/src/util/ds3_borderless.hpp"', '#include "'+str(policy)+'"')
# A separate client area is deliberately distinct from old buffers and monitor.
win32=win32.replace('static RECT rect=', 'static RECT client_area={0,0,1600,900};\nstatic RECT rect=')
win32=win32.replace('*r={0,0,rect.right-rect.left-(invalid_client?1:0),rect.bottom-rect.top};', '*r=client_area;')
win32=win32.replace('rect={x,y,x+w,y+h};', 'rect={x,y,x+w,y+h};client_area={0,0,w,h};')
win32=win32.replace('*p=pid;', 'if(p)*p=pid;')
win32=win32.replace('bool GetClientRect(HWND,RECT *r){', 'bool GetClientRect(HWND,RECT *r){client_queries++;')
win32=win32.replace('DWORD GetWindowThreadProcessId(HWND h,DWORD *p){', 'DWORD GetWindowThreadProcessId(HWND h,DWORD *p){owner_queries++;')
win32=win32.replace('void reset(){','void reset(){client_area={0,0,1600,900};')
fixture=(ROOT/'fixture.cpp').read_text()
fixture=fixture.replace('// WIN32_POLICY',win32).replace('// RESIZE_BUFFERS',method('ResizeBuffers')).replace('// RESIZE_TARGET',method('ResizeTarget'))
start=s.index('class ModeSetGuard {');end=s.index('\ntemplate <bool EnableMetalFX>',start)
fixture=fixture.replace('// MODESET_GUARD',s[start:end])
trace_method=None
# TraceResizeState has an inline return type in production.
if '  void TraceResizeState(' in s:
    normalized=s.replace('  void TraceResizeState(', '  TraceResizeState(')
    original=s;s=normalized;trace_method=method('TraceResizeState','void');s=original
fixture=fixture.replace('// TRACE_STATE',trace_method or 'void TraceResizeState(uint32_t,uint32_t){}')
fixture=fixture.replace('SOURCE_ROOT',str(source))
path=ROOT/(a.label+'-generated.cpp');path.write_text(fixture)
(ROOT/'unknwn.h').write_text('struct IUnknown { int QueryInterface(int, void **) { return -1; } };\n')
exe=ROOT/(a.label+'-test')
compile_cmd=['clang++','-std=c++20','-Wall','-Wextra','-Wno-unused-parameter','-fsanitize=address,undefined','-I',str(ROOT),str(path),'-o',str(exe)]
c=subprocess.run(compile_cmd,capture_output=True,text=True);(ROOT/(a.label+'-build.log')).write_text(c.stdout+c.stderr)
if c.returncode: print(c.stderr);raise SystemExit(c.returncode)
r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
result={'source':str(source),'source_sha256':hashlib.sha256(swap.read_bytes()).hexdigest(),'policy_sha256':hashlib.sha256(policy.read_bytes()).hexdigest(),'command':compile_cmd,'returncode':r.returncode,'stdout':r.stdout,'stderr':r.stderr,'fixture_sha256':hashlib.sha256((ROOT/'fixture.cpp').read_bytes()).hexdigest(),'com_sha256':hashlib.sha256((source/'src/util/com/com_pointer.hpp').read_bytes()).hexdigest(),'rc_sha256':hashlib.sha256((source/'src/util/rc/util_rc_ptr.hpp').read_bytes()).hexdigest(),'wineStarted':False,'installedChanges':False}
(ROOT/(a.label+'-result.json')).write_text(json.dumps(result,indent=2)+'\n');print(r.stdout+r.stderr,end='');raise SystemExit(r.returncode)
