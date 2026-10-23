#!/usr/bin/env python3
from pathlib import Path
import os,subprocess,json,hashlib
HERE=Path(__file__).resolve().parent
REPO=HERE.parents[2]
OUT=REPO/'build/bridge-port-profile';OUT.mkdir(parents=True,exist_ok=True)
results=[]
for name,flags in [('regular',[]),('x86_64',['-arch','x86_64']),('sanitized',['-fsanitize=address,undefined'])]:
 binary=OUT/name
 subprocess.run(['xcrun','clang','-fno-objc-arc','-Wall','-Wextra','-Werror',*flags,'-I'+str(REPO/'src/winemetal/unix'),'-framework','Foundation',str(HERE/'profile.m'),'-o',str(binary)],check=True,capture_output=True,text=True,timeout=60)
 result=subprocess.run([str(binary)],check=True,capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),timeout=20)
 results.append(dict(variant=name,output=result.stdout))
(OUT/'receipt.json').write_text(json.dumps(dict(scope='Foundation layer mocks and actual profile helper; no UI/GPU/Wine/game',results=results,sources={str(p.relative_to(REPO)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [HERE/'profile.m',Path(__file__),REPO/'src/winemetal/unix/wave7_ds3_profile.inc']}),indent=2)+'\n')
print(json.dumps(dict(passed=True,variants=len(results),receipt=str(OUT/'receipt.json'))))
