#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Build offscreen actual-present-shader pixel oracle. Never execute GPU programs."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path, help='new absolute build directory')
    parser.add_argument('--library', type=Path, help='existing actual production metallib; otherwise compile current source')
    args = parser.parse_args()
    if not args.output.is_absolute() or args.output.exists() or args.output.is_symlink():
        parser.error('output must be a new absolute directory')
    if args.library and (not args.library.is_absolute() or not args.library.is_file() or args.library.stat().st_size > 16 * 1024 * 1024):
        parser.error('library must be an existing absolute file of at most 16 MiB')
    spec = importlib.util.spec_from_file_location('bounded_gpu_builder', HERE.parents[1] / 'gpu-wave/build.py')
    bounded = importlib.util.module_from_spec(spec); spec.loader.exec_module(bounded)
    args.output = args.output.parent.resolve(strict=True) / args.output.name
    args.output.mkdir(mode=0o700)
    for source in [HERE / 'fixture.mm', ROOT / 'src/dxmt/dxmt_command.metal', ROOT / 'src/dxmt/dxmt_command_constants.hpp']:
        if source.stat().st_size > 512 * 1024:
            raise ValueError('source exceeds 512 KiB cap')
        shutil.copyfile(source, args.output / source.name)
    receipt = {'runtimeExecuted': False, 'builderSha256': sha(Path(__file__)), 'sourcesSha256': {
        name: sha(args.output / name) for name in ['fixture.mm', 'dxmt_command.metal', 'dxmt_command_constants.hpp']}, 'commands': []}
    def compile(name, argv, product):
        result = bounded.bounded_command(argv, 60)
        (args.output / (name + '.log')).write_bytes(result.pop('output'))
        item = {'name': name, 'argv': argv, **result}
        if result['returncode'] == 0:
            if not product.is_file() or product.stat().st_size > 16 * 1024 * 1024:
                item['returncode'] = 1; item['error'] = 'product missing or exceeds 16 MiB'
            else:
                item['sha256'] = sha(product)
        receipt['commands'].append(item)
        return item['returncode'] == 0 and not item['timedOut'] and not item['outputTruncated']
    library = args.output / 'dxmt_command.metallib'
    ok = True
    if args.library:
        shutil.copyfile(args.library, library)
        receipt['suppliedLibrary'] = {'path': str(args.library), 'sha256': sha(library)}
    else:
        air = args.output / 'dxmt_command.air'
        ok = compile('metal', ['xcrun', '-sdk', 'macosx', 'metal', '-c', str(args.output / 'dxmt_command.metal'),
                              '-fmodules-cache-path=' + str(args.output / 'metal-module-cache'), '-o', str(air)], air)
        if ok:
            ok = compile('metallib', ['xcrun', '-sdk', 'macosx', 'metallib', '-o', str(library), str(air)], library)
    if ok:
        ok = compile('fixture', ['xcrun', 'clang++', '-std=c++20', '-O2', '-fobjc-arc', '-fblocks',
                                 '-Wall', '-Wextra', '-Werror', str(args.output / 'fixture.mm'),
                                 '-framework', 'Foundation', '-framework', 'Metal', '-o', str(args.output / 'fixture')], args.output / 'fixture')
    receipt['librarySha256'] = sha(library) if library.is_file() else None
    receipt['bundleBytes'] = sum(path.stat().st_size for path in args.output.rglob('*') if path.is_file())
    if receipt['bundleBytes'] > 64 * 1024 * 1024:
        ok = False; receipt['error'] = 'build bundle exceeds 64 MiB cap; owner must review exact generated directory'
    receipt['status'] = 'compiled_only' if ok else 'failed'
    data = json.dumps(receipt, indent=2) + '\n'
    if len(data.encode()) > 32 * 1024: raise ValueError('receipt exceeds 32 KiB')
    (args.output / 'build.json').write_text(data)
    print(json.dumps({'status': receipt['status'], 'receipt': str(args.output / 'build.json')}))
    return 0 if ok else 1

if __name__ == '__main__':
    raise SystemExit(main())
