#!/usr/bin/env python3
"""Exact-production bridge fixture using only test-owned Mach rights, no GPU."""
from pathlib import Path
import hashlib
import os
import platform
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BASELINE = '66bae06c1cba97db49172ef555e4ea4f2cc60efc'
PREFIX_HASHES = {
    '__wine_unix_call_funcs': '4bfc97e65ebe52d5a2acc4cd12e6cb5ee52a9ab49cf477d80ffb6d4166b60c78',
    '__wine_unix_call_wow64_funcs': '0bcbe4aeb4de349d9ef32774c9978b9d2ab6b96bf132033b4b2311b132d51346',
}

def function(source, name, prefix):
    matches = re.findall(rf'^{prefix}\n{re.escape(name)}\([\s\S]*?^\}}', source, re.MULTILINE)
    assert len(matches) == 1, (name, len(matches))
    return matches[0]


def table(source, name):
    matches = re.findall(r'const void \*' + name + r'\[\] = \{(.*?)\n\};', source, re.S)
    assert len(matches) == 1, name
    entries = [entry.strip().removeprefix('&') for entry in matches[0].split(',') if entry.strip()]
    residue = re.sub(r'(?:&[A-Za-z0-9_]+|NULL)\s*,', '', matches[0])
    assert not residue.strip(), (name, residue)
    return entries


INPUT_PATHS = ['src/winemetal/winemetal.h', 'src/winemetal/winemetal_thunks.h', 'src/winemetal/winemetal_thunks.c', 'src/winemetal/unix/winemetal_unix.c', 'src/dxmt/dxmt_texture.cpp']
input_bytes = {relative: (ROOT / relative).read_bytes() for relative in INPUT_PATHS}
native = input_bytes['src/winemetal/unix/winemetal_unix.c'].decode()
thunks = input_bytes['src/winemetal/winemetal_thunks.c'].decode()
for name, frozen_hash in PREFIX_HASHES.items():
    current = table(native, name)
    assert len(current) == 151
    assert hashlib.sha256(('\n'.join(current[:150]) + '\n').encode()).hexdigest() == frozen_hash
    assert current[149:] == ['_MTLAllocation_allocatedSize', '_WMTMachPortDeallocate'], name
    print('PASS', name, 'baseline 0..149 frozen; allocationSize=149; cleanup=150; length=151')

