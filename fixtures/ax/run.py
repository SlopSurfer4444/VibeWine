#!/usr/bin/env python3
"""Execute native bridge regression fixtures with deterministic delayed publication/time."""
from pathlib import Path
import json, subprocess, tempfile
root = Path(__file__).resolve().parent
modes = ['refresh', 'queue-time', 'validate-time', 'publish-time', 'apply-time']
results = []
with tempfile.TemporaryDirectory(prefix='wave5-ax-') as tmp:
    for candidate in ['before', 'after']:
        binary = Path(tmp) / candidate
        cmd = ['xcrun', 'clang', '-fblocks', '-fsanitize=address,undefined', '-framework', 'Cocoa',
               '-I'+str(root.parents[1]/'source/dlls/winemac.drv')]
        if candidate == 'before': cmd += ['-DAX_SOURCE="ax_native.before.inc"']
        subprocess.run(cmd+[str(root/'refresh-deadline.m'), '-o', str(binary)], check=True)
        for mode in modes:
            p = subprocess.run([str(binary), mode], capture_output=True, text=True)
            results.append(dict(candidate=candidate, mode=mode, code=p.returncode, stdout=p.stdout, stderr=p.stderr))
            if (p.returncode == 0) != (candidate == 'after'):
                raise SystemExit(json.dumps(results, indent=2))
(root/'results.json').write_text(json.dumps(results, indent=2)+'\n')
print('Five before-fail/after-pass native AX cases, ASan/UBSan: PASS')
