#!/usr/bin/env python3
from pathlib import Path
import argparse,hashlib,json,subprocess
root=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=root.parents[1]/'winemac');args=p.parse_args();source=args.source.resolve()
baseline=root.parents[2]/'native/wave7/source/winemac.drv'
def extract(path,token):
 s=path.read_text();start=s.index(token);begin=s.index('{',start);i=begin+1;depth=1
 while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[start:i]
paths=['window.c','macdrv.h','macdrv_cocoa.h']
results={}
for label,tree in [('candidate',source),('baseline-negative-control',baseline)]:
 fixture=(root/'fixture.cpp').read_text()
 for marker,path,token in [('UPDATE','window.c','static void macdrv_client_surface_update('),('RECT_CONVERSION','macdrv.h','static inline CGRect cgrect_from_rect('),('RETINA_CONVERSION','macdrv_cocoa.h','static inline CGRect cgrect_mac_from_win(')]:
  fixture=fixture.replace('// '+marker,extract(tree/path,token))
 generated=root/(label+'.cpp');generated.write_text(fixture);exe=root/(label+'-test')
 command=['clang++','-std=c++20','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-DEXPECT_TOPLEVEL_NOQUERY='+('1' if label=='candidate' else '0'),'-DEXPECT_CHILD_PARENT_RELATIVE='+('1' if label=='candidate' else '0'),'-fsanitize=address,undefined',str(generated),'-o',str(exe)]
 c=subprocess.run(command,capture_output=True,text=True);(root/(label+'-build.log')).write_text(c.stdout+c.stderr)
 if c.returncode:print(c.stderr);raise SystemExit(c.returncode)
 r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=15)
 result={'source':str(tree),'hashes':{f:hashlib.sha256((tree/f).read_bytes()).hexdigest() for f in paths},'fixture_sha256':hashlib.sha256((root/'fixture.cpp').read_bytes()).hexdigest(),'command':command,'returncode':r.returncode,'stdout':r.stdout,'stderr':r.stderr,'wineStarted':False,'fullBuild':False,'installedChanges':False}
 results[label]=result;(root/(label+'-result.json')).write_text(json.dumps(result,indent=2)+'\n');(root/(label+'-run.log')).write_text(r.stdout+r.stderr);print(label+': '+r.stdout,end='')
 if r.stderr:print(r.stderr,end='')
accepted=results['candidate']['returncode']==0 and results['baseline-negative-control']['returncode']==1
(root/'result.json').write_text(json.dumps({'passed':accepted,'candidate':results['candidate']['returncode'],'baselineNegativeControl':results['baseline-negative-control']['returncode'],'wineStarted':False,'fullBuild':False,'installedChanges':False},indent=2)+'\n')
raise SystemExit(0 if accepted else 1)
