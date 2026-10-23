#!/usr/bin/env python3
"""Reuse the existing main-ownership display fixture for the actual guard/thunk."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess

HERE=Path(__file__).resolve().parent
REPO=HERE.parents[2]
BASE=REPO/'src/winemetal'
OUT=REPO/'build/native-occlusion-tests'
OUT.mkdir(parents=True,exist_ok=True)
template=(HERE.parent/'native/snapshot-fixture.m.in').read_text()
# Reuse the Foundation fakes and their AppKit-on-main/snapshot-lock assertions.
prefix=template[:template.index('static CAMetalLayer* layer;')]
policy_clock='''#include <time.h>
static _Atomic uint64_t policy_now=1000000000;
static int fixture_policy_clock(clockid_t clock,struct timespec *out) {
 assert(clock==CLOCK_MONOTONIC_RAW); uint64_t now=atomic_load(&policy_now);
 out->tv_sec=now/1000000000; out->tv_nsec=now%1000000000; return 0;
}
#define clock_gettime fixture_policy_clock
#include "wave7_display_snapshot.inc"
#undef clock_gettime
'''
prefix=prefix.replace('#include "wave7_display_snapshot.inc"',policy_clock)
def extract(text,marker):
    start=text.index(marker); i=text.index('{',start)+1; depth=1
    while depth:
        if text[i]=='{': depth+=1
        elif text[i]=='}': depth-=1
        i+=1
    return text[start:i]
header=(BASE/'winemetal.h').read_text(); thunks=(BASE/'winemetal_thunks.h').read_text()
types=[extract(header,'struct '+n+' {')+';' for n in ['WMTLayerProps','WMTEDRValue','WMTHDRMetadata','WMTDisplayDescription']]
types += [extract(thunks,'struct '+n+' {')+';' for n in ['unixcall_query_display_setting','unixcall_update_display_setting','unixcall_query_display_setting_for_layer']]
prefix=prefix.replace('/* TYPES */','\n'.join(types))
# The original fixture declares inversion helpers after its cache include.
helpers=template[template.index('static pthread_mutex_t device_lock='):template.index('static void *cold_reader')]
native=(BASE/'unix/winemetal_unix.c').read_text()
thunk=re.search(r'static NTSTATUS\n_MetalLayer_nextDrawable\(void \*obj\) \{.*?\n\}',native,re.S).group()
fixture=prefix+'\n'+helpers+'\n'+(HERE/'occlusion_cases.m').read_text().replace('/* PRODUCTION_THUNK */',thunk)
(OUT/'fixture.m').write_text(fixture)
results=[]
environment=os.environ.copy(); environment.pop('DXMT_ACQUIRE_DIAGNOSTICS_DIR',None)
environment.pop('DXMT_SKIP_OCCLUDED_DRAWABLES',None); environment['ASAN_OPTIONS']='detect_leaks=0'
for variant,flags in [('regular',[]),('x86_64',['-arch','x86_64']),('sanitized',['-fsanitize=address,undefined'])]:
    binary=OUT/variant
    command=['/usr/bin/clang','-fblocks','-fno-objc-arc','-O1','-g','-Wall','-Wextra','-Werror',
             '-Wno-unused-function','-Wno-unused-parameter',*flags,'-framework','Foundation','-I',str(BASE/'unix'),str(OUT/'fixture.m'),'-o',str(binary)]
    build=subprocess.run(command,capture_output=True,text=True)
    (OUT/(variant+'-build.log')).write_text(build.stdout+build.stderr)
    if build.returncode: raise RuntimeError(build.stderr)
    run=subprocess.run([str(binary)],env=environment,capture_output=True,text=True,timeout=20)
    (OUT/(variant+'-run.log')).write_text(run.stdout+run.stderr)
    if run.returncode: raise RuntimeError(run.stdout+run.stderr)
    results.append(dict(variant=variant,command=command,output=run.stdout,binarySha256=hashlib.sha256(binary.read_bytes()).hexdigest()))
files=[BASE/'unix/wave7_display_snapshot.inc',BASE/'unix/wave7_acquire_diag.inc',BASE/'unix/winemetal_unix.c',HERE/'occlusion_cases.m',Path(__file__),HERE.parent/'native/snapshot-fixture.m.in']
receipt=dict(scope='actual cache/guard/thunk; Foundation AppKit fakes; no UI/GPU/game',
             sources={str(p.relative_to(REPO)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files},results=results)
(OUT/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(dict(passed=True,variants=len(results),receipt=str(OUT/'receipt.json'))))
