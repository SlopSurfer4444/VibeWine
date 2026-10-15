#!/usr/bin/env python3
"""Source-connected offline fixtures, in a disposable copy; no Wine/game launch."""
from pathlib import Path
import argparse,os,shutil,subprocess,sys,tempfile
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--include-history',action='store_true')
p.add_argument('--mutation-check',action='store_true')
a=p.parse_args();root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='wine-wave7-public-') as d:
 work=Path(d)/'export';shutil.copytree(root,work,ignore=shutil.ignore_patterns('.git','__pycache__'))
 r=work/'renderer/wave7';n=work/'native/wave7'
 def run(*args):
  print('CHECK', ' '.join(str(v).replace(str(work),'<export>') for v in args),flush=True)
  env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
  subprocess.run([str(v) for v in args],cwd=work,check=True,env=env)
 py=sys.executable
 run(py,r/'resize-tests/run.py','--source',r/'source','--label','public')
 run(py,r/'presenter-tests/run.py','--source',r/'source','--sanitizer','address')
 run(py,r/'presenter-tests/run.py','--source',r/'source','--sanitizer','address','--debug')
 run(py,r/'wsi-tests/run_fixture.py','--source',r/'source',*(['--mutation-check'] if a.mutation_check else []))
 run(py,r/'wsi-tests/query_concurrency.py','--source',r/'source')
 run(py,r/'verify_consumer.py')
 run(py,r/'smoke/test_evidence.py')
 for folder in ('lifetime-tests','display-tests','settings-tests','edr-tests','main-executor-tests'):
  run(py,n/folder/'run.py')
 run(py,n/'settings-tests/run.py','--rejected-sync-negative')
 run(py,n/'edr-tests/check_init_failure.py')
 for folder in ('feedback-tests','listener-tests'):
  run('/bin/sh',n/folder/'run.sh')
 if a.include_history:run(py,work/'tests/run_wave6.py')
print('Wave7 portable offline fixtures passed. No live game, input, resolution or FPS acceptance implied.')
