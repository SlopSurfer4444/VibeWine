#!/usr/bin/env python3
"""Build real diagnostic include with existing main-ownership snapshot seam."""
from pathlib import Path
import os, subprocess, tempfile
HERE=Path(__file__).resolve().parent
REPO=HERE.parents[2]; BASE=REPO/'src/winemetal'
template=(HERE.parent/'native/snapshot-fixture.m.in').read_text()
prefix=template[:template.index('static CAMetalLayer* layer;')]
prefix=prefix.replace('BOOL fakeVisible,fakeMini;', 'BOOL fakeVisible,fakeMini,fakeOnSpace; NSInteger fakeWindowNumber;')
prefix=prefix.replace('@property(readonly) NSUInteger occlusionState;', '@property(readonly) NSUInteger occlusionState;\n@property(readonly,getter=isOnActiveSpace) BOOL onActiveSpace;\n@property(readonly) NSInteger windowNumber;')
prefix=prefix.replace('- (NSScreen*)screen { CHECK_MAIN(); return fakeScreen; }', '- (NSScreen*)screen { CHECK_MAIN(); return fakeScreen; }\n- (BOOL)isOnActiveSpace { CHECK_MAIN(); return fakeOnSpace; }\n- (NSInteger)windowNumber { CHECK_MAIN(); return fakeWindowNumber; }')
prefix=prefix.replace('BOOL _opaque,', 'BOOL _hidden, _opaque,').replace('@property BOOL opaque,', '@property BOOL hidden, opaque,').replace('ACCESS(BOOL,opaque,setOpaque)', 'ACCESS(BOOL,hidden,setHidden)\nACCESS(BOOL,opaque,setOpaque)')
clock="""#include <time.h>
static _Atomic uint64_t policy_now=1000000000;
static int fixture_policy_clock(clockid_t clock,struct timespec *out) {
 assert(clock==CLOCK_MONOTONIC_RAW); uint64_t now=atomic_load(&policy_now);
 out->tv_sec=now/1000000000;out->tv_nsec=now%1000000000;return 0;
}
#define clock_gettime fixture_policy_clock
#include "wave7_display_snapshot.inc"
#include "wave7_visibility.inc"
#undef clock_gettime
"""
prefix=prefix.replace('#include "wave7_display_snapshot.inc"',clock)
def structure(text,name):
 start=text.index('struct '+name+' {');i=text.index('{',start)+1;depth=1
 while depth:depth+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[start:i]+';'
h=(BASE/'winemetal.h').read_text();t=(BASE/'winemetal_thunks.h').read_text()
types=[structure(h,n) for n in ['WMTLayerProps','WMTEDRValue','WMTHDRMetadata','WMTDisplayDescription']]
types += [structure(t,n) for n in ['unixcall_query_display_setting','unixcall_update_display_setting','unixcall_query_display_setting_for_layer']]
source=prefix.replace('/* TYPES */','\n'.join(types))+'\n'+(HERE/'visibility_cases.m').read_text()
env=dict(os.environ);env.pop('DXMT_SKIP_OCCLUDED_DRAWABLES',None)
with tempfile.TemporaryDirectory(prefix='w7vs-test-') as temp:
 out=Path(temp);src=out/'fixture.m';src.write_text(source)
 for name,flags in [('arm64',[]),('x86_64',['-arch','x86_64']),('sanitized',['-fsanitize=address,undefined'])]:
  binary=out/name
  subprocess.run(['xcrun','clang','-fblocks','-fno-objc-arc','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter',*flags,'-framework','Foundation','-I'+str(BASE/'unix'),str(src),'-o',str(binary)],check=True,timeout=60)
  subprocess.run([str(binary)],check=True,timeout=15,env=env)
