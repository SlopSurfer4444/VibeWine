#!/usr/bin/env python3
"""Run focused successor source fixtures in a disposable export copy; no Wine/GPU/AppKit app launch."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,sys,tempfile
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--sanitizer',choices=['all','none','address','thread'],default='all')
a=p.parse_args()
bindings=json.loads((HERE/'source-bindings.json').read_text())
for rel,digest in bindings.items():
    if hashlib.sha256((ROOT/rel).read_bytes()).hexdigest()!=digest:
        raise RuntimeError('Frozen source binding changed: '+rel)
with tempfile.TemporaryDirectory(prefix='wave7-focused-public-') as directory:
    work=Path(directory)/'export'
    shutil.copytree(ROOT,work,ignore=shutil.ignore_patterns('.git','__pycache__'))
    tests=work/'renderer/wave7/next-tests'
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
    def run(*args):
        subprocess.run([sys.executable,*map(str,args)],cwd=work,check=True,env=env)
    modes=['none','address','thread'] if a.sanitizer=='all' else [a.sanitizer]
    for suite in ['event77','persistence']:
        for mode in modes:
            run(tests/suite/'tests/run.py','--source',work/'renderer/wave7/source','--sanitizer',mode)
    run(tests/'r1/tests/run.py')
    run(tests/'clear/tests/test_clear_pso.py')
    run(work/'native/wave7/ax-root-tests/run.py')
    run(work/'renderer/wave7/verify_consumer.py')
    run(work/'native/wave7/mask-tests/test_mask_coordinates.py')
print('Focused successor source fixtures passed; no installed runtime, GPU, game or live AX acceptance implied.')
