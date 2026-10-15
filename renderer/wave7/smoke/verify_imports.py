#!/usr/bin/env python3
"""Inspect PE imports offline and reject accidental distributable C++ runtime DLLs."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,os
OUT=Path(__file__).resolve().parent
INSPECTOR=Path(os.environ['LLVM_MINGW'])/'llvm-readobj'

def verify(binary, receipt):
    binary=Path(binary).resolve()
    command=[str(INSPECTOR),'--coff-imports',str(binary)]
    result=subprocess.run(command,capture_output=True,text=True)
    imports=re.findall(r'^  Name: (.+)$',result.stdout,re.M)
    forbidden=[name for name in imports if name.lower() in ('libc++.dll','libunwind.dll','libgcc_s_seh-1.dll','libstdc++-6.dll','libwinpthread-1.dll')]
    allowed={'kernel32.dll','user32.dll','d3d11.dll','d3dcompiler_47.dll'}
    unknown=[name for name in imports if name.lower() not in allowed and not re.fullmatch(r'api-ms-win-crt-[a-z]+-l1-1-0\.dll',name.lower())]
    required={'kernel32.dll','user32.dll','d3d11.dll','d3dcompiler_47.dll'}
    valid=(result.returncode==0 and 'Format: COFF-x86-64' in result.stdout and len(imports)==len(set(imports)) and
           required.issubset({name.lower() for name in imports}) and not forbidden and not unknown)
    report={'state':'PASS' if valid else 'FAIL','binary':str(binary),'sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
            'command':command,'inspectorExit':result.returncode,'imports':imports,'forbiddenRuntimeImports':forbidden,
            'unknownImports':unknown,'runtimeExecuted':False,'scope':'Offline PE import table inspection; not a Wine loader or GPU execution test'}
    Path(receipt).write_text(json.dumps(report,indent=2)+'\n')
    if not valid: raise RuntimeError(f'Unexpected PE dependencies; see {receipt}')
    return report

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('binary',type=Path);ap.add_argument('--receipt',type=Path,required=True)
    args=ap.parse_args();print(json.dumps(verify(args.binary,args.receipt),indent=2))
