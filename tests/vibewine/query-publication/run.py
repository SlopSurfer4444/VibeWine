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


def extract_method(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return '#line %d "immediate-GetData-production"\n%s\n' % (text[:start].count('\n') + 1, text[start:end])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-ref')
    parser.add_argument('--expect-frontier-baseline', action='store_true')
    parser.add_argument('--arch', choices=['arm64', 'x86_64'])
    parser.add_argument('--negative', choices=['reuse-generation', 'drop-empty-completion'])
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
    if args.expect_frontier_baseline and not args.source_ref:
        parser.error('--expect-frontier-baseline requires --source-ref')
    if sum(bool(x) for x in [args.expect_races, args.expect_empty_baseline, args.expect_frontier_baseline, args.negative]) > 1:
        parser.error('select only one expected-failure mode')
    tests = Path(__file__).resolve().parent
    source = tests.parents[2]
    paths = ['src/dxmt/dxmt_occlusion_query.hpp', 'src/dxmt/dxmt_context.cpp',
             'src/d3d11/d3d11_query.cpp', 'src/d3d11/d3d11_context_imm.cpp', 'src/d3d11/d3d11_query.hpp']
    contents = {p: subprocess.check_output(['git', 'show', args.source_ref + ':' + p], cwd=source, text=True)
                if args.source_ref else (source / p).read_text() for p in paths}
    compiler = shutil.which(os.environ.get('CXX', 'clang++'))
    if not compiler:
        parser.error('C++ compiler unavailable')
    with tempfile.TemporaryDirectory(prefix='dxmt-query-') as directory:
        work = Path(directory)
        header = contents[paths[0]]
        classes = ['VisibilityResultOffsetBumpState', 'VisibilityResultQuery', 'VisibilityResultReadback', 'TimestampQuery']
        if args.negative == 'drop-empty-completion':
            marker = 'count || !pending_queries_.empty()'
            if contents[paths[1]].count(marker) != 1:
                raise ValueError('empty completion negative seam missing')
            contents[paths[1]] = contents[paths[1]].replace(marker, 'count')
        extracted = ''.join(extract_class(header, name) for name in classes)
        # Class-local allocation hooks count actual production wrapper new/delete;
        # production fields, methods, locking and Rc remain unchanged.
        for name in ['VisibilityResultQuery', 'TimestampQuery']:
            marker = 'class ' + name + ' {\npublic:'
            hooks = ('\n  static void *operator new(size_t n) { if(fail_query_wrapper) throw std::bad_alloc{}; WrapperCounts<' + name + '>::allocations++; return ::operator new(n); }'
                     '\n  static void operator delete(void *p) noexcept { WrapperCounts<' + name + '>::deletions++; ::operator delete(p); }')
            if marker not in extracted:
                raise ValueError('wrapper hook seam missing: ' + name)
            extracted = extracted.replace(marker, marker + hooks)
        (work / 'query-under-test.hpp').write_text(extracted)
        api = contents[paths[2]]
        if args.negative == 'reuse-generation':
            for name in ['VisibilityResultQuery', 'TimestampQuery']:
                marker = '        query_ = new ' + name + '();'
                if api.count(marker) != 1:
                    raise ValueError('first-success rotation negative seam missing: ' + name)
                api = api.replace(marker, '        (void)0; // invalid completed-generation reuse')
        (work / 'event-under-test.hpp').write_text('template <typename DataType>\n' + extract_class(contents[paths[4]], 'MTLD3D11EventQueryImpl'))
        (work / 'immediate-under-test.hpp').write_text(extract_method(contents[paths[3]], '  HRESULT\n  STDMETHODCALLTYPE\n  GetData(ID3D11Asynchronous'))
        (work / 'query-issue-under-test.hpp').write_text(extract_method(contents[paths[3]], '  void\n  STDMETHODCALLTYPE\n  Begin(ID3D11Asynchronous') + '\n' + extract_method(contents[paths[3]], '  void\n  STDMETHODCALLTYPE\n  End(ID3D11Asynchronous'))
        (work / 'query-api-under-test.hpp').write_text(''.join(extract_class(api, name) for name in ['OcclusionQuery', 'MTLD3D11TimestampQueryImpl']))
        context = contents[paths[1]]
        start = context.index('  QueryReadbacks readbacks{};')
        end = context.index('  readbacks.timestamp =', start)
        (work / 'flush-under-test.hpp').write_text(context[start:end])
        command = [compiler, '-std=c++20', '-O0' if args.sanitize == 'thread' else '-O1', '-g', '-Wall', '-Wextra', '-pthread',
                   '-DHAS_QUEUE_FAILURE=' + ('1' if 'cmd_queue.FailureReason()' in contents[paths[3]] else '0'), '-I', str(source / 'src/util'), '-I', str(work), str(tests / 'fixture.cpp'), '-o', str(work / 'fixture')]
        if args.arch:
            command += ['-arch', args.arch]
        if args.sanitize != 'none':
            command += ['-fsanitize=' + ('address,undefined' if args.sanitize == 'address' else 'thread'),
                        '-fno-omit-frame-pointer']
        subprocess.run(command, check=True, timeout=60)
        tsan_options = 'exitcode=66:halt_on_error=0:abort_on_error=0'
        if args.symbolizer:
            tsan_options += ':external_symbolizer_path=' + str(args.symbolizer.resolve())
        result = subprocess.run([str(work / 'fixture')] + (['race'] if args.expect_races else ['empty'] if args.expect_empty_baseline or args.negative == 'drop-empty-completion' else ['frontier'] if args.expect_frontier_baseline or args.negative else []),
                                capture_output=True, text=True, timeout=30,
                                env={**os.environ, 'TSAN_OPTIONS': tsan_options,
                                     'ASAN_OPTIONS': 'detect_leaks=' + ('0' if platform.system() == 'Darwin' else '1'),
                                     'UBSAN_OPTIONS': 'halt_on_error=1'})
        (work / 'stdout.txt').write_text(result.stdout)
        (work / 'stderr.txt').write_text(result.stderr)
        report = {'source': str(source), 'ref': args.source_ref, 'sanitize': args.sanitize,
                  'arch': args.arch, 'host_arch': platform.machine(), 'binary': subprocess.check_output(['file', str(work / 'fixture')], text=True).strip().split(': ', 1)[-1], 'negative': args.negative, 'status': result.returncode, 'source_sha256': {p: hashlib.sha256(c.encode()).hexdigest() for p, c in contents.items()},
                  'harness_sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in [tests / 'fixture.cpp', tests / 'run.py']},
                  'generated_sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in work.glob('*.hpp')},
                  'compile': [arg.replace(str(work), '<scratch>') for arg in command]}
        (work / 'receipt.json').write_text(json.dumps(report, indent=2) + '\n')
        if args.keep:
            args.keep.mkdir(parents=True, exist_ok=True)
            for path in work.iterdir():
                if path.is_file() and path.name != 'fixture':
                    shutil.copy2(path, args.keep / path.name)
        if args.expect_frontier_baseline:
            expected = {'zero-size destination unchanged ' + str(i) for i in range(6)}
            expected |= {'zero-size pending destination unchanged ' + str(i) for i in [0, 4, 5]}
            expected |= {'cached visibility allocation bounded', 'cached timestamp allocation bounded',
                         'initial cached reads allocate no wrappers', 'first completion rotates once per generation'}
            actual = {line.removeprefix('FAIL ') for line in result.stderr.splitlines() if line.startswith('FAIL ')}
            if result.returncode != 1 or actual != expected or any(not line.startswith('FAIL ') for line in result.stderr.splitlines()):
                raise SystemExit('unexpected frontier baseline outcome:\n' + result.stdout + result.stderr)
            print('PASS baseline detects status-only writes and repeated-ready allocations')
        elif args.negative:
            expected = ({'first completion rotates once per generation', 'completed backend generation preserved',
                         'completed timestamp generation preserved', 'old timestamp completion cannot publish reissue',
                         'empty retained readback generation preserved'} if args.negative == 'reuse-generation'
                        else {'empty trailing completion retained', 'all empty completion retained',
                              'empty trailing completion ready', 'all empty completion ready'})
            actual = {line.removeprefix('FAIL ') for line in result.stderr.splitlines() if line.startswith('FAIL ')}
            if result.returncode != 1 or actual != expected or any(not line.startswith('FAIL ') for line in result.stderr.splitlines()):
                raise SystemExit('negative control did not fail as required:\n' + result.stdout + result.stderr)
            print('PASS negative control detected ' + args.negative)
        elif args.expect_races:
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
        elif result.returncode or result.stderr or not result.stdout.endswith('PASS query publication scenarios\n'):
            raise SystemExit('unexpected candidate outcome:\n' + result.stdout + result.stderr)
        else:
            print(result.stdout, end='')
        print(json.dumps(report, sort_keys=True))


if __name__ == '__main__':
    main()
