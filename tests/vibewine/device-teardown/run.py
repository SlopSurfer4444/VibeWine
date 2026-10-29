#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise the actual COM/private-data/capture chain reaching final release."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile


def class_source(text, name):
    import re
    match = re.search(r'^(?:template\s*<[^\n]+>\s*)?class ' + name + r'\b', text, re.M)
    if not match:
        raise ValueError('missing production class ' + name)
    start = match.start()
    return text[start:balanced_end(text, text.index('{', start)) + 1]


def balanced_end(text, brace):
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return end


def method(text, marker, prefix=''):
    start = text.index(marker)
    return prefix + text[start:balanced_end(text, text.index('{', start))] + '\n'


def without_includes(text):
    return '\n'.join(line for line in text.splitlines() if not line.startswith('#include') and line != '#pragma once') + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-ref')
    parser.add_argument('--arch', choices=['arm64', 'x86_64'], help='Explicit macOS fixture target; default is the host target')
    parser.add_argument('--mutation', choices=['skip-drain', 'discard-before-drain', 'wait-unsubmitted', 'skip-discard'], help='Reject one corrupted temporary shutdown body')
    parser.add_argument('--sanitize', choices=['none', 'address', 'thread'], default='address')
    parser.add_argument('--keep', type=Path)
    parser.add_argument('--suite', choices=['ownership', 'shutdown', 'retirement', 'integrated', 'all'], default='all')
    parser.add_argument('--expect-bypass', action='store_true', help='Restore the old direct-delete body as a negative control')
    args = parser.parse_args()
    if args.arch and platform.system() != 'Darwin':
        parser.error('--arch requires the macOS compiler/runner')
    if args.mutation and (args.suite != 'shutdown' or args.expect_bypass):
        parser.error('--mutation requires --suite shutdown and cannot combine with --expect-bypass')
    tests = Path(__file__).resolve().parent
    source = tests.parents[2]
    files = ['src/util/com/com_object.hpp', 'src/util/com/com_pointer.hpp',
             'src/util/com/com_private_data.hpp', 'src/util/com/com_private_data.cpp',
             'src/d3d11/d3d11_device_child.hpp', 'src/dxmt/dxmt_command_list.hpp',
             'src/d3d11/d3d11_context_imm.cpp', 'src/d3d11/d3d11_context_impl.cpp',
             'src/dxmt/dxmt_command_queue.cpp', 'src/dxmt/dxmt_command_queue.hpp']
    if args.suite in ['retirement', 'integrated', 'all']:
        files += ['src/util/device_retirement.hpp', 'src/d3d11/d3d11_device.cpp', 'src/util/util_error.hpp']
    files += ['src/util/util_cpu_fence.hpp']
    contents = {name: subprocess.check_output(['git', 'show', args.source_ref + ':' + name], cwd=source, text=True)
                if args.source_ref else (source / name).read_text() for name in files}
    with tempfile.TemporaryDirectory(prefix='dxmt-teardown-') as directory:
        work = Path(directory)
        common = ''.join(without_includes(contents[name]) for name in files[:4])
        if args.expect_bypass:
            if args.suite != 'retirement' or common.count('      Destroy();') != 1:
                parser.error('--expect-bypass requires retirement suite and the single destruction hook')
            common = common.replace('      Destroy();', '      delete this;')
        (work / 'com-under-test.hpp').write_text(common)
        (work / 'device-child-under-test.hpp').write_text(without_includes(contents[files[4]]))
        (work / 'command-list-under-test.hpp').write_text(without_includes(contents[files[5]]))
        immediate = contents[files[6]]
        refs = method(immediate, 'AddRef() override', 'ULONG STDMETHODCALLTYPE\n')
        refs += method(immediate, 'Release() override', 'ULONG STDMETHODCALLTYPE\n')
        (work / 'context-refs-under-test.hpp').write_text(refs)
        command_class = class_source(contents[files[7]], 'MTLD3D11CommandList')
        recycle = method(command_class, 'Recycle() final', 'void ')
        recycle += method(command_class, 'Reset() {', 'void\n')
        (work / 'recycle-under-test.hpp').write_text(recycle)
        start = command_class.index('  bool promote_flush = false;')
        end = command_class.index('\nprivate:', start)
        (work / 'retained-fields-under-test.hpp').write_text(command_class[start:end])
        start = immediate.index('    EmitOP([cmdlist = std::move(cmdlist), query_list = std::move(query_list)]')
        end = immediate.index('\n    });', start) + len('\n    });')
        (work / 'execute-capture-under-test.hpp').write_text(immediate[start:end])
        if args.suite != 'ownership':
            queue = contents[files[8]]
            shutdown = method(queue, 'CommandQueue::~CommandQueue()')
            shutdown += method(queue, 'CommandQueue::ShutdownAndJoin()', 'void\n')
            terminal_shutdown = 'wait_state_->cancel();' in shutdown
            (work / 'shutdown-protocol.hpp').write_text('#define TEST_TERMINAL_SHUTDOWN ' + str(int(terminal_shutdown)) + '\n')
            (work / 'cpu-fence-under-test.hpp').write_text(contents['src/util/util_cpu_fence.hpp'])
            if args.mutation:
                if not terminal_shutdown:
                    parser.error('--mutation requires the terminal queue shutdown source')
                old, new = {
                    'skip-drain': ('      chunk.attached_cmdbuf.waitUntilCompleted();', '      (void)chunk;'),
                    'discard-before-drain': ('  argument_encoding_ctx.discardCommands();', ''),
                    'wait-unsubmitted': ('chunk.submitted && chunk.attached_cmdbuf', 'chunk.attached_cmdbuf'),
                    'skip-discard': ('  argument_encoding_ctx.discardCommands();', ''),
                }[args.mutation]
                if shutdown.count(old) != 1:
                    raise ValueError('shutdown mutation seam changed: ' + args.mutation)
                shutdown = shutdown.replace(old, new)
                if args.mutation == 'discard-before-drain':
                    marker = '  for (auto &chunk : chunks) {'
                    if shutdown.count(marker) != 1:
                        raise ValueError('shutdown drain-loop seam changed')
                    shutdown = shutdown.replace(marker, '  argument_encoding_ctx.discardCommands();\n' + marker)
            (work / 'shutdown-under-test.hpp').write_text(shutdown)
        if args.suite in ['retirement', 'integrated', 'all']:
            helper = contents[files[10]]
            (work / 'retirement-under-test.hpp').write_text(without_includes(contents[files[12]]) + without_includes(helper))
            outer = class_source(contents[files[11]], 'MTLD3D11DXGIDevice')
            owner = method(outer, 'Destroy() override', 'void ')
            owner += method(outer, 'Retire(void *object) noexcept', 'static void ')
            (work / 'retirement-owner-methods.hpp').write_text(owner)
        command = [shutil.which(os.environ.get('CXX', 'clang++')), '-std=c++20', '-O1', '-g',
                   '-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-unused-variable', '-pthread',
                   '-I', str(work), str(tests / 'fixture.cpp'), '-o', str(work / 'fixture')]
        if args.arch:
            command += ['-arch', args.arch]
        if args.sanitize != 'none':
            command += ['-fsanitize=' + ('address,undefined' if args.sanitize == 'address' else 'thread'), '-fno-omit-frame-pointer']
        suites = ['ownership', 'shutdown', 'retirement', 'integrated'] if args.suite == 'all' else [args.suite]
        reports = []
        for suite in suites:
            build_command = list(command)
            if suite == 'shutdown':
                build_command[build_command.index(str(tests / 'fixture.cpp'))] = str(tests / 'shutdown_fixture.cpp')
            elif suite == 'retirement':
                build_command[build_command.index(str(tests / 'fixture.cpp'))] = str(tests / 'retirement_fixture.cpp')
            elif suite == 'integrated':
                build_command += ['-DTEST_RETIREMENT']
            subprocess.run(build_command, check=True, timeout=60)
            execute = [str(work / 'fixture')]
            if args.arch == 'x86_64' and platform.machine() == 'arm64':
                execute = ['/usr/bin/arch', '-x86_64'] + execute
            result = subprocess.run(execute, capture_output=True, text=True, timeout=15,
                                env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=' + ('0' if platform.system() == 'Darwin' else '1'),
                                     'UBSAN_OPTIONS': 'halt_on_error=1', 'TSAN_OPTIONS': 'exitcode=66:abort_on_error=0'})
            expected = ('PASS source ownership: API final release and finish-thread final release\n' if suite == 'ownership'
                        else 'PASS queue shutdown: repeated, concurrent, pending worker, destructor and capture ownership\n' if suite == 'shutdown'
                        else 'PASS retirement: origins, private hook, pending drain, setup failures and module pin\n' if suite == 'retirement'
                        else 'PASS integrated retirement: actual private-data capture chain\n')
            if args.mutation:
                failure = {
                    'skip-drain': 'discard-before-native-terminal',
                    'discard-before-drain': 'discard-before-native-terminal',
                    'wait-unsubmitted': 'waited-unsubmitted-buffer',
                    'skip-discard': 'discard-before-capture',
                }[args.mutation]
                if result.returncode != 1 or result.stdout or result.stderr != 'FAIL shutdown ' + failure + '\n':
                    raise SystemExit('unexpected shutdown mutation outcome:\n' + result.stdout + result.stderr)
                print('PASS rejected shutdown mutation ' + args.mutation)
            elif args.expect_bypass:
                if result.returncode != 1 or result.stdout or result.stderr != 'FAIL deferred destruction required\n':
                    raise SystemExit('unexpected destruction-hook bypass outcome:\n' + result.stdout + result.stderr)
                print('PASS old direct-delete body fails deferred-destruction requirement')
            elif result.returncode or result.stderr or result.stdout != expected:
                raise SystemExit('unexpected ' + suite + ' outcome:\n' + result.stdout + result.stderr)
            reports.append({'suite': suite, 'status': result.returncode})
            (work / (suite + '-stdout.txt')).write_text(result.stdout)
            (work / (suite + '-stderr.txt')).write_text(result.stderr)
            print(result.stdout, end='')
        report = {'source': str(source), 'ref': args.source_ref, 'results': reports, 'sanitize': args.sanitize,
                  'bypass_negative_control': args.expect_bypass, 'mutation': args.mutation,
                  'arch': args.arch, 'host_arch': platform.machine(),
                  'shutdown_protocol': ('terminal-cancel/drain/discard' if terminal_shutdown else 'legacy-ready-fences') if args.suite != 'ownership' else None,
                  'fixture_sha256': {name: hashlib.sha256((tests / name).read_bytes()).hexdigest() for name in ['fixture.cpp', 'shutdown_fixture.cpp', 'retirement_fixture.cpp', 'mock_threadpool.hpp', 'run.py']},
                  'generated_sha256': {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in work.iterdir() if path.suffix == '.hpp'},
                  'source_sha256': {name: hashlib.sha256(text.encode()).hexdigest() for name, text in contents.items()}}
        (work / 'receipt.json').write_text(json.dumps(report, indent=2) + '\n')
        if args.keep:
            args.keep.mkdir(parents=True, exist_ok=True)
            for path in work.iterdir():
                if path.is_file() and path.name != 'fixture':
                    shutil.copy2(path, args.keep / path.name)
        print(json.dumps(report, sort_keys=True))


if __name__ == '__main__':
    main()
