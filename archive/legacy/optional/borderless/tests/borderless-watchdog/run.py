#!/usr/bin/env python3
from pathlib import Path
import subprocess,json,hashlib,sys
root=Path(__file__).resolve().parents[2]
source=root/'bin/ds3-borderless-helper.c'
out=Path(sys.argv[1]) if len(sys.argv)>1 else root/'tests/borderless-watchdog/results'
out.mkdir(parents=True,exist_ok=True)
s=source.read_text()
def extract(marker):
 start=s.index(marker);b=s.index('{',start);level=1;i=b+1
 while level:
  level+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[start:i]
watch=s[s.index('typedef struct {\n    ULONGLONG generation'):s.index('static int image_ok')]
template=Path(__file__).with_name('fixture.c').read_text()
fixture=template.replace('/* WATCH_CODE */',watch).replace('/* WAIT_TARGET */',extract('static int wait_target(')).replace('/* WRITE_METHODS */',extract('static int write_long(')+'\n'+extract('static int position_window('))
f=out/'fixture.c';f.write_text(fixture)
cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g',str(f),'-o',str(out/'fixture')]
b=subprocess.run(cmd,capture_output=True,text=True);(out/'build.log').write_text(b.stdout+b.stderr)
r={'sourceSha256':hashlib.sha256(source.read_bytes()).hexdigest(),'fixtureSha256':hashlib.sha256(f.read_bytes()).hexdigest(),'buildCommand':cmd,'buildExit':b.returncode,'wineStarted':False,'liveGameChanged':False}
if b.returncode==0:
 x=subprocess.run([str(out/'fixture')],capture_output=True,text=True);r.update(exit=x.returncode,stdout=x.stdout,stderr=x.stderr);(out/'run.log').write_text(x.stdout+x.stderr)
(out/'result.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2));sys.exit(r.get('exit',1))
