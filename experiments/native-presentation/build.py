#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Offline build/sign/self-test only; never starts an AppKit application."""
import hashlib,json,plistlib,shutil,subprocess,tempfile
from pathlib import Path
G=Path(__file__).resolve().parent;checks=[]
def run(argv,expected=0):
 q=subprocess.run([str(x) for x in argv],capture_output=True,text=True,timeout=60);checks.append(dict(argv=[str(x) for x in argv],exit=q.returncode,stdout=q.stdout,stderr=q.stderr));(G/'checks.json').write_text(json.dumps(checks,indent=2)+'\n')
 if q.returncode!=expected:raise RuntimeError(q.stderr or q.stdout)
for arch in ['arm64','x86_64']:
 binary=G/('fixture-'+arch)
 run(['/usr/bin/clang','-arch',arch,'-fobjc-arc','-Wall','-Wextra','-Werror',G/'fixture.m','-o',binary,'-framework','AppKit','-framework','Metal','-framework','QuartzCore','-framework','CoreGraphics'])
 app=G/('Native Present '+arch+'.app');mac=app/'Contents/MacOS';mac.mkdir(parents=True,exist_ok=True);shutil.copy2(binary,mac/'fixture')
 bundle='local.native.present.visibility.v3.'+arch
 (app/'Contents/Info.plist').write_bytes(plistlib.dumps(dict(CFBundleExecutable='fixture',CFBundleIdentifier=bundle,CFBundleName='Native Present '+arch,CFBundlePackageType='APPL',CFBundleVersion='1',NSPrincipalClass='NSApplication',NSHighResolutionCapable=True,LSUIElement=False)))
 run(['/usr/bin/codesign','--force','--sign','-','--identifier',bundle,app]);run(['/usr/bin/codesign','--verify','--strict',app]);run(['/usr/bin/file',mac/'fixture']);run([mac/'fixture','--self-test']);run([mac/'fixture'],64)
 with tempfile.TemporaryDirectory(prefix='native-present-exit-') as temporary:
  for flag,expected in [('--self-test-no-start-exit',70),('--self-test-complete-exit',0)]:
   output=Path(temporary)/(flag+'.jsonl');run([mac/'fixture',flag,output],expected)
   row=json.loads(output.read_text());assert row['intendedExitCode']==expected and row['offlineExitFlowTest'] is True
   checks.append(dict(name='actual-binary-flushed-exit',architecture=arch,flag=flag,expectedExit=expected,record=row));(G/'checks.json').write_text(json.dumps(checks,indent=2)+'\n')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
files=[G/'fixture.m',G/'run.py',G/'build.py',G/'checks.json',G/'fixture-arm64',G/'fixture-x86_64']
for arch in ['arm64','x86_64']:files.extend(p for p in (G/('Native Present '+arch+'.app')).rglob('*') if p.is_file())
manifest=dict(state='BUILT_SELF_TESTED_NOT_GUI_LAUNCHED',bindings={str(p.relative_to(G)):sha(p) for p in files},sameSourceForArchitectures=True,phaseSeconds=[0,4,8,12],settledPhaseSeconds=[2,6,10],maxStartWaitSeconds=30,hostDeadlineSeconds=dict(default=25,waitForStart=55),hostReapSeconds=2,callbackFinalizationGuaranteed=False)
(G/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps(dict(state=manifest['state'],manifestSha256=sha(G/'manifest.json'))))
