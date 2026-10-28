#!/usr/bin/env python3
"""Bounded standalone recipe codec validation; no LLVM, runtime or installation."""
import argparse,json,pathlib,subprocess,tempfile
HERE=pathlib.Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source-dir',type=pathlib.Path,default=HERE.parents[2])
p.add_argument('--arch',choices=('arm64','x86_64'),default='arm64')
p.add_argument('--sanitize',action='store_true')
a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='dxmt-worker-recipe-') as scratch:
 binary=str(pathlib.Path(scratch)/'fixture')
 command=['xcrun','clang++','-std=c++20','-arch',a.arch,'-UNDEBUG','-I'+str(a.source_dir/'src/airconv'),str(HERE/'recipe_fixture.cpp'),'-o',binary]
 if a.sanitize:command+=['-fsanitize=address,undefined','-fno-sanitize-recover=all']
 subprocess.run(command,check=True,timeout=60)
 subprocess.run([binary],check=True,timeout=30)
print(json.dumps({'arch':a.arch,'sanitizers':a.sanitize,'recipe_codec':'passed'}))
