#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compile exact query/API classes, Rc and flush selection against CPU buffer seams."""
import argparse
import hashlib
import json
import os
import platform
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def extract_class(text, name):
    match = re.search(r'^class ' + re.escape(name) + r'\b', text, re.M)
    if not match:
        raise ValueError('class missing: ' + name)
    start = match.start()
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    if text[end] != ';':
        raise ValueError('unexpected class terminator')
    return '#line %d "%s"\n%s\n' % (text[:start].count('\n') + 1,
                                             name + '-production', text[start:end + 1])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-ref')
    parser.add_argument('--sanitize', choices=['none', 'address', 'thread'], default='none')
    parser.add_argument('--expect-races', action='store_true')
    parser.add_argument('--expect-empty-baseline', action='store_true')
    parser.add_argument('--keep', type=Path, help='Preserve bounded generated source, logs and receipt here')
    parser.add_argument('--symbolizer', type=Path, help='LLVM symbolizer for source-attributed TSAN reports')
    args = parser.parse_args()
    if args.expect_races and (not args.source_ref or args.sanitize != 'thread'):
        parser.error('--expect-races requires --source-ref and --sanitize thread')
    if args.expect_empty_baseline and not args.source_ref:
        parser.error('--expect-empty-baseline requires --source-ref')
    tests = Path(__file__).resolve().parent
    source = tests.parents[2]
    paths = ['src/dxmt/dxmt_occlusion_query.hpp', 'src/dxmt/dxmt_context.cpp',
             'src/d3d11/d3d11_query.cpp']
    contents = {p: subprocess.check_output(['git', 'show', args.source_ref + ':' + p], cwd=source, text=True)
                if args.source_ref else (source / p).read_text() for p in paths}
    compiler = shutil.which(os.environ.get('CXX', 'clang++'))
    if not compiler:
        parser.error('C++ compiler unavailable')
    with tempfile.TemporaryDirectory(prefix='dxmt-query-') as directory:
        work = Path(directory)
        header = contents[paths[0]]
        classes = ['VisibilityResultOffsetBumpState', 'VisibilityResultQuery', 'VisibilityResultReadback', 'TimestampQuery']
        (work / 'query-under-test.hpp').write_text(''.join(extract_class(header, name) for name in classes))
        api = contents[paths[2]]
        (work / 'query-api-under-test.hpp').write_text(''.join(extract_class(api, name) for name in ['OcclusionQuery', 'MTLD3D11TimestampQueryImpl']))
        context = contents[paths[1]]
        start = context.index('  QueryReadbacks readbacks{};')
        end = context.index('  readbacks.timestamp =', start)
        (work / 'flush-under-test.hpp').write_text(context[start:end])
        command = [compiler, '-std=c++20', '-O0' if args.sanitize == 'thread' else '-O1', '-g', '-Wall', '-Wextra', '-pthread',
                   '-I', str(source / 'src/util'), '-I', str(work), str(tests / 'fixture.cpp'), '-o', str(work / 'fixture')]
        if args.sanitize != 'none':
            command += ['-fsanitize=' + ('address,undefined' if args.sanitize == 'address' else 'thread'),
                        '-fno-omit-frame-pointer']
        subprocess.run(command, check=True, timeout=60)
        tsan_options = 'exitcode=66:halt_on_error=0:abort_on_error=0'
        if args.symbolizer:
            tsan_options += ':external_symbolizer_path=' + str(args.symbolizer.resolve())
        result = subprocess.run([str(work / 'fixture')] + (['race'] if args.expect_races else ['empty'] if args.expect_empty_baseline else []),
                                capture_output=True, text=True, timeout=30,
                                env={**os.environ, 'TSAN_OPTIONS': tsan_options,
                                     'ASAN_OPTIONS': 'detect_leaks=' + ('0' if platform.system() == 'Darwin' else '1'),
                                     'UBSAN_OPTIONS': 'halt_on_error=1'})
        (work / 'stdout.txt').write_text(result.stdout)
        (work / 'stderr.txt').write_text(result.stderr)
        report = {'source': str(source), 'ref': args.source_ref, 'sanitize': args.sanitize,
                  'status': result.returncode, 'source_sha256': {p: hashlib.sha256(c.encode()).hexdigest() for p, c in contents.items()}}
        (work / 'receipt.json').write_text(json.dumps(report, indent=2) + '\n')
        if args.keep:
            args.keep.mkdir(parents=True, exist_ok=True)
            for path in work.iterdir():
                if path.is_file() and path.name != 'fixture':
                    shutil.copy2(path, args.keep / path.name)
        if args.expect_races:
            reports = result.stderr.count('WARNING: ThreadSanitizer: data race')
            if result.returncode != 66 or reports != 3 or result.stdout != 'publication race exercised\nPASS query publication race\n':
                raise SystemExit('unexpected baseline race outcome:\n' + result.stdout + result.stderr)
            summaries = [line for line in result.stderr.splitlines() if line.startswith('SUMMARY: ThreadSanitizer:')]
            if (len(summaries) != 3 or sum('TimestampQuery-production' in line for line in summaries) != 1
                    or sum('VisibilityResultQuery-production' in line for line in summaries) != 2):
                raise SystemExit('race report did not identify both production classes')
            if re.search(r'AddressSanitizer|runtime error:|Assertion failed|WARNING: (?!ThreadSanitizer: data race)', result.stderr):
                raise SystemExit('unexpected additional baseline diagnostic:\n' + result.stderr)
            print('PASS baseline detects three production publication races')
        elif args.expect_empty_baseline:
            expected = {'empty trailing completion retained', 'all empty completion retained',
                        'empty trailing completion ready', 'all empty completion ready'}
            actual = {line.removeprefix('FAIL ') for line in result.stderr.splitlines() if line.startswith('FAIL ')}
            if result.returncode != 1 or actual != expected or any(not line.startswith('FAIL ') for line in result.stderr.splitlines()):
                raise SystemExit('unexpected empty baseline outcome:\n' + result.stdout + result.stderr)
            print('PASS baseline detects four empty-chunk completion failures')
        elif result.returncode or result.stderr or result.stdout != 'publication race exercised\nPASS query publication scenarios\n':
            raise SystemExit('unexpected candidate outcome:\n' + result.stdout + result.stderr)
        else:
            print(result.stdout, end='')
        print(json.dumps(report, sort_keys=True))


if __name__ == '__main__':
    main()
