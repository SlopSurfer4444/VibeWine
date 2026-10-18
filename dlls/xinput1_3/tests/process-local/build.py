#!/usr/bin/env python3
"""Build private x64 builtin DLLs and executable fixture from a configured Wine build.

Adapts the configured Wine Makefile/compile database; never writes to its inputs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--configured-build', required=True, type=Path)
parser.add_argument('--toolchain', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
a = parser.parse_args()
build, toolchain, out = (p.resolve() for p in (a.configured_build, a.toolchain, a.output))
source = Path(__file__).resolve().parents[4]
if source == out or source in out.parents or build == out or build in out.parents:
    parser.error('Use a fresh output outside source and configured build')
out.mkdir(parents=True, exist_ok=False)
for part in ('imports', 'resources', 'objects', 'dlls', 'logs', 'tmp'):
    (out / part).mkdir()
make = (build / 'Makefile').read_text()
rows = json.loads((build / 'compile_commands.json').read_text())
env = os.environ.copy()
env['PATH'] = str(toolchain / 'bin') + ':/usr/bin:/bin:/usr/sbin:/sbin'
env['TMPDIR'] = str(out / 'tmp')
commands = []


def recipe(target):
    start = make.index('\n' + target + ':') + 1
    start = make.index('\n\t', start) + 1
    end = make.index('\n', start)
    while make[end - 1] == '\\':
        end = make.index('\n', end + 1)
    return shlex.split(make[start:end].strip().replace('\\\n', ' '))


def run(label, argv):
    result = subprocess.run([str(v) for v in argv], cwd=out, env=env, capture_output=True, text=True)
    (out / 'logs' / (label + '.log')).write_text(result.stdout + result.stderr)
    commands.append({'label': label, 'argv': [str(v) for v in argv], 'returncode': result.returncode})
    (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    print(label, result.returncode, flush=True)
    if result.returncode:
        raise SystemExit(result.stdout + result.stderr)


for lib in ('hid', 'setupapi'):
    cmd = recipe(f'dlls/{lib}/x86_64-windows/lib{lib}.a')
    cmd[0] = str(build / cmd[0])
    cmd[cmd.index('-o') + 1] = str(out / 'imports' / f'lib{lib}.a')
    cmd[cmd.index('--export') + 1] = str(source / 'dlls' / lib / f'{lib}.spec')
    run('import-' + lib, cmd)
for module in ('xinput1_3', 'xinput1_4'):
    cmd = recipe(f'dlls/{module}/version.res')
    cmd[0] = str(build / cmd[0])
    cmd[cmd.index('-o') + 1] = str(out / 'resources' / (module + '.res'))
    cmd = ['-I' + str(build / v[2:]) if v.startswith('-I') and not v[2:].startswith('/') else v for v in cmd]
    run('resource-' + module, cmd)
    row = next(r for r in rows if r.get('output') == f'dlls/{module}/x86_64-windows/main.o')
    cmd = shlex.split(row['command'])
    cmd[0] = str(toolchain / 'bin' / cmd[0])
    cmd[cmd.index(row['file'])] = str(source / 'dlls/xinput1_3/main.c')
    cmd[cmd.index('-o') + 1] = str(out / 'objects' / (module + '.o'))
    cmd = ['-I' + str(build / v[2:]) if v.startswith('-I') and not v[2:].startswith('/') else v for v in cmd]
    run('compile-' + module, cmd)
    cmd = recipe(f'dlls/{module}/x86_64-windows/{module}.dll')
    replacements = {'tools/winegcc/winegcc': str(build / 'tools/winegcc/winegcc'),
                    '$@': str(out / 'dlls' / (module + '.dll')), '.': str(build),
                    f'dlls/{module}/x86_64-windows/main.o': str(out / 'objects' / (module + '.o')),
                    f'dlls/{module}/version.res': str(out / 'resources' / (module + '.res'))}
    for lib in ('hid', 'setupapi', 'advapi32', 'user32', 'winecrt0', 'ucrtbase', 'kernel32', 'ntdll'):
        rel = f'dlls/{lib}/x86_64-windows/lib{lib}.a'
        replacements[rel] = str(out / 'imports' / f'lib{lib}.a') if lib in ('hid', 'setupapi') else str(build / rel)
    cmd = [replacements.get(v, v) for v in cmd]
    flags = shlex.split(re.search(r'^x86_64_LDFLAGS = (.*)$', make, re.M)[1])
    cmd = [item for v in cmd for item in (flags if v == '$(x86_64_LDFLAGS)' else [v])]
    cmd = [str(source / 'dlls' / module / (module + '.spec')) if v.endswith('/' + module + '.spec') else v for v in cmd]
    run('link-' + module, cmd)
    run('inspect-' + module, [toolchain / 'bin/llvm-readobj', '--file-headers', '--coff-exports', '--coff-imports', out / 'dlls' / (module + '.dll')])
    # Builtin lookup uses the PE internal export name even for an absolute path.
    # Link the very same object with a unique internal name for fixture loading.
    fixture_name = 'vibewine_test_' + module + '.dll'
    fixture_cmd = cmd.copy()
    fixture_cmd[fixture_cmd.index('-o') + 1] = str(out / 'dlls' / fixture_name)
    fixture_cmd.append('-Wb,--filename,' + fixture_name)
    run('link-fixture-' + module, fixture_cmd)
    run('inspect-fixture-' + module, [toolchain / 'bin/llvm-readobj', '--file-headers', '--coff-exports', '--coff-imports', out / 'dlls' / fixture_name])
run('compile-fixture', [toolchain / 'bin/x86_64-w64-mingw32-clang', '-Wall', '-Wextra', '-Werror', '-O2',
                        source / 'dlls/xinput1_3/tests/process-local/fixture.c', '-o', out / 'fixture.exe'])
inputs = {str(p.relative_to(source)): hashlib.sha256(p.read_bytes()).hexdigest() for p in
          (source / 'dlls/xinput1_3/main.c', source / 'dlls/xinput1_3/test_backend.h', Path(__file__), Path(__file__).with_name('fixture.c'))}
outputs = {str(p.relative_to(out)): hashlib.sha256(p.read_bytes()).hexdigest() for p in (out / 'dlls').glob('*.dll')}
outputs['fixture.exe'] = hashlib.sha256((out / 'fixture.exe').read_bytes()).hexdigest()
(out / 'receipt.json').write_text(json.dumps({'state': 'BUILT_NOT_INSTALLED', 'inputs': inputs, 'outputs': outputs, 'commands': commands}, indent=2) + '\n')
print(out)
