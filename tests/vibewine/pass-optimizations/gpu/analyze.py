#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Validate bounded parent-captured output; never select or execute a runtime."""
import argparse
import json
from pathlib import Path

CAP = 64 * 1024
MODULES = ['d3d11.dll', 'dxgi.dll', 'winemetal.dll', 'd3dcompiler_47.dll']


def validate(data, exit_code, negative=False):
    if not 0 < len(data) <= CAP:
        raise ValueError('missing/oversized stdout')
    lines = data.decode('utf-8', errors='strict').splitlines()
    if not 5 <= len(lines) <= 9 or any(len(line.encode()) > 8192 for line in lines):
        raise ValueError('record count/line cap')
    modules = {}
    for expected, line in zip(MODULES, lines[:4]):
        parts = line.split(' ', 2)
        if len(parts) != 3 or parts[:2] != ['MODULE', expected] or not parts[2]:
            raise ValueError('missing/out-of-order module identity')
        modules[expected] = parts[2]
    if negative:
        if exit_code != 20 or lines[4:] != ['NEGATIVE_REJECTED pixel']:
            raise ValueError('negative must naturally reject the intended image golden')
        return {'status': 'negative_rejected', 'modules': modules}
    expected = [f'CASE_PASS mode={mode}' for mode in range(4)] + ['PASS cases=4 images=5']
    if exit_code != 0 or lines[4:] != expected:
        raise ValueError('normal run requires ordered cases, final summary and natural exit zero')
    return {'status': 'pass', 'cases': 4, 'images': 5, 'modules': modules}


def read(path):
    with path.open('rb') as stream:
        return stream.read(CAP + 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--baseline-exit', type=int, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--candidate-exit', type=int, required=True)
    parser.add_argument('--negative', type=Path, required=True)
    parser.add_argument('--negative-exit', type=int, required=True)
    args = parser.parse_args()
    try:
        results = {'baseline': validate(read(args.baseline), args.baseline_exit),
                   'candidate': validate(read(args.candidate), args.candidate_exit),
                   'negative': validate(read(args.negative), args.negative_exit, True)}
    except (OSError, UnicodeError, ValueError) as error:
        parser.exit(1, f'FAIL {error}\n')
    print(json.dumps({'scope': 'captured image-guest output only; parent must separately verify loaded PE/native hashes',
                      'results': results}, indent=2))


if __name__ == '__main__':
    main()
