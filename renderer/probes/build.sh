#!/bin/zsh
set -euo pipefail
cd "${0:A:h}"
python3 - <<'PY'
import hashlib,json,pathlib
p=pathlib.Path('../diagnostic/source/src/d3d11/cs2_trace.hpp')
s=p.read_text();start=s.index('inline void record(');body=s.index('{',start);depth=1;end=body+1
while depth:
    if s[end]=='{':depth+=1
    elif s[end]=='}':depth-=1
    end+=1
record=s[start:end]
pathlib.Path('producer-record.inc').write_text(record+'\n')
pathlib.Path('producer-record-receipt.json').write_text(json.dumps({'source':str(p),'source_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'record_sha256':hashlib.sha256((record+'\n').encode()).hexdigest(),'fixture':'exact record() body; buffer/thread ID stubbed; no Wine process'},indent=2)+'\n')
PY
clang++ -std=c++20 -O2 -Wall -Wextra -Werror trace_controller.cpp -o cs2trace
clang++ -std=c++20 -O2 -Wall -Wextra -Werror trace_tests.cpp -o trace_tests
testroot=$(mktemp -d /private/tmp/cs2trace-tests.XXXXXXXX)
./trace_tests "$testroot" | tee test-results.json
python3 verify_cli.py
print -r -- "$testroot" > test-root.txt
file cs2trace trace_tests > binary-types.txt
shasum -a 256 trace_core.hpp trace_controller.cpp trace_tests.cpp producer-record.inc cs2trace trace_tests > SHA256SUMS
