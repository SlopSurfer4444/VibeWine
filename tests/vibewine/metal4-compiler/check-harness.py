#!/usr/bin/env python3
"""CPU-only checks for the corpus extraction and child collection boundary."""
import argparse
import importlib.util
import json
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from bounded_child import run


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--wine-source',type=Path,required=True)
    a=p.parse_args();here=Path(__file__).resolve().parent
    spec=importlib.util.spec_from_file_location('owned_prepare',here/'prepare-corpus.py')
    prepare=importlib.util.module_from_spec(spec);spec.loader.exec_module(prepare)
    snapshot=a.wine_source.read_bytes();corpus=prepare.extract(snapshot)
    assert len(corpus)==5 and prepare.extract(snapshot)==corpus
    damaged=snapshot.replace(b'0x00000140, 0x00000003,',b'0x00000141, 0x00000003,',1)
    assert damaged!=snapshot
    try:prepare.extract(damaged)
    except AssertionError:pass
    else:raise AssertionError('invalid DXBC container size accepted')
    checks=['five-source-array-snapshots','invalid-container-size-rejected']
    result=run([sys.executable,'-c','print("bounded-ok")'])
    assert result.returncode==0 and result.stdout==b'bounded-ok\n';checks.append('success')
    try:run([sys.executable,'-c','import os;os.write(1,b"x"*(1024*1024+65536))'])
    except OverflowError:checks.append('stream-output-limit')
    else:raise AssertionError('output overflow accepted')
    try:run([sys.executable,'-c','import time;time.sleep(10)'],timeout=.1)
    except TimeoutError:checks.append('deadline')
    else:raise AssertionError('deadline accepted')
    with tempfile.TemporaryDirectory() as temporary:
        marker=Path(temporary)/'pid'
        code='import os,time;pid=os.fork();(open('+repr(str(marker))+',"w").write(str(os.getpid())),time.sleep(10)) if pid==0 else None'
        try:run([sys.executable,'-c',code],timeout=.3)
        except TimeoutError:pass
        else:raise AssertionError('unclosed descendant accepted')
        pid=int(marker.read_text());time.sleep(.1)
        state=subprocess.run(['ps','-p',str(pid),'-o','stat='],capture_output=True,text=True).stdout.strip()
        assert not state or state.startswith('Z'),state
        checks.append('descendant-group-closure')
    print(json.dumps({'status':'PASS','checks':checks,'native_compiler':False,'gpu':False}))

if __name__=='__main__':main()
