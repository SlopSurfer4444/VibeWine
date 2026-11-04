#!/usr/bin/env python3
"""Run pure AX predicate/merge checks, or compile the AppKit harness without launch."""
from pathlib import Path
import argparse,hashlib,subprocess,tempfile
HERE=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--compile-appkit-only',action='store_true')
a=p.parse_args()
source=HERE/('fixture.m' if a.compile_appkit_only else 'test_policy.m')
with tempfile.TemporaryDirectory(prefix='wave7-ax-root-') as directory:
    binary=Path(directory)/'fixture'
    command=['/usr/bin/clang','-Wall','-Wextra','-Werror',str(source),'-o',str(binary),'-framework','Cocoa']
    if a.compile_appkit_only:command[1:1]=['-arch','x86_64']
    subprocess.run(command,check=True,timeout=60)
    if a.compile_appkit_only:
        print('COMPILED_NOT_LAUNCHED',hashlib.sha256(binary.read_bytes()).hexdigest())
    else:
        subprocess.run([str(binary)],check=True,timeout=15)