native_body = function(native, '_WMTMachPortDeallocate', 'static NTSTATUS')
thunk_body = function(thunks, 'WMTMachPortDeallocate', 'WINEMETAL_API void')
assert 'UNIX_CALL(150, &params);' in thunk_body
assert 'params = {mach_port};' in thunk_body
assert 'mach_port_deallocate(mach_task_self(), params->mach_port)' in native_body
variants = {
    'current': (native_body, thunk_body),
    'wrong-ordinal': (native_body, thunk_body.replace('UNIX_CALL(150,', 'UNIX_CALL(149,')),
    'wrong-port': (native_body, thunk_body.replace('params = {mach_port};', 'params = {0};')),
    'omit-deallocate': (native_body.replace('mach_port_deallocate(mach_task_self(), params->mach_port)', 'KERN_SUCCESS'), thunk_body),
}
assert platform.system() == 'Darwin', 'This fixture requires real macOS Mach ports.'
with tempfile.TemporaryDirectory(prefix='.tmp-', dir=HERE) as directory:
    work = Path(directory)
    include = work / 'include'
    include.mkdir()
    for header in ['winemetal.h', 'winemetal_thunks.h']:
        (include / header).write_bytes(input_bytes['src/winemetal/' + header])
    for name, (native_variant, thunk_variant) in variants.items():
        source = (HERE / 'fixture.c.in').read_text().replace('// EXACT_NATIVE', native_variant).replace('// EXACT_THUNK', thunk_variant)
        path = work / (name + '.c')
        path.write_text(source)
        common = ['clang', '-std=c2x', '-Wall', '-Wextra', '-Werror', '-I', str(include)]
        if name == 'current':
            for arch in ['arm64', 'x86_64']:
                subprocess.run(common + ['-arch', arch, '-fsyntax-only', str(path)], check=True, timeout=45)
                print('PASS exact bridge syntax', arch)
        binary = work / name
        subprocess.run(common + ['-O1', '-g', '-fsanitize=address,undefined', str(path), '-o', str(binary)], check=True, timeout=45)
        assert sum(p.stat().st_size for p in work.rglob('*') if p.is_file()) < 64 * 1024 * 1024
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20,
            env={**os.environ, 'ASAN_OPTIONS': 'halt_on_error=1', 'UBSAN_OPTIONS': 'halt_on_error=1'})
        assert 'Sanitizer' not in result.stderr and 'runtime error:' not in result.stderr, result.stderr
        if name == 'current':
            assert result.returncode == 0, (name, result.stderr)
            print(platform.machine(), result.stdout.strip())
        else:
            assert result.returncode == 1 and 'CHECK failed' in result.stderr, (name, result.returncode, result.stderr)
            print('PASS rejected mutation', name)
    # Compile the exact production bridge into a separate C object for C++ caller cases.
    bridge_object = work / 'bridge.o'
    subprocess.run(common + ['-O1', '-g', '-fsanitize=address,undefined', '-DFIXTURE_LIBRARY',
        '-c', str(work / 'current.c'), '-o', str(bridge_object)], check=True, timeout=45)
    texture_text = input_bytes['src/dxmt/dxmt_texture.cpp'].decode()
    allocate_matches = re.findall(r'^Rc<TextureAllocation>\nTexture::allocate\(Flags<TextureAllocationFlag> flags\) \{[\s\S]*?^\}', texture_text, re.M)
    constructor_matches = re.findall(r'^TextureAllocation::TextureAllocation\(\n    Texture \*descriptor, WMT::Reference<WMT::Texture> &&texture,[\s\S]*?^\};', texture_text, re.M)
    assert len(allocate_matches) == len(constructor_matches) == 1
    allocate, constructor = allocate_matches[0], constructor_matches[0]
    assert 'WMTMachPortDeallocate(info.mach_port);' in allocate
    for name, candidate in [('texture-current', allocate), ('texture-omit-cleanup', allocate.replace('WMTMachPortDeallocate(info.mach_port);', '(void)info.mach_port;'))]:
        source = (HERE / 'texture.cpp.in').read_text().replace('// EXACT_TEXTURE_CONSTRUCTOR', constructor).replace('// EXACT_TEXTURE_ALLOCATE', candidate)
        path = work / (name + '.cpp')
        path.write_text(source)
        cxx = ['clang++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(include)]
        if name == 'texture-current':
            for arch in ['arm64', 'x86_64']:
                subprocess.run(cxx + ['-arch', arch, '-fsyntax-only', str(path)], check=True, timeout=45)
                print('PASS exact Texture allocation syntax', arch)
        binary = work / name
        subprocess.run(cxx + ['-O1', '-g', '-fsanitize=address,undefined', str(path), str(bridge_object), '-o', str(binary)], check=True, timeout=45)
        assert sum(p.stat().st_size for p in work.rglob('*') if p.is_file()) < 64 * 1024 * 1024
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20,
            env={**os.environ, 'ASAN_OPTIONS': 'halt_on_error=1', 'UBSAN_OPTIONS': 'halt_on_error=1'})
        assert 'Sanitizer' not in result.stderr and 'runtime error:' not in result.stderr, result.stderr
        if name == 'texture-current':
            assert result.returncode == 0, (name, result.stderr)
            print(platform.machine(), result.stdout.strip())
        else:
            assert result.returncode == 1 and 'CHECK failed' in result.stderr, (name, result.returncode, result.stderr)
            print('PASS rejected mutation', name)
    for relative, original in input_bytes.items():
        assert (ROOT / relative).read_bytes() == original, ('input changed during test', relative)
        print('INPUT_SHA256', hashlib.sha256(original).hexdigest(), relative)
print('PASS exact production Mach bridge; host ASan/UBSan; no private scratch retained')
