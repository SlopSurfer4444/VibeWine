#!/usr/bin/env python3
"""Compile exact candidate method bodies against Foundation-only display/queue fakes."""
from pathlib import Path
import hashlib, json, subprocess
root = Path(__file__).resolve().parents[1]
source = (root/'source/winemac.drv/cocoa_app.m').read_text()
methods = ['releaseDisplayCaptureSavingModes:', 'beginDisplayCaptureRelease', 'retryDisplayCaptureRelease', 'updateFullscreenWindows', 'setMode:', 'areDisplaysCaptured', 'realizeLatentDisplayModes']
bodies=[]
for name in methods:
    import re
    match = re.search(r'    - \([^\n]+\) ?'+re.escape(name)+r'[^\n]*\n    \{', source)
    assert match, name
    opening=source.index('{',match.start()); depth=1; i=opening+1
    while depth:
        if source[i]=='{': depth+=1
        elif source[i]=='}': depth-=1
        i+=1
    bodies.append(source[match.start():i])
start=source.index('int macdrv_display_capture_cleanup_complete_v1(void)')
opening=source.index('{',start); depth=1; i=opening+1
while depth:
    if source[i]=='{': depth+=1
    elif source[i]=='}': depth-=1
    i+=1
query=source[start:i]
fixture=(root/'display-tests/fixture.m.in').read_text().replace('/* EXTRACTED METHODS */','\n\n'.join(bodies)).replace('/* EXTRACTED QUERY */', query)
out=root/'display-tests/extracted.m'; out.write_text(fixture)
subprocess.run(['clang','-fblocks','-Werror','-Wno-objc-method-access','-framework','Foundation',str(out),'-o',str(root/'display-tests/display-tests')],check=True)
result=subprocess.run([str(root/'display-tests/display-tests')],check=True,text=True,capture_output=True)
print(result.stdout)
(root/'display-tests/result.json').write_text(json.dumps({'source_sha256':hashlib.sha256(source.encode()).hexdigest(),'methods':methods,'result':result.stdout.strip(),'scope':'Extracted Objective-C method bodies, Foundation fake CG calls; no AppKit or physical display operations'},indent=2)+'\n')
