#!/usr/bin/env python3
"""Extract production resource creation bodies and exercise CPU failure edges."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
SOURCES = ('src/d3d11/d3d11_buffer.cpp', 'src/d3d11/d3d11_texture_device.cpp',
           'src/util/rc/util_rc_ptr.hpp', 'src/util/util_likely.hpp')


def block(text, marker, with_catch=False):
    start = text.index(marker)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    if with_catch:
        assert text[end:].lstrip().startswith('catch (const std::bad_alloc &)')
        catch_start = text.index('catch (const std::bad_alloc &)', end)
        tail = block(text[catch_start:], 'catch (const std::bad_alloc &)')
        end = catch_start + len(tail)
    return text[start:end]


def generate(contents, fixture):
    buffer, texture = contents[SOURCES[0]], contents[SOURCES[1]]
    owner_start = texture.index('  DeviceTexture(const tag_texture::DESC1 *pDesc,')
    owner_end = texture.index('  Rc<StagingResource> staging(', owner_start)
    parts = {
        'BUFFER_CONSTRUCTOR': block(buffer, '  D3D11Buffer(const tag_buffer::DESC1 *pDesc,'),
        'BUFFER_INITIALIZE': block(buffer, '  HRESULT\n  Initialize('),
        'BUFFER_FACTORY': block(buffer, 'HRESULT\nCreateBuffer('),
        'TEXTURE_OWNER': texture[owner_start:owner_end],
        'MACH_GUARD': block(texture, 'class SharedTextureMachPortGuard {') + ';',
        'TEXTURE_FACTORY': block(texture, 'template <typename tag>\nHRESULT CreateDeviceTextureInternal(', True),
    }
    for name, code in parts.items():
        marker = '// ' + name + '\n'
        assert fixture.count(marker) == 1, name
        fixture = fixture.replace(marker, code + '\n')
    return fixture


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, default=HERE.parents[2])
    parser.add_argument('--arch', choices=('arm64', 'x86_64'))
    args = parser.parse_args()
    source = args.source_dir.resolve()
    source_bytes = {name: (source / name).read_bytes() for name in SOURCES}
    fixture_bytes = {name: (HERE / name).read_bytes() for name in ('mocks.hpp', 'fixture.cpp.in', 'run.py')}
    contents = {name: data.decode() for name, data in source_bytes.items()}
    generated = generate(contents, fixture_bytes['fixture.cpp.in'].decode())
    variants = [('current', generated, None)]
    # Independently remove material checks from the extracted candidate. These
    # controls must fail contract assertions, without sanitizer crashes.
    mapping = generated.replace('result.status == InitStatus::OutOfMemory ? E_OUTOFMEMORY : E_FAIL', 'E_OUTOFMEMORY')
    assert mapping != generated
    variants.append(('wrong-failure-mapping', mapping, 'failure category preserved'))
    ignored = generated.replace('if (!result)', 'if (!result && false)')
    assert ignored != generated
    variants.append(('ignored-initializer-failure', ignored, 'failure category preserved'))
    no_null = generated.replace('if (!allocation || !allocation->texture())', 'if (!allocation)')
    assert no_null != generated
    variants.append(('missing-native-check', no_null, 'native texture checked before rename'))
    no_adopt = generated.replace('resource->setSharedHandles(create.hResource, create.hGlobalShare);', '(void)create.hResource;')
    assert no_adopt != generated
    variants.append(('missing-shared-adoption', no_adopt, 'shared resource released exactly once'))
    no_release = generated.replace('WMTMachPortDeallocate(port_);', '(void)port_;')
    assert no_release != generated
    variants.append(('missing-unpublished-port-release', no_release, 'fresh unpublished Mach right released exactly once'))
    results = []
    with tempfile.TemporaryDirectory(prefix='dxmt-initializer-creation-') as directory:
        scratch = Path(directory)
        for name, data in source_bytes.items():
            destination = scratch / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
        (scratch / 'mocks.hpp').write_bytes(fixture_bytes['mocks.hpp'])
        for name, code, rejection in variants:
            cpp, executable = scratch / (name + '.cpp'), scratch / name
            cpp.write_text(code)
            command = [os.environ.get('CXX', shutil.which('clang++') or 'clang++'),
                       '-std=c++20', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                       '-Wno-unused-parameter', '-Wno-unused-private-field',
                       '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                       '-fno-omit-frame-pointer', '-I', str(scratch), str(cpp), '-o', str(executable)]
            if args.arch:
                command[1:1] = ['-arch', args.arch]
            subprocess.run(command, check=True, timeout=60)
            launch = [str(executable)]
            if args.arch and args.arch != platform.machine():
                launch = ['/usr/bin/arch', '-' + args.arch, *launch]
            result = subprocess.run(launch, capture_output=True, text=True, timeout=30,
                                    env={**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1'})
            output = result.stdout + result.stderr
            if rejection:
                assert result.returncode == 1 and 'CONTRACT: ' + rejection in output, (name, result.returncode, output)
            else:
                assert result.returncode == 0, (name, result.returncode, output)
            assert 'ERROR: AddressSanitizer' not in output and 'runtime error:' not in output, output
            record = {'variant': name, 'exit': result.returncode, 'expected_rejection': rejection, 'output': output.strip()}
            results.append(record)
            print(json.dumps(record), flush=True)
        assert sum(p.stat().st_size for p in scratch.rglob('*') if p.is_file()) < 64 * 1024 * 1024
    print(json.dumps({'arch': args.arch or platform.machine(), 'scope': 'production creation bodies; CPU allocation/platform mocks; no Wine, Metal, GPU, installed runtime or game',
                      'sanitizers': ['address', 'undefined'],
                      'sources_sha256': {name: hashlib.sha256(data).hexdigest() for name, data in source_bytes.items()},
                      'fixtures_sha256': {name: hashlib.sha256(data).hexdigest() for name, data in fixture_bytes.items()},
                      'generated_sha256': hashlib.sha256(generated.encode()).hexdigest(), 'results': results}, indent=2))


if __name__ == '__main__':
    main()
