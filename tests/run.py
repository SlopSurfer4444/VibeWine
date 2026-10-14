#!/usr/bin/env python3
"""Offline source fixtures on macOS. No Wine launch or installed-file changes."""
from pathlib import Path
import subprocess, tempfile, sys
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='wine-audit-fixtures-') as tmp:
 out=Path(tmp)
 for name,file in [('display','cocoa_app.m'),('retina','display.c')]:
  subprocess.run([sys.executable,str(root/'tests'/name/'run_tests.py'),str(root/'source/dlls/winemac.drv'/file),str(out/name)],check=True,stdout=subprocess.DEVNULL)
  print(name,'PASS',flush=True)
 flags=['xcrun','clang','-fblocks','-Wall','-Wextra','-Werror=implicit-function-declaration','-fsanitize=address,undefined','-framework','Cocoa','-I'+str(root/'source/dlls/winemac.drv')]
 for source in sorted((root/'tests/ax').glob('*.m')):
  binary=out/source.stem
  subprocess.run(flags+[str(source),'-o',str(binary)],check=True)
  subprocess.run([str(binary)],check=True)
 print('Offline fixture suite complete. This does not establish real-game acceptance.')
