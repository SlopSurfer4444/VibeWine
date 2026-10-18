#!/usr/bin/env python3
"""Build isolated fixtures against the checked-out bridge; never start a game."""
from pathlib import Path
import os
import subprocess
import tempfile
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BRIDGE = ROOT / 'src/winemetal'
def run(args, **kw):
    subprocess.run([str(x) for x in args], check=True, timeout=60, **kw)
def structure(text, name):
    start=text.index('struct '+name+' {'); cursor=text.index('{',start)+1; depth=1
    while depth:
        depth+=(text[cursor]=='{')-(text[cursor]=='}'); cursor+=1
    return text[start:cursor]+';'
def main():
    clang=['xcrun','clang']
    syntax=['-fsyntax-only','-x','objective-c','-fblocks','-fno-objc-arc','-Werror','-Wno-deprecated-declarations']
    syntax += ['-I'+str(ROOT/p) for p in ('include','include/native','src/airconv','src/winemetal')]
    for mode in ([],['-DDXMT_NATIVE']):
        run(clang+syntax+mode+[BRIDGE/'unix/winemetal_unix.c'])
    common=['-fblocks','-fno-objc-arc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(BRIDGE/'unix'),'-framework','Foundation','-framework','Metal','-framework','CoreFoundation']
    with tempfile.TemporaryDirectory(prefix='vibewine-native-') as temp:
        out=Path(temp)
        run(clang+['-dynamiclib',HERE/'capture_mock.c','-o',out/'capture_mock.dylib'])
        for name in ('listener','feedback'):
            executable=out/name
            run(clang+common+[HERE/('test_'+name+'.m'),'-o',executable])
            run([executable],env=dict(os.environ,W7FB_CAPTURE_MOCK=str(out/'capture_mock.dylib')))
        hdr=(BRIDGE/'winemetal.h').read_text(); thunks=(BRIDGE/'winemetal_thunks.h').read_text()
        types=[structure(hdr,n) for n in ('WMTLayerProps','WMTEDRValue','WMTHDRMetadata','WMTDisplayDescription')]
        types += [structure(thunks,n) for n in ('unixcall_query_display_setting','unixcall_update_display_setting','unixcall_query_display_setting_for_layer')]
        fixture=out/'snapshot.m';fixture.write_text((HERE/'snapshot-fixture.m.in').read_text().replace('/* TYPES */','\n'.join(types)))
        run(clang+common+['-Wno-unused-parameter','-Wno-unused-function',fixture,'-o',out/'snapshot'])
        run([out/'snapshot'])
    print('PASS: bridge syntax (Wine/native), feedback, listener, display snapshots')
if __name__=='__main__': main()
