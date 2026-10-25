#!/usr/bin/env python3
"""Native Foundation ownership test; no GPU, installed files or Wine execution."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

here = Path(__file__).resolve().parent
root = here.parents[2]

def function(source, name, prefix):
    pattern = rf'^{prefix}\n{re.escape(name)}\([\s\S]*?^\}}'
    matches = re.findall(pattern, source, re.MULTILINE)
    assert len(matches) == 1, (name, len(matches))
    return matches[0]

native = (root / 'src/winemetal/unix/winemetal_unix.c').read_text()
thunks = (root / 'src/winemetal/winemetal_thunks.c').read_text()
source = (here / 'fixture.mm.in').read_text().replace('CreateAutoreleasePool', 'MakeAutoreleasePool')
native_source = (here / 'native.m.in').read_text().replace('// EXACT_NATIVE_FUNCTIONS', '\n\n'.join(
    function(native, name, 'static NTSTATUS') for name in [
        '_NSObject_retain', '_NSObject_release', '_NSAutoreleasePool_alloc_init', '_MTLDevice_newLibrary']))
source = source.replace('// EXACT_PE_THUNKS', '\n\n'.join(
    function(thunks, name, r'WINEMETAL_API (?:void|obj_handle_t)') for name in [
        'NSObject_retain', 'NSObject_release', 'NSAutoreleasePool_alloc_init', 'MTLDevice_newLibrary']))
header = (root / 'src/winemetal/Metal.hpp').read_text()
baseline = subprocess.check_output(['git', 'show', '25eff5e13003d5e7f6b7cf6e1c76e9e6f3363e27:src/winemetal/Metal.hpp'], cwd=root, text=True)
assert header.count('AdoptAutoreleasedError(error, error_handle);') == 14
assert '&error.handle' not in header
# Raw artifacts are private scratch under this test; deleted even after a failed check.
with tempfile.TemporaryDirectory(prefix='.tmp-', dir=here) as directory:
    work = Path(directory)
    (work / 'fixture.mm').write_text(source)
    (work / 'native.m').write_text(native_source)
    for name, text in [('current', header), ('baseline', baseline), ('omit-retain', header.replace(
            'error = Reference<Error>(Error{autoreleased});',
            'error = Reference<Error>(autoreleased);')),
            ('donor-release-before-retain', header.replace(
                'error = Reference<Error>(Error{autoreleased});', 'error = Error{autoreleased};'))]:
        include = work / name
        include.mkdir()
        (include / 'Metal.hpp').write_text(text)
        # Other checked-in headers remain the production candidate's exact bytes.
        for dependency in ['winemetal.h', 'wave7_layer_apply.h']:
            (include / dependency).write_bytes((root / 'src/winemetal' / dependency).read_bytes())
        for arch in ['arm64', 'x86_64']:
            binary = work / f'{name}-{arch}'
            native_object = work / f'native-{arch}.o'
            if not native_object.exists():
                subprocess.run(['clang', '-std=c2x', '-fno-objc-arc', '-Wall', '-Wextra', '-Werror',
                    '-O1', '-g', '-arch', arch, '-fsanitize=address,undefined',
                    '-I', str(root / 'src/winemetal'), '-c', str(work / 'native.m'),
                    '-o', str(native_object)], check=True, timeout=45)
            command = [os.environ.get('CXX', 'clang++'), '-std=c++20', '-fno-objc-arc',
                       '-Wall', '-Wextra', '-Werror', '-O1', '-g', '-arch', arch,
                       '-fsanitize=address,undefined', '-framework', 'Foundation',
                       '-I', str(include), '-I', str(root / 'src/winemetal'),
                       str(work / 'fixture.mm'), str(native_object), '-o', str(binary)]
            subprocess.run(command, check=True, timeout=45)
            result = subprocess.run(['/usr/bin/arch', '-' + arch, str(binary)],
                capture_output=True, text=True, timeout=20,
                env={**os.environ, 'ASAN_OPTIONS': 'halt_on_error=1', 'UBSAN_OPTIONS': 'halt_on_error=1'})
            if name == 'current':
                assert result.returncode == 0, (name, arch, result.returncode, result.stderr)
                print(arch, result.stdout.strip())
            else:
                assert result.returncode != 0, (name, arch, result.stdout)
                if name in ['baseline', 'omit-retain']:
                    assert result.returncode == 1 and 'CHECK failed' in result.stderr, result.stderr
                    assert 'Sanitizer' not in result.stderr and 'runtime error:' not in result.stderr, result.stderr
                else:
                    assert 'Sanitizer' in result.stderr, result.stderr
                print(f'PASS negative control {name} {arch}: exit={result.returncode}')
            assert sum(p.stat().st_size for p in work.rglob('*') if p.is_file()) < 64 * 1024 * 1024
print('PASS exact production native/PE ownership seams; baseline fails/new passes; two mutation controls; arm64+x86_64 ASan/UBSan')
