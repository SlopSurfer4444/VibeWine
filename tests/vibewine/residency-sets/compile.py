#!/usr/bin/env python3
"""Check actual queue/context sources using retained Meson compiler flags; no output objects."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compile-db', type=Path, required=True)
    parser.add_argument('--translation-unit', choices=('queue','context'))
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[3]
    db = json.loads(args.compile_db.read_text())
    paths = ['src/dxmt/dxmt_command_queue.cpp', 'src/dxmt/dxmt_context.cpp']
    if args.translation_unit:
        paths = [paths[args.translation_unit == 'context']]
    checks = []
    for path in paths:
        item = next(row for row in db if row['file'].endswith(path))
        old_root = item['file'][:-len(path)].rstrip('/')
        command = item.get('arguments') or shlex.split(item['command'])
        flags, skip = [], False
        for flag in command:
            if skip:
                skip = False
                continue
            if flag in ('-MD', '-MMD', '-c'):
                continue
            if flag in ('-MQ', '-MT', '-MF', '-o'):
                skip = True
                continue
            if flag.startswith(('-DDXMT_RESIDENCY_SETS=', '-DDXMT_IDLE_RETIREMENT=')):
                continue
            flags.append(flag.replace(old_root, str(source)) if old_root else flag)
        for residency, idle in ((0,0), (1,0), (0,1), (1,1)):
            cmd = flags + [f'-DDXMT_RESIDENCY_SETS={residency}', f'-DDXMT_IDLE_RETIREMENT={idle}', '-fsyntax-only']
            result = subprocess.run(cmd, cwd=item['directory'], text=True, capture_output=True, timeout=60)
            if result.stdout or result.stderr:
                print(result.stdout + result.stderr, end='')
            result.check_returncode()
            checks.append({'path':path, 'residency':residency, 'idle':idle})
            print(f'PASS syntax {path} residency={residency} idle={idle}', flush=True)
    print(json.dumps({'checks':checks, 'compile_db_sha256':hashlib.sha256(args.compile_db.read_bytes()).hexdigest(),
                      'source_sha256':{p:hashlib.sha256((source/p).read_bytes()).hexdigest() for p in paths},
                      'scope':'actual production syntax; no link, runtime, native GPU or installed changes'}, sort_keys=True))


if __name__ == '__main__':
    main()
