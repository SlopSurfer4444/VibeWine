#!/usr/bin/env python3
"""Build/test the private immutable-snapshot consumer. No live trace access."""
import hashlib,json,subprocess,time
from pathlib import Path

ROOT=Path(__file__).resolve().parent
CONSUMER=ROOT/'consumer'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    compile_cmd=['clang++','-std=c++20','-O2','-Wall','-Wextra',str(CONSUMER/'trace_controller.cpp'),'-o',str(CONSUMER/'cs2trace')]
    test_cmd=['python3','-m','unittest','discover','-s',str(CONSUMER),'-p','test*.py','-v']
    results=[]
    for name,command in [('compile',compile_cmd),('tests',test_cmd)]:
        result=subprocess.run(command,text=True,capture_output=True)
        log=ROOT/(name+'-results.txt');log.write_text(result.stdout+result.stderr)
        results.append({'name':name,'command':command,'exit_code':result.returncode,'log':str(log),'log_sha256':sha(log)})
        if result.returncode:break
    inputs=list(CONSUMER.glob('*.py'))+list(CONSUMER.glob('*.cpp'))+list(CONSUMER.glob('*.hpp'))+[ROOT/'protocol/src/d3d11/cs2_trace_protocol.hpp']
    sources={str(p.relative_to(ROOT)):sha(p) for p in sorted(inputs)}
    result={'purpose':'offline consumer build and actual binary v2 fixture decoding; no live/installed acceptance','checks':results,'candidate_sources_sha256':sources,'controller_sha256':sha(CONSUMER/'cs2trace') if (CONSUMER/'cs2trace').exists() else None}
    (ROOT/'receipt.json').write_text(json.dumps(result,indent=2)+'\n')
    return any(x['exit_code'] for x in results)
if __name__=='__main__':raise SystemExit(main())
