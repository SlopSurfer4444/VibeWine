#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise unchanged production CreateShader and wrapper methods with real SHA1."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
CALLER = 'src/d3d11/d3d11_pipeline_cache.cpp'
BASELINE = 'e3845197'
PATHS = [CALLER, 'src/d3d11/d3d11_shader_singleflight.hpp', 'src/airconv/airconv_public.h',
         'src/util/sha1/sha1.c', 'src/util/sha1/sha1_util.cpp', 'src/util/sha1/sha1_util.hpp']


def block(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def generated(caller, fixture, historical=False):
    # The enclosing storage adapts only Equal to observe that each concurrent
    # caller has joined the pending entry. It delegates actual SHA1 equality.
    storage = ('std::unordered_map<Sha1Digest, std::unique_ptr<CachedSM50Shader>, '
               'std::hash<Sha1Digest>, ObservedDigestEqual> shaders_;\n'
               'std::shared_mutex mutex_shares;' if historical else
               'ShaderSingleflightCache<Sha1Digest, CachedSM50Shader, '
               'std::hash<Sha1Digest>, ObservedDigestEqual> shaders_;')
    substitutions = {
        'CREATE_SHADER': block(caller, '  CachedSM50Shader *CreateShader('),
        'CONSTRUCTOR': block(caller, '    CachedSM50Shader(PipelineCache *cache,'),
        'DESTRUCTOR': block(caller, '    ~CachedSM50Shader()'),
        'INITIALIZED': '' if historical else block(caller, '    bool initialized() const'),
        'ARGUMENT_FIELD': re.search(r'(?m)^\s*MTL_SM50_SHADER_ARGUMENT \*arguments_info_buffer[^;]*;', caller)[0],
        'DEBUG_FIELDS': '#ifdef DXMT_DEBUG\n' + re.search(r'(?m)^\s*void \*bytecode[^;]*;\s*size_t bytecode_length;', caller)[0] + '\n#endif',
        'STORAGE': storage,
    }
    code = fixture
    for marker, value in substitutions.items():
        token = '// ' + marker + '\n'
        assert code.count(token) == 1, marker
        code = code.replace(token, value + '\n')
    return code


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, default=REPO)
    parser.add_argument('--arch', choices=['arm64', 'x86_64'])
    parser.add_argument('--sanitize', choices=['address', 'thread', 'none'], default='address')
    parser.add_argument('--keep', type=Path, help='Retain source hashes and logs; generated binaries are temporary')
    args = parser.parse_args()
    source = args.source_dir.resolve()
    snapshot = {path: (source / path).read_bytes() for path in PATHS}
    fixture_bytes = (HERE / 'createshader.cpp.in').read_bytes()
    runner_bytes = Path(__file__).read_bytes()
    caller = snapshot[CALLER].decode()
    historical = subprocess.check_output(['git', '-C', str(source), 'show', BASELINE + ':' + CALLER], text=True)
    ordinary = ['success', 'no-args', 'no-error', 'owned-error', 'argument-allocation']
    exceptional = ['initialize-throw', 'wrapper-allocation', 'error-log-throw', 'error-message-throw', 'persistent-log-throw']
    variants = [('current-exceptions', caller, [], ordinary + exceptional, False),
                ('current-no-exceptions', caller, ['-fno-exceptions'], ordinary, False),
                ('current-debug', caller, ['-DDXMT_DEBUG'], ordinary + exceptional + ['debug-allocation'], False),
                ('historical', historical, ['-fno-exceptions', '-DHISTORICAL_STORAGE'], ['success'], True)]
    results, logs = [], {}
    with tempfile.TemporaryDirectory(prefix='dxmt-createshader-') as directory:
        scratch = Path(directory)
        common = ['-O1', '-gline-tables-only', '-Wall', '-Wextra', '-Werror', '-Wno-unused-private-field',
                  '-pthread', '-fno-omit-frame-pointer', '-I', str(source / 'src/d3d11'),
                  '-I', str(source / 'src/airconv'), '-I', str(source / 'src/util')]
        if args.arch:
            common += ['-arch', args.arch]
        if args.sanitize != 'none':
            common += ['-fsanitize=' + ('address,undefined' if args.sanitize == 'address' else 'thread'),
                       '-fno-sanitize-recover=all']
        sha = scratch / 'sha1.o'
        subprocess.run([os.environ.get('CC', 'clang'), '-std=c2x', *common, '-c',
                        str(source / 'src/util/sha1/sha1.c'), '-o', str(sha)], check=True, timeout=60)
        for name, contents, defines, cases, old in variants:
            cpp, binary = scratch / (name + '.cpp'), scratch / name
            cpp.write_text(generated(contents, fixture_bytes.decode(), old))
            build = subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++20', *common, *defines,
                                    str(cpp), str(source / 'src/util/sha1/sha1_util.cpp'), str(sha),
                                    '-o', str(binary)], capture_output=True, text=True, timeout=60)
            logs[name + '-build.txt'] = build.stdout + build.stderr
            if build.returncode:
                print(logs[name + '-build.txt'], end='')
                build.check_returncode()
            launch = [str(binary)]
            if args.arch and platform.machine() != args.arch:
                launch = ['/usr/bin/arch', '-' + args.arch, *launch]
            for case in cases:
                result = subprocess.run([*launch, case], capture_output=True, text=True, timeout=30,
                                        env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=' + ('0' if platform.system() == 'Darwin' else '1'),
                                             'UBSAN_OPTIONS': 'halt_on_error=1',
                                             'TSAN_OPTIONS': 'exitcode=66:abort_on_error=0'})
                output = result.stdout + result.stderr
                logs[name + '-' + case + '-stdout.txt'] = result.stdout
                logs[name + '-' + case + '-stderr.txt'] = result.stderr
                rejection = 'same-key concurrent CreateShader initializes once' if old else None
                assert result.returncode == (1 if rejection else 0), f'{name}/{case}: {result.returncode}\n{output}'
                if rejection:
                    assert 'CONTRACT: ' + rejection in output, output
                else:
                    assert result.stdout.startswith('PASS actual CreateShader:') and not result.stderr, output
                assert 'ERROR: AddressSanitizer' not in output and 'runtime error:' not in output, output
                record = {'variant': name, 'case': case, 'exit': result.returncode,
                          'expected_rejection': rejection, 'output': output.strip()}
                results.append(record)
                print(json.dumps(record), flush=True)
        assert sum(p.stat().st_size for p in scratch.iterdir()) < 64 * 1024 * 1024, '64 MiB scratch cap'
    assert all((source / path).read_bytes() == contents for path, contents in snapshot.items()), 'source changed during test run; discard receipt'
    assert (HERE / 'createshader.cpp.in').read_bytes() == fixture_bytes, 'fixture changed during test run; discard receipt'
    assert Path(__file__).read_bytes() == runner_bytes, 'runner changed during test run; discard receipt'
    receipt = {'arch': args.arch or platform.machine(), 'sanitize': args.sanitize, 'baseline_ref': BASELINE,
               'scope': 'unchanged production CreateShader/constructor/destructor/initialized; real SHA1; observed equality and mocked SM50/allocation boundaries; no Wine/native parser/GPU/game',
               'sources_sha256': {path: hashlib.sha256(contents).hexdigest() for path, contents in snapshot.items()},
               'historical_caller_sha256': hashlib.sha256(historical.encode()).hexdigest(),
               'fixture_sha256': hashlib.sha256(fixture_bytes).hexdigest(),
               'runner_sha256': hashlib.sha256(runner_bytes).hexdigest(), 'results': results}
    if args.keep:
        args.keep.mkdir(parents=True, exist_ok=True)
        for name, contents in logs.items():
            (args.keep / name).write_text(contents)
        (args.keep / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt, sort_keys=True))


if __name__ == '__main__':
    main()
