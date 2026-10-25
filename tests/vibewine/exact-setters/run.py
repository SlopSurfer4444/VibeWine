#!/usr/bin/env python3
"""Production context setters and binding/dirty/rename seams; no native GPU runtime."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
PATHS = ['src/d3d11/d3d11_context_impl.cpp', 'src/d3d11/d3d11_context_imm.cpp',
         'src/d3d11/d3d11_context_def.cpp', 'src/d3d11/d3d11_context_state.hpp',
         'src/dxmt/dxmt_context.hpp', 'src/dxmt/dxmt_context.cpp', 'src/dxmt/dxmt_binding_set.hpp']
CASES = ['layout-repeat', 'shader-repeat', 'vb-repeat', 'lifecycle', 'discard', 'controls', 'actual-callers']
NEGATIVES = {
    'layout-repeat': 'invalidations == 0',
    'shader-repeat': 'invalidations == 0',
    'vb-repeat': 'ctx.emitted == before && ctx.validations == 101',
    'lifecycle': 'ctx.emitted == before',
    'discard': 'ctx.emitted == before && ctx.state_.InputAssembler.VertexBuffers.test_dirty(0)',
    'actual-callers': 'ctx.emitted == before',
}


def block(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    depth, cursor = 1, opening + 1
    while depth:
        depth += (text[cursor] == '{') - (text[cursor] == '}')
        cursor += 1
    return text[start:cursor]


def generate(contents):
    impl, imm, deferred, state, native, cpp, _ = (contents[path] for path in PATHS)
    vb = block(state, 'struct VERTEX_BUFFER_B') + ';\n' + block(state, 'template <> struct redundant_binding_trait<VERTEX_BUFFER_B>') + ';'
    setters = [block(impl, marker).replace(' override', '') for marker in [
        'void\n  STDMETHODCALLTYPE\n  IASetInputLayout(',
        'void\n  STDMETHODCALLTYPE\n  IASetVertexBuffers(',
        'template <PipelineStage stage, typename IShader>\n  void\n  SetShader(',
        'void\n  SetVertexBuffers(', 'void\n  UpdateVertexBuffer()',
        'void\n  InvalidateRenderPipeline()', 'void\n  InvalidateComputePipeline()',
        'bool\n  InvalidateCurrentPass(', 'void ResetEncodingContextState()',
        'void ResetD3D11ContextState()', 'void\n  STDMETHODCALLTYPE\n  ClearState()']]
    setters.append('void *\n' + block(imm, 'MapDynamicBuffer(Rc<DynamicBuffer> &dynamic,'))
    setters.append(block(imm, 'void\n  STDMETHODCALLTYPE\n  ExecuteCommandList(').replace(' override', ''))
    setters.append(block(deferred, 'HRESULT \n  STDMETHODCALLTYPE\n  FinishCommandList(').replace(' override', ''))
    native_methods = []
    for name in ['bindVertexBuffer', 'bindVertexBufferOffset']:
        method = block(native, 'void\n  ' + name + '(').replace(name + '(', name + 'Impl(', 1)
        native_methods.append(method)
    native_methods += [block(native, 'template<PipelineStage stage = PipelineStage::Compute>\n  std::pair<BufferAllocation *, uint64_t>\n  access(')]
    encoder = block(cpp, 'template <PipelineKind kind>\nvoid\nArgumentEncodingContext::encodeVertexBuffers(')
    native_methods.append(encoder.replace('ArgumentEncodingContext::', '', 1))
    restore = block(impl, 'for (const auto &[slot, element] : state_.InputAssembler.VertexBuffers)')
    reopen = block(impl, 'bool\n  SwitchToRenderEncoder()')
    reopen = reopen[reopen.index('{') + 1:reopen.index('    dirty_state.set(')]
    # Both actual command-list callers select the exact same existing restore
    # helpers; execute their state-management boundary portions in the fixture.
    execute = imm[imm.index('    if (RestoreContextState)'):imm.index('\n  }', imm.index('    if (RestoreContextState)'))]
    finish = deferred[deferred.index('    if (RestoreDeferredContextState)'):deferred.index('\n    return S_OK;', deferred.index('    if (RestoreDeferredContextState)'))]
    discard = block(imm, 'case D3D11_MAP_WRITE_DISCARD:')
    if 'state_.InputAssembler.VertexBuffers.set_dirty();' not in block(deferred, 'case D3D11_MAP_WRITE_DISCARD:'):
        raise RuntimeError('Deferred DISCARD no longer independently dirties vertex buffers')
    fixture = (HERE / 'fixture.cpp').read_text()
    values = {'NATIVE_METHODS': '\n'.join(native_methods), 'CONTEXT_METHODS': '\n'.join(setters),
              'COMMAND_STATE': block(impl, 'enum class CommandBufferState') + ';',
              'RESTORE_VERTEX': restore, 'REOPEN_DIRTY': reopen,
              'EXECUTE_RESTORE': execute, 'FINISH_RESTORE': finish, 'DISCARD_CASE': discard}
    for name, value in values.items():
        marker = '/* ' + name + ' */'
        if fixture.count(marker) != 1:
            raise RuntimeError(f'Fixture marker {name} changed')
        fixture = fixture.replace(marker, value)
    return fixture, 'namespace dxmt {\n' + vb + '\n}\n'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--arch', choices=('native', 'x86_64'), default='native')
    p.add_argument('--sanitize', action='store_true')
    p.add_argument('--baseline-ref')
    args = p.parse_args()
    contents = {path: (ROOT / path).read_text() for path in PATHS}
    if args.baseline_ref:
        contents[PATHS[0]] = subprocess.check_output(['git', '-C', str(ROOT), 'show', f'{args.baseline_ref}:{PATHS[0]}'], text=True)
    fixture, vb = generate(contents)
    with tempfile.TemporaryDirectory(prefix='dxmt-exact-setters-') as directory:
        scratch = Path(directory)
        (scratch / 'fixture.cpp').write_text(fixture)
        (scratch / 'production-vb.hpp').write_text(vb)
        (scratch / 'unknwn.h').write_text('#pragma once\nstruct IUnknown { virtual long QueryInterface(int, void **) = 0; virtual unsigned AddRef() = 0; virtual unsigned Release() = 0; };\n#define SUCCEEDED(value) ((value)>=0)\n#define __uuidof(...) 0\n')
        command = [shutil.which('clang++') or 'c++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                   '-Wno-unused-parameter', '-Wno-missing-field-initializers', '-O1', '-g',
                   '-I', str(scratch), '-I', str(ROOT / 'src/util'), '-I', str(ROOT / 'src/dxmt'),
                   str(scratch / 'fixture.cpp'), '-o', str(scratch / 'fixture')]
        if args.arch == 'x86_64': command += ['-arch', 'x86_64']
        if args.sanitize: command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        subprocess.run(command, check=True, timeout=90)
        run = [str(scratch / 'fixture')]
        if args.arch == 'x86_64' and platform.machine() == 'arm64': run = ['/usr/bin/arch', '-x86_64'] + run
        for case in CASES:
            result = subprocess.run(run + [case], timeout=20, capture_output=True, text=True)
            print(result.stdout, end=''); print(result.stderr, end='')
            expected = 2 if args.baseline_ref and case in NEGATIVES else 0
            if result.returncode != expected: raise RuntimeError(f'{case}: exit {result.returncode}, expected {expected}')
            if expected == 2 and result.stderr != f'FAIL {case}: {NEGATIVES[case]}\n': raise RuntimeError(f'{case}: unrelated baseline failure')
    print(json.dumps({'arch': args.arch, 'sanitize': args.sanitize, 'baseline_ref': args.baseline_ref,
        'groups': len(CASES), 'expected_negatives': len(NEGATIVES) if args.baseline_ref else 0,
        'source_sha256': {path: hashlib.sha256(value.encode()).hexdigest() for path, value in contents.items()}}, sort_keys=True))


if __name__ == '__main__': main()
