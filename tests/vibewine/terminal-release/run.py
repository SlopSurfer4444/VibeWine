#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compile full production PE-thunk source at a native seam; never run Wine."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--keep', type=Path)
    parser.add_argument('--arch', choices=['native', 'x86_64'], default='native')
    parser.add_argument('--source-ref', default='828429ca6b142230399d98272207af7714f12895',
                        help='Pre-fix baseline for old-fails/new-passes control')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    root = here.parents[2]
    production = root / 'src/winemetal/winemetal_thunks.c'
    old = subprocess.check_output(['git', 'show', args.source_ref + ':src/winemetal/winemetal_thunks.c'],
                                  cwd=root, text=True)
    reports = []
    with tempfile.TemporaryDirectory(prefix='dxmt-terminal-release-') as directory:
        work = Path(directory)
        (work / 'baseline.c').write_text(old)
        for name, source in [('baseline', work / 'baseline.c'), ('candidate', production)]:
            binary = work / name
            command = ['clang', '-std=c2x', '-O1', '-g', '-fno-objc-arc', '-pthread',
                       '-Wall', '-Wextra', '-Werror', '-I', str(here),
                       '-I', str(root / 'src/winemetal'), str(source), str(here / 'fixture.m'),
                       '-framework', 'Foundation', '-o', str(binary)]
            if args.arch != 'native':
                command += ['-arch', args.arch]
            built = subprocess.run(command, capture_output=True, text=True, timeout=30)
            (work / (name + '-build.txt')).write_text(built.stdout + built.stderr)
            if built.returncode:
                raise RuntimeError(built.stdout + built.stderr)
            for mode in ['runtime', 'terminal']:
                # subprocess owns this exact child; no process lookup, Wine prefix,
                # live-game signal, or blanket process cleanup is involved.
                child = subprocess.Popen([str(binary), mode], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                timeout = False
                try:
                    stdout, stderr = child.communicate(timeout=2)
                except subprocess.TimeoutExpired:
                    timeout = True
                    child.kill()
                    stdout, stderr = child.communicate(timeout=2)
                expected_timeout = name == 'baseline' and mode == 'terminal'
                expected_output = b'DN' if expected_timeout else (b'NS' if mode == 'runtime' else b'DS')
                if timeout != expected_timeout or stdout != expected_output or stderr:
                    raise RuntimeError((name, mode, timeout, child.returncode, stdout, stderr))
                if not timeout and child.returncode != 20:
                    raise RuntimeError((name, mode, child.returncode))
                reports.append({'source': name, 'mode': mode, 'pid': child.pid,
                                'exit_code': child.returncode, 'deadline_cleanup': timeout,
                                'receipt': stdout.decode(), 'accepted': True})
        report = {'scope': 'isolated native host-lock regression; NTDLL phase injected, no Wine execution',
                  'fixture_arch': platform.machine() if args.arch == 'native' else args.arch,
                  'production_sha256': hashlib.sha256(production.read_bytes()).hexdigest(),
                  'fixture_sha256': hashlib.sha256((here / 'fixture.m').read_bytes()).hexdigest(),
                  'seam_sha256': hashlib.sha256((here / 'wineunixlib.h').read_bytes()).hexdigest(),
                  'baseline_ref': args.source_ref,
                  'baseline_sha256': hashlib.sha256(old.encode()).hexdigest(), 'cases': reports}
        (work / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        if args.keep:
            args.keep.mkdir(parents=True, exist_ok=True)
            for name in ['report.json', 'baseline-build.txt', 'candidate-build.txt']:
                shutil.copy2(work / name, args.keep / name)
        print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
