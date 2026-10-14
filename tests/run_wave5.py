#!/usr/bin/env python3
"""Run offline source fixtures in a disposable copy; never launch games or Wine."""
from pathlib import Path
import shutil,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='wine-wave5-public-') as d:
 work=Path(d)/'export';shutil.copytree(root,work)
 scripts=['tests/run.py','fixtures/startup/test_startup.py','fixtures/ax/run.py','fixtures/ax-composition/run.py','fixtures/displaylink/run.py','fixtures/monitor/run.py','renderer/guarded/test.py','optional/borderless/tests/borderless-watchdog/run.py']
 for script in scripts:
  print('RUN',script,flush=True);subprocess.run([sys.executable,str(work/script)],cwd=work,check=True)
 subprocess.run(['/bin/zsh',str(work/'renderer/probes/build.sh')],cwd=work,check=True)
 subprocess.run(['/bin/zsh',str(work/'optional/sync-probes/consumer/build.sh')],cwd=work,check=True)
 subprocess.run(['clang++','-std=c++20','-fsanitize=address,undefined','-pthread',str(work/'optional/sync-probes/tests-generation.cpp'),'-o',str(work/'sync-generation')],check=True)
 subprocess.run([str(work/'sync-generation')],check=True)
print('Offline source fixtures passed. No real game, display hardware, CUA or FPS acceptance implied.')
