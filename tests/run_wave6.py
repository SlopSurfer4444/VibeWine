#!/usr/bin/env python3
"""Offline source acceptance in a disposable copy. Does not start Wine or games."""
from pathlib import Path
import shutil, subprocess, sys, tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='wine-wave6-public-') as d:
 work=Path(d)/'export';shutil.copytree(root,work)
 r=work/'renderer/wave6'
 def run(*args):subprocess.run([str(x) for x in args],cwd=work,check=True)
 run(sys.executable,work/'tests/run_wave5.py')
 run(sys.executable,r/'verify_trace.py')
 run(sys.executable,r/'correctness/tests/test_correctness.py',r/'source')
 run('/bin/sh',r/'statistics-tests/run.sh','asan')
 run('/bin/sh',r/'statistics-tests/run.sh','release')
 run(sys.executable,r/'wsi-tests/run_fixture.py','--source',r/'source','--mutation-check')
 run('clang++','-std=c++20','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',r/'tests/policy.cpp','-o',r/'tests/policy')
 run(r/'tests/policy')
 run(sys.executable,r/'tests/test_exclusive.py')
 run(sys.executable,r/'tests/test_exclusive_factory.py')
print('Wave6 public offline source checks passed. No live, CUA, displayed FPS or exclusive-exit acceptance implied.')
