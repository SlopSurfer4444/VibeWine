#!/usr/bin/env python3
import argparse, hashlib, json, subprocess
from pathlib import Path

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def extract(text, anchor):
    start=text.index(anchor, text.index('@implementation WineApplicationController')); body=text.index('{',start); depth=0
    for i in range(body,len(text)):
        if text[i]=='{': depth+=1
        elif text[i]=='}':
            depth-=1
            if not depth: return text[start:i+1]
    raise ValueError(anchor)
def main():
    p=argparse.ArgumentParser(); p.add_argument('source',type=Path); p.add_argument('output',type=Path); a=p.parse_args()
    source=a.source.resolve(); output=a.output.resolve(); output.mkdir(parents=True,exist_ok=True)
    template=Path(__file__).with_name('fixture-template.m'); fixture=template.read_text(); text=source.read_text()
    for key,anchor in [('SET','- (BOOL) setMode:'),('RELEASE','- (BOOL) releaseDisplayCaptureSavingModes:'),('CAPTURED','- (BOOL) areDisplaysCaptured'),('ACTIVE','- (void)applicationDidBecomeActive:')]:
        fixture=fixture.replace('@@'+key+'@@',extract(text,anchor))
    path=output/'fixture.m'; path.write_text(fixture); binary=output/'fixture'
    cmd=['/usr/bin/clang','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-g','-framework','Foundation',str(path),'-o',str(binary)]
    build=subprocess.run(cmd,capture_output=True,text=True); (output/'build.log').write_text(build.stdout+build.stderr)
    if build.returncode: raise RuntimeError(build.stderr)
    run=subprocess.run([str(binary)],capture_output=True,text=True); (output/'run.log').write_text(run.stdout+run.stderr)
    result=json.loads(run.stdout)
    result.update({'source':str(source),'sourceSha256':sha(source),'templateSha256':sha(template),'fixtureSha256':sha(path),'buildCommand':cmd,'exitCode':run.returncode,'foundationOnly':True,'wineLaunched':False,'installed':False})
    (output/'result.json').write_text(json.dumps(result,indent=2)+'\n'); print(json.dumps(result)); return run.returncode
if __name__=='__main__': raise SystemExit(main())
