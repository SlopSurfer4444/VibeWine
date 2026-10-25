#!/usr/bin/env python3
"""CPU-only fence packet assessment: exact thunk, actual headers, separate TUs, no LTO."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]

def block(text, marker):
    start = text.index(marker)
    cursor = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[cursor] == '{') - (text[cursor] == '}')
        cursor += 1
    return text[start:cursor]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, default=REPO)
    parser.add_argument('--sanitize', action='store_true', help='behavioral validation; timings are instrumented')
    args = parser.parse_args()
    root = args.source_dir
    context = (root / 'src/dxmt/dxmt_context.cpp').read_text()
    flush = context.index('ArgumentEncodingContext::flushCommands(')
    wait_start = context.index('      data->fence_wait.forEach(', flush)
    wait_end = context.index('\n      );', wait_start) + len('\n      );')
    update_start = context.index('      data->fence_update_vertex.forEach(', wait_end)
    update_end = context.index('\n      );', update_start) + len('\n      );')
    wait, update = context[wait_start:wait_end], context[update_start:update_end]
    fixture = (HERE / 'fixture.cpp.in').read_text()
    for name, text in [('WAIT', wait), ('UPDATE', update)]:
        fixture = fixture.replace('// PRODUCTION_' + name, text)
        fixture = fixture.replace('// PACKET_' + name, text.replace('encoder.waitForFence(', 'packet.append(').replace('encoder.updateFence(', 'packet.append('))
    thunk_source = (root / 'src/winemetal/winemetal_thunks.c').read_text()
    thunk = block(thunk_source, 'WINEMETAL_API void\nMTLRenderCommandEncoder_encodeCommands(')
    thunk = '#include "observer.hpp"\n#define UNIX_CALL(op, params) observe_unix_call(op, params)\n' + thunk
    compiler = shutil.which('clang++') or shutil.which('c++')
    assert compiler
    with tempfile.TemporaryDirectory(prefix='dxmt-fence-assessment-') as temporary:
        scratch = Path(temporary)
        (scratch / 'fixture.cpp').write_text(fixture)
        (scratch / 'thunk.cpp').write_text(thunk)
        flags = ['-std=c++20', '-O2', '-fno-lto', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter']
        if args.sanitize:
            flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
        includes = ['-I' + str(p) for p in [HERE, root / 'include', root / 'src/winemetal', root / 'src/dxmt', root / 'src/util']]
        objects = []
        for index, source in enumerate([scratch / 'fixture.cpp', scratch / 'thunk.cpp', HERE / 'observer.cpp']):
            output = scratch / (str(index) + '.o')
            subprocess.run([compiler, *flags, *includes, '-c', str(source), '-o', str(output)], check=True, timeout=60)
            objects.append(str(output))
        binary = scratch / 'assessment'
        subprocess.run([compiler, *flags, *objects, '-o', str(binary)], check=True, timeout=60)
        run = subprocess.run([str(binary)], text=True, capture_output=True, check=True, timeout=60)
    result = json.loads(run.stdout)
    bindings = ['src/dxmt/dxmt_context.cpp', 'src/dxmt/dxmt_deptrack.hpp', 'src/winemetal/Metal.hpp',
                'src/winemetal/winemetal.h', 'src/winemetal/winemetal_thunks.h', 'src/winemetal/winemetal_thunks.c',
                'src/winemetal/unix/winemetal_unix.c']
    result.update(scope='CPU proxy, actual FenceSet/Metal.hpp/ABI, exact opcode-38 thunk; separate native observer, no LTO; no Wine/Metal/GPU',
                  sanitized=args.sanitize, architecture=platform.machine(), samples=7, iterations_per_sample=30000,
                  compiler=subprocess.check_output([compiler, '--version'], text=True).splitlines()[0],
                  sources_sha256={path: hashlib.sha256((root / path).read_bytes()).hexdigest() for path in bindings},
                  assessment_sha256={path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                                     for path in [HERE / 'run.py', HERE / 'fixture.cpp.in', HERE / 'observer.hpp', HERE / 'observer.cpp']})
    print(json.dumps(result, indent=2))

if __name__ == '__main__':
    main()
