#!/usr/bin/env python3
from pathlib import Path
import subprocess,hashlib,json,sys
root=Path(__file__).resolve().parents[1]; out=root/'settings-tests'; base=root/'source/bridge/src/winemetal'
hdr=(base/'winemetal.h').read_text(); thunks=(base/'winemetal_thunks.h').read_text(); inc=base/'unix/wave7_display_snapshot.inc'
def extract(text,marker):
 start=text.index(marker);i=text.index('{',start)+1;depth=1
 while depth:
  if text[i]=='{':depth+=1
  elif text[i]=='}':depth-=1
  i+=1
 return text[start:i]
types=[extract(hdr,'struct '+n+' {')+';' for n in ['WMTLayerProps','WMTEDRValue','WMTHDRMetadata','WMTDisplayDescription']]
types += [extract(thunks,'struct '+n+' {')+';' for n in ['unixcall_query_display_setting','unixcall_update_display_setting','unixcall_query_display_setting_for_layer']]
negative='--rejected-sync-negative' in sys.argv
fixture=(out/'snapshot-fixture.m.in').read_text().replace('/* TYPES */','\n'.join(types))
if negative:
 fixture=fixture.replace('#include "wave7_display_snapshot.inc"','#define _MetalLayer_getProps w7ds_safe_getProps\n#include "wave7_display_snapshot.inc"\n#undef _MetalLayer_getProps\n'+(out/'rejected-getprops.inc').read_text())
stem='rejected-sync-negative' if negative else 'snapshot'; (out/(stem+'.m')).write_text(fixture)
cmd=['clang','-fblocks','-fno-objc-arc','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-fsanitize=address,undefined','-framework','Foundation','-I',str(inc.parent),str(out/(stem+'.m')),'-o',str(out/stem)]
r=subprocess.run(cmd,capture_output=True,text=True);(out/(stem+'-build.log')).write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
r=subprocess.run([str(out/stem)],capture_output=True,text=True,timeout=30);(out/(stem+'-run.log')).write_text(r.stdout+r.stderr)
assert (r.returncode==-14 if negative else r.returncode==0),r.stdout+r.stderr
print('EXPECTED SIGALRM: rejected synchronous getter deadlocks on main/device inversion' if negative else r.stdout.strip())
(out/(stem+'-result.json')).write_text(json.dumps({'snapshot_source_sha256':hashlib.sha256(inc.read_bytes()).hexdigest(),'exit_code':r.returncode,'rejected_sync_negative':negative,'output':r.stdout+r.stderr,'native_appkit_gpu_actions':False,'sanitizers':['address','undefined']},indent=2)+'\n')
