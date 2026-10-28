#!/usr/bin/env python3
"""Build the opt-in native ARM airconv worker from repository sources.

Requires existing ARM LLVM 15 archives/headers and generated Metal helper headers.
Never downloads, installs, rebuilds LLVM, or enables the Wine bridge.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import selectors
import shutil
import signal
import subprocess
import sys
import time

BUILD_CAP = 256 * 1024 * 1024
LOG_CAP = 32 * 1024 * 1024
RECEIPT_CAP = 8 * 1024 * 1024
MIN_FREE = 20 * 1024 ** 3
HEADERS = {'.h', '.hpp', '.inc', '.def'}
HELPERS = ('air_msad.h', 'air_samplepos.h', 'air_tessellation.h')


def sha(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def tool_output(command):
    return subprocess.check_output(command, text=True, timeout=30).strip()


def meson_list(path, name, files=False):
    # Read the existing static Meson declarations, never execute/evaluate them.
    text = re.sub(r'#.*', '', path.read_text())
    opening = r'files\(\[' if files else r'\['
    match = re.search(r'\b' + re.escape(name) + r'\s*=\s*' + opening + r'(.*?)\]', text, re.S)
    if not match:
        raise ValueError('missing static Meson list: ' + name)
    body = match.group(1)
    entries = re.findall(r"'([^']*)'", body)
    if not entries or re.sub(r"'[^']*'|[\s,]", '', body):
        raise ValueError('non-static Meson list requires builder update: ' + name)
    return entries


def bounded(command, output, deadline, seconds, log):
    if time.monotonic() >= deadline:
        raise RuntimeError('30-minute build deadline exceeded')
    if shutil.disk_usage(output).free < MIN_FREE:
        raise RuntimeError('less than 20 GiB free working space')
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               start_new_session=True, env={'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'LANG': 'C'})
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    until = min(deadline, time.monotonic() + seconds)
    try:
        while selector.get_map():
            if time.monotonic() >= until:
                raise RuntimeError('compiler/link deadline exceeded')
            if shutil.disk_usage(output).free < MIN_FREE:
                raise RuntimeError('free working-space boundary crossed')
            if sum(p.stat().st_size for p in output.rglob('*') if p.is_file()) > BUILD_CAP:
                raise RuntimeError('256 MiB output-directory cap exceeded')
            for key, _ in selector.select(.2):
                data = os.read(key.fd, 65536)
                if not data:
                    selector.unregister(key.fileobj)
                    continue
                if log.tell() + len(data) > LOG_CAP:
                    raise RuntimeError('32 MiB compiler-log cap exceeded')
                log.write(data)
                log.flush()
        process.wait(timeout=max(.1, until - time.monotonic()))
        if process.returncode:
            raise subprocess.CalledProcessError(process.returncode, command)
    except BaseException:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()
        raise
    finally:
        selector.close()
        process.stdout.close()
    if sum(p.stat().st_size for p in output.rglob('*') if p.is_file()) > BUILD_CAP:
        raise RuntimeError('256 MiB output-directory cap exceeded')


def prepare(args):
    source = args.source_dir.resolve(strict=True)
    llvm_includes = [p.resolve(strict=True) for p in args.llvm_include]
    llvm_lib = args.llvm_lib_dir.resolve(strict=True)
    helper = args.helper_include.resolve(strict=True)
    directx = (args.directx_include or source / 'include/native/directx').resolve(strict=True)
    for path in [source, llvm_lib, helper, directx] + llvm_includes:
        if not path.is_dir():
            raise ValueError('expected directory: ' + str(path))
    if not (directx / 'd3d11.h').is_file() or not (directx / 'd3d12.h').is_file():
        raise ValueError('DirectX headers missing: initialize the pinned submodule separately or supply --directx-include')
    air_meson = source / 'src/airconv/meson.build'
    parser_meson = source / 'libs/DXBCParser/meson.build'
    air_sources = [source / 'src/airconv' / p for p in meson_list(air_meson, 'airconv_src', True)]
    parser_sources = [source / 'libs/DXBCParser' / p for p in meson_list(parser_meson, 'dxbc_parser_src', True)]
    sources = air_sources + parser_sources + [source / 'src/airconv/airconv_worker_main.cpp']
    if any(p.suffix != '.cpp' or not p.is_file() or not p.resolve().is_relative_to(source) for p in sources):
        raise ValueError('converter source list contains an absent/non-C++/outside file')
    libraries = meson_list(air_meson, 'llvm_deps')
    if any(not re.fullmatch(r'-lLLVM[A-Za-z0-9_]+', p) for p in libraries):
        raise ValueError('unexpected LLVM library declaration')
    archives = [llvm_lib / ('lib' + name[2:] + '.a') for name in libraries]
    for archive in archives:
        if not archive.is_file():
            raise ValueError('missing LLVM archive: ' + str(archive))
        subprocess.run(['xcrun', 'lipo', '-verify_arch', 'arm64', str(archive)],
                       stdout=subprocess.DEVNULL, check=True, timeout=30)
    config = next((p / 'llvm/Config/llvm-config.h' for p in llvm_includes
                   if (p / 'llvm/Config/llvm-config.h').is_file()), None)
    if config is None or not re.search(r'^#define LLVM_VERSION_MAJOR 15\s*$', config.read_text(), re.M):
        raise ValueError('LLVM 15 generated llvm-config.h is required')
    if not any((p / 'llvm/IR/LLVMContext.h').is_file() for p in llvm_includes):
        raise ValueError('LLVM source headers are missing (use a second --llvm-include)')
    for name in HELPERS:
        if not (helper / name).is_file():
            raise ValueError('missing generated helper header: ' + str(helper / name))
    compiler = tool_output(['xcrun', '--find', 'clang++'])
    sdk = tool_output(['xcrun', '--show-sdk-path'])
    sdk_version = tool_output(['xcrun', '--show-sdk-version'])
    version = tool_output([compiler, '--version'])
    flags = ['-std=c++20', '-arch', 'arm64', '-mmacosx-version-min=14.0', '-O1', '-DNDEBUG', '-fblocks',
             '-Wno-return-type', '-Wno-ignored-attributes', '-Wno-unknown-pragmas']
    converter_flags = meson_list(air_meson, 'airconv_args')
    parser_flags = meson_list(parser_meson, 'dxbc_parser_args')
    includes = [source / 'src/airconv', source / 'include', source / 'libs',
                source / 'include/native/windows', directx] + llvm_includes + [helper]
    inputs = {}
    def add(label, path):
        if label in inputs:
            raise ValueError('duplicate identity label: ' + label)
        inputs[label] = {'path': str(path), 'sha256': sha(path)}
    for path in sources + [air_meson, parser_meson,
                          source / 'src/winemetal/unix/airconv_worker_bridge.cpp',
                          source / 'src/winemetal/unix/airconv_worker_bridge.h']:
        add('repository/' + str(path.relative_to(source)), path)
    for root in ('src/airconv', 'include', 'libs/DXBCParser'):
        for path in sorted((source / root).rglob('*')):
            if path.is_relative_to(source / 'include/native/directx'):
                continue  # Hash the selected overlay independently of submodule population.
            if path.is_file() and path.suffix in HEADERS:
                label = 'repository/' + str(path.relative_to(source))
                if label not in inputs:
                    add(label, path)
    for path in sorted(directx.rglob('*')):
        if path.is_file() and path.suffix in HEADERS:
            add('directx-include/' + str(path.relative_to(directx)), path)
    for index, root in enumerate(llvm_includes):
        for path in sorted(root.rglob('*')):
            if path.is_file() and path.suffix in HEADERS:
                add('llvm-include-%d/%s' % (index, path.relative_to(root)), path)
    for path in archives:
        add('llvm-library/' + path.name, path)
    for name in HELPERS:
        add('helper/' + name, helper / name)
    add('builder', Path(__file__).resolve())
    identity = {'schema': 'dxmt-native-arm-worker-build-v2', 'compiler': version,
                'compiler_executable_sha256': sha(Path(compiler).resolve()),
                'sdk_version': sdk_version, 'compiler_environment': {'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'LANG': 'C'}, 'flags': flags, 'converter_flags': converter_flags,
                'parser_flags': parser_flags, 'system_link_flags': ['-lm', '-lz', '-lcurses', '-lxml2'],
                'llvm_link_order': [p.name for p in archives],
                'include_order': ['repository/src/airconv', 'repository/include', 'repository/libs',
                                  'repository/include/native/windows', 'directx-include'] +
                                 ['llvm-include-%d' % i for i in range(len(llvm_includes))] + ['helper'],
                'inputs': {key: value['sha256'] for key, value in inputs.items()}}
    build_id = hashlib.sha256(json.dumps(identity, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    return source, sources, parser_sources, compiler, sdk, flags, converter_flags, parser_flags, includes, archives, inputs, identity, build_id


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--directx-include', type=Path, help='defaults to the initialized repository DirectX submodule')
    parser.add_argument('--llvm-include', type=Path, action='append', required=True,
                        help='repeat in compiler search order: generated include then source include')
    parser.add_argument('--llvm-lib-dir', type=Path, required=True)
    parser.add_argument('--helper-include', type=Path, required=True,
                        help='existing generated air_msad.h, air_samplepos.h, air_tessellation.h')
    parser.add_argument('--output-dir', type=Path, required=True, help='new or empty owned directory')
    parser.add_argument('--check-only', action='store_true', help='validate/hash inputs; write nothing and do not compile')
    args = parser.parse_args()
    (source, sources, parser_sources, compiler, sdk, flags, converter_flags, parser_flags,
     includes, archives, inputs, identity, build_id) = prepare(args)
    summary = {'build_id': build_id, 'source_count': len(sources), 'archive_count': len(archives),
               'bridge_meson_option': '-Dvibewine_arm_worker_build_id=' + build_id,
               'check_only': args.check_only}
    if args.check_only:
        print(json.dumps(summary))
        return
    output = args.output_dir.absolute()
    if output.is_symlink() or (output.exists() and (not output.is_dir() or any(output.iterdir()))):
        raise ValueError('refusing nonempty/symlink output directory: ' + str(output))
    output = output.resolve()
    if output.is_relative_to(source / 'src') or output.is_relative_to(source / 'include'):
        raise ValueError('build outputs must not be placed in source/include trees')
    output.mkdir(parents=True, exist_ok=True)
    start = time.monotonic()
    commands = []
    receipt = {'status': 'building', 'build_id': build_id, 'identity': identity, 'inputs': inputs,
               'source_dir': str(source), 'compiler_path': compiler, 'sdk_path': sdk,
               'commands': commands, 'retention': {'build_days': 7, 'build_byte_cap': BUILD_CAP,
               'log_days': 7, 'log_byte_cap': LOG_CAP, 'receipt_days': 30,
               'receipt_byte_cap': 8 * 1024 * 1024, 'active_hold': 'until owner admits or rejects this candidate',
               'cleanup': 'manual exact owned-output cleanup after process/hold verification'}}
    receipt_path = output / 'build-receipt.json'
    def save():
        payload = json.dumps(receipt, indent=2) + '\n'
        if len(payload.encode()) > RECEIPT_CAP:
            raise RuntimeError('8 MiB receipt cap exceeded')
        receipt_path.write_text(payload)
    save()
    common = [compiler] + flags + ['-isysroot', sdk]
    for directory in includes:
        common += ['-I', str(directory)]
    common += ['-DDXMT_WORKER_BUILD_ID="' + build_id + '"']
    objects = []
    try:
        with (output / 'build.log').open('wb') as log:
            for index, path in enumerate(sources):
                obj = output / ('%02d_%s.o' % (index, path.stem))
                # DXBCParser uses exceptions; retain its actual Meson flags.
                command = common + (parser_flags if path in parser_sources else converter_flags) + ['-c', str(path), '-o', str(obj)]
                commands.append(command)
                bounded(command, output, start + 1800, 240, log)
                objects.append(obj)
            executable = output / 'airconv-worker-arm64'
            command = common + [str(p) for p in objects] + [str(p) for p in archives] + identity['system_link_flags'] + ['-o', str(executable)]
            commands.append(command)
            bounded(command, output, start + 1800, 180, log)
        # Rescan inventories as well as bytes: a newly added higher-precedence
        # shadow header must invalidate admission too. This also revalidates
        # compiler/SDK identity and the existing static Meson declarations.
        checked = prepare(args)
        if checked[-1] != build_id:
            raise RuntimeError('input inventory/content/toolchain changed during build')
        subprocess.run(['xcrun', 'lipo', '-verify_arch', 'arm64', str(executable)], check=True, timeout=30)
        receipt.update(status='built', wall_seconds=time.monotonic() - start,
                       executable={'path': str(executable), 'sha256': sha(executable), 'bytes': executable.stat().st_size})
        (output / 'bridge-build-id.txt').write_text(build_id + '\n')
        save()
        summary.update(executable=receipt['executable'], wall_seconds=receipt['wall_seconds'])
        print(json.dumps(summary))
    except BaseException as error:
        receipt.update(status='failed', failure=str(error), wall_seconds=time.monotonic() - start)
        save()
        raise


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print('native ARM worker build failed: ' + str(error), file=sys.stderr)
        sys.exit(1)
