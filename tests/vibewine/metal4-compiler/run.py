#!/usr/bin/env python3
"""Exact native bridge + real Metal GPU parity. No game/install/cache mutation."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--probe-depth', action='store_true')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    paths = [ROOT / 'src/winemetal/unix/winemetal_unix.c',
             ROOT / 'src/winemetal/unix/next_metal4_compiler.inc',
             ROOT / 'src/winemetal/winemetal.h', ROOT / 'src/winemetal/winemetal_thunks.h',
             HERE / 'fixture.mm.in', Path(__file__).resolve()]
    snapshots = {str(path): path.read_bytes() for path in paths}
    native = snapshots[str(paths[0])].decode()
    names = ['_MTLLibrary_newFunction', '_MTLLibrary_newFunctionWithConstants',
             '_MTLDevice_newRenderPipelineState', '_MTLDevice_newComputePipelineState']
    functions = []
    for name in names:
        matches = re.findall(r'^static NTSTATUS\n' + name + r'\([^\n]*\) \{.*?^\}',
                             native, re.MULTILINE | re.DOTALL)
        assert len(matches) == 1, name
        functions.append(matches[0])
    template = snapshots[str(HERE / 'fixture.mm.in')].decode()
    assert template.count('// EXACT_NATIVE_FUNCTIONS') == 1
    source = template.replace('// EXACT_NATIVE_FUNCTIONS', '\n\n'.join(functions))
    commands = []

    def run(label, command, expected=0, env=None):
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                timeout=60, env=env)
        for suffix, output in [('stdout', result.stdout), ('stderr', result.stderr)]:
            assert len(output) <= 1024 * 1024, 'bounded child output exceeded'
            (args.output / (label + '.' + suffix)).write_bytes(output)
        commands.append({'label': label, 'argv': command, 'exit': result.returncode,
                         'stdout_sha256': digest(result.stdout), 'stderr_sha256': digest(result.stderr)})
        assert result.returncode == expected, (label, result.returncode,
                                               result.stdout.decode(errors='replace'),
                                               result.stderr.decode(errors='replace'))
        return result

    with tempfile.TemporaryDirectory(prefix='dxmt-metal4-') as temporary:
        work = Path(temporary)
        fixture = work / 'fixture.m'
        fixture.write_text(source)
        (work / 'next_metal4_compiler.inc').write_bytes(snapshots[str(paths[1])])
        (work / 'winemetal.h').write_bytes(snapshots[str(paths[2])])
        (work / 'winemetal_thunks.h').write_bytes(snapshots[str(paths[3])])
        common = ['xcrun', 'clang', '-x', 'objective-c', '-std=c2x', '-fno-objc-arc', '-fblocks',
                  '-O1', '-g', '-Wall', '-Wextra', '-Wno-unused-function',
                  '-Werror=unguarded-availability', '-mmacosx-version-min=14.0',
                  '-DDXMT_NO_PRIVATE_API', '-I', str(work), '-framework', 'Foundation', '-framework', 'Metal']
        binary = work / 'fixture'
        run('build', common + ['-DDXMT_METAL4_COMPILER=1', str(fixture), '-o', str(binary)])
        enabled = {**os.environ, 'DXMT_METAL4_COMPILER': '1'}
        run('gpu', [str(binary)] + (['--probe-depth'] if args.probe_depth else []), env=enabled)
        include = work / 'next_metal4_compiler.inc'
        original_include = snapshots[str(paths[1])].decode()
        old = 'dst.blendingState = src.blendingEnabled ? MTL4BlendStateEnabled : MTL4BlendStateDisabled;'
        assert original_include.count(old) == 1
        include.write_text(original_include.replace(old, 'dst.blendingState = MTL4BlendStateDisabled;'))
        mutant = work / 'mutant-blend'
        run('build-mutant-blend', common + ['-DDXMT_METAL4_COMPILER=1', str(fixture), '-o', str(mutant)])
        mutation = run('mutant-blend', [str(mutant)], expected=1, env=enabled)
        assert b'FAIL ' in mutation.stderr, 'missing connected mutation witness'
        include.write_text(original_include)
        # Same exact production slice must also compile with APIs erased and with
        # the build opt-in absent; this is not an old physical SDK/OS claim.
        guard = work / 'guard.m'
        guard.write_text('''#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <string.h>
#include "winemetal_thunks.h"
#include "next_metal4_compiler.inc"
typedef int NTSTATUS;
#define STATUS_SUCCESS 0
static MTLPixelFormat to_metal_pixel_format(enum WMTPixelFormat f) { return (MTLPixelFormat)ORIGINAL_FORMAT(f); }
''' + '\n\n'.join(functions) + '''
int main(void) {
  if (WMT_METAL4_AVAILABLE) return 2;
  if (wmt_metal4_render(nil, nil, NULL, NULL, NULL)) return 3;
  if (wmt_metal4_compute(nil, nil, NULL, NULL, NULL)) return 4;
  puts("PASS disabled/unavailable build uses legacy route");
  return 0;
}
''')
        for label, flags in [('disabled', []), ('unavailable', ['-DDXMT_METAL4_COMPILER=1', '-DDXMT_METAL4_FORCE_UNAVAILABLE'])]:
            target = work / label
            run('build-' + label, common + flags + [str(guard), '-o', str(target)])
            run(label, [str(target)], env=enabled)
        runtime_guard = work / 'runtime-guard.m'
        runtime_guard.write_text('''#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <string.h>
#include "winemetal_thunks.h"
#include "next_metal4_compiler.inc"
int main(void) {
  if (wmt_metal4_enabled()) return 2;
  setenv("DXMT_METAL4_COMPILER", "1", 1);
  if (wmt_metal4_enabled()) return 3;
  puts("PASS runtime opt-in disabled and frozen");
  return 0;
}
''')
        target = work / 'runtime-guard'
        run('build-runtime-guard', common + ['-DDXMT_METAL4_COMPILER=1', str(runtime_guard), '-o', str(target)])
        run('runtime-guard', [str(target)], env={**os.environ, 'DXMT_METAL4_COMPILER': '0'})
        # Both supported host ABI targets compile the actual bridge source.
        run('syntax-x86_64', common + ['-arch', 'x86_64', '-DDXMT_METAL4_COMPILER=1',
                                     '-fsyntax-only', str(fixture)])
        binary_hash = digest(binary.read_bytes())
    for path, original in snapshots.items():
        assert Path(path).read_bytes() == original, 'source drift: ' + path
    receipt = {'status': 'PASS', 'scope': 'exact-native-bridge-offscreen-gpu',
               'sources': {path: digest(data) for path, data in snapshots.items()},
               'generated_source_sha256': digest(source.encode()), 'binary_sha256': binary_hash,
               'commands': commands, 'gameplay': False, 'installed': False, 'performance_claim': False}
    (args.output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({'status': 'PASS', 'receipt': str(args.output / 'receipt.json')}))


if __name__ == '__main__':
    main()
