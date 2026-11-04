#!/usr/bin/env python3
"""Compile the procedural PE fixture only; never execute it."""
from pathlib import Path
import hashlib,json,subprocess,os
from verify_imports import verify
OUT=Path(__file__).resolve().parent
COMPILER=Path(os.environ['LLVM_MINGW'])/'x86_64-w64-mingw32-clang++'
argv=[str(COMPILER),'-O2','-Wall','-Wextra','-Werror','-static',str(OUT/'fixture.cpp'),'-o',str(OUT/'fixture.exe'),'-ld3d11','-ldxgi','-ldxguid','-ld3dcompiler','-luser32']
result=subprocess.run(argv,capture_output=True,text=True)
(OUT/'compile.log').write_text(result.stdout+result.stderr)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
(OUT/'compile.json').write_text(json.dumps({'argv':argv,'returncode':result.returncode,'sourceSha256':sha(OUT/'fixture.cpp'),'binarySha256':sha(OUT/'fixture.exe') if result.returncode==0 else None,'runtimeExecuted':False},indent=2)+'\n')
if result.returncode==0: verify(OUT/'fixture.exe',OUT/'imports.json')
raise SystemExit(result.returncode)
