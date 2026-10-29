#!/usr/bin/env python3
"""Production context setters and binding/dirty/rename seams; no native GPU runtime."""
import argparse
import hashlib
import json
import os
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
         'src/dxmt/dxmt_context.hpp', 'src/dxmt/dxmt_context.cpp', 'src/dxmt/dxmt_binding_set.hpp',
         'src/dxmt/dxmt_command_queue.hpp']
CASES = ['index-repeat', 'index-data', 'index-lifecycle', 'index-discard', 'layout-repeat', 'shader-repeat', 'vb-repeat', 'lifecycle', 'discard', 'controls', 'actual-callers', 'queue-failure']
NEGATIVES = {
    'index-repeat': 'ctx.emitted == before && ctx.validations == 101',
    'index-discard': 'ctx.enc.index_binds == binds',
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
    impl, imm, deferred, state, native, cpp, _ = (contents[path] for path in PATHS[:7])
    vb = block(state, 'struct VERTEX_BUFFER_B') + ';\n' + block(state, 'template <> struct redundant_binding_trait<VERTEX_BUFFER_B>') + ';'
    setters = [block(impl, marker).replace(' override', '') for marker in [
        'void\n  STDMETHODCALLTYPE\n  IASetIndexBuffer(', 'void\n  STDMETHODCALLTYPE\n  IAGetIndexBuffer(',
        'void\n  STDMETHODCALLTYPE\n  DrawIndexed(',
        'void\n  STDMETHODCALLTYPE\n  IASetInputLayout(',
        'void\n  STDMETHODCALLTYPE\n  IASetVertexBuffers(',
        'template <PipelineStage stage, typename IShader>\n  void\n  SetShader(',
        'void\n  SetVertexBuffers(', 'void\n  UpdateVertexBuffer()',
        'void\n  InvalidateRenderPipeline()', 'void\n  InvalidateComputePipeline()',
        'bool\n  InvalidateCurrentPass(', 'void ResetEncodingContextState()',
        'void ResetD3D11ContextState()', 'void\n  STDMETHODCALLTYPE\n  ClearState()']]
    if 'bool CommandQueueFailed() const' in impl:
        setters.append(block(impl, 'bool CommandQueueFailed() const'))
    setters.append('void *\n' + block(imm, 'MapDynamicBuffer(Rc<DynamicBuffer> &dynamic,'))
    setters.append(block(imm, 'void\n  STDMETHODCALLTYPE\n  ExecuteCommandList(').replace(' override', ''))
    setters.append(block(deferred, 'HRESULT \n  STDMETHODCALLTYPE\n  FinishCommandList(').replace(' override', ''))
    native_methods = []
    for name in ['bindVertexBuffer', 'bindVertexBufferOffset', 'bindIndexBuffer']:
        method = block(native, 'void\n  ' + name + '(').replace(name + '(', name + 'Impl(', 1)
        native_methods.append(method)
    native_methods.append(block(native, 'std::pair<WMT::Buffer, uint64_t>\n  currentIndexBuffer()'))
    native_methods += [block(native, 'template<PipelineStage stage = PipelineStage::Compute>\n  std::pair<BufferAllocation *, uint64_t>\n  access(')]
    encoder = block(cpp, 'template <PipelineKind kind>\nvoid\nArgumentEncodingContext::encodeVertexBuffers(')
    native_methods.append(encoder.replace('ArgumentEncodingContext::', '', 1))
    restore = block(impl, 'for (const auto &[slot, element] : state_.InputAssembler.VertexBuffers)')
    restore += '\n' + block(block(impl, 'void\n  RestoreEncodingContextState()'), 'if (state_.InputAssembler.IndexBuffer) {')
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
    forward = '\n'.join(block(source, 'template<typename Object> Rc<Object> forward_rc(').replace('forward_rc(', name + '(', 1) for source, name in [(imm, 'immediate_forward_rc'), (deferred, 'deferred_forward_rc')])
    queue = contents[PATHS[7]]
    values = {'MOVEONLY_LIST': 'namespace dxmt {\n' + block(queue, 'template <typename T> class moveonly_list') + ';\n}',
              'QUEUE_FAILURE_API': block(queue, 'enum class Failure : int32_t') + ';\n' + block(queue, 'int32_t FailureReason() const'),
              'FORWARD_RC': forward, 'NATIVE_METHODS': '\n'.join(native_methods), 'CONTEXT_METHODS': '\n'.join(setters),
              'COMMAND_STATE': block(impl, 'enum class CommandBufferState') + ';',
              'RESTORE_VERTEX': restore, 'REOPEN_DIRTY': reopen,
              'EXECUTE_RESTORE': execute, 'FINISH_RESTORE': finish, 'DISCARD_CASE': discard}
    for name, value in values.items():
        marker = '/* ' + name + ' */'
        if fixture.count(marker) != 1:
            raise RuntimeError(f'Fixture marker {name} changed')
        fixture = fixture.replace(marker, value)
    return fixture, 'namespace dxmt {\n' + vb + '\n}\n'


MUTATIONS = {
    'queue-admission': ('if (cmd_queue.FailureReason()) return;', '(void)0;', 'queue-failure'),
    'queue-clone-failure': ('cmd_queue.Fail(CommandQueue::Failure::OutOfMemory);', '(void)0;', 'queue-failure'),
    'queue-render-failure': ('if (CommandQueueFailed()) return false;', '(void)0;', 'queue-failure'),
    'queue-map-publication': ('if (auto failure = cmd_queue.FailureReason()) return failure;', '(void)0;', 'queue-failure'),
    'identity-always': ('if (state_.InputAssembler.IndexBuffer.ptr() != pBuffer)', 'if (true)', 'index-repeat'),
    'skip-hazard': ('ValidateIAHazard(pBuffer)', 'true', 'index-data'),
    'drop-format': ('state_.InputAssembler.IndexBufferFormat = Format;', '(void)Format;', 'index-data'),
    'drop-offset': ('state_.InputAssembler.IndexBufferOffset = Offset;', '(void)Offset;', 'index-data'),
    'retain-null': ('EmitST([](ArgumentEncodingContext &enc) { enc.bindIndexBuffer({}); });', '(void)0;', 'index-data'),
    'drop-restore': ('enc.bindIndexBuffer(forward_rc(buffer));', '(void)buffer;', 'index-lifecycle'),
    'drop-suboffset': ('return {allocation, allocation->currentSuballocationOffset()};', 'return {allocation, 0};', 'index-data'),
}

def mutate(contents, name):
    old, new, _ = MUTATIONS[name]
    path = PATHS[4] if name == 'drop-suboffset' else PATHS[1] if name in ('queue-admission', 'queue-clone-failure', 'queue-map-publication') else PATHS[0]
    text = contents[path]
    if name.startswith('queue-'):
        marker = 'case D3D11_MAP_WRITE_DISCARD:' if name == 'queue-map-publication' else 'bool\n  SwitchToRenderEncoder()' if name == 'queue-render-failure' else 'void\n  STDMETHODCALLTYPE\n  ExecuteCommandList('
        section = block(text, marker)
        if section.count(old) != 1: raise RuntimeError('Queue mutation binding changed')
        contents[path] = text.replace(section, section.replace(old, new))
    elif name == 'drop-restore':
        section = block(text, 'void\n  RestoreEncodingContextState()')
        if section.count(old) != 1: raise RuntimeError('Mutation binding changed')
        contents[path] = text.replace(section, section.replace(old, new))
    elif name != 'drop-suboffset':
        section = block(text, 'void\n  STDMETHODCALLTYPE\n  IASetIndexBuffer(')
        if section.count(old) != 1: raise RuntimeError('Mutation binding changed')
        contents[path] = text.replace(section, section.replace(old, new))
    else:
        if text.count(old) != 1: raise RuntimeError('Mutation binding changed')
        contents[path] = text.replace(old, new)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--arch', choices=('native', 'x86_64'), default='native')
    p.add_argument('--sanitize', action='store_true')
    p.add_argument('--baseline-ref')
    p.add_argument('--index-only', action='store_true')
    p.add_argument('--mutation', choices=tuple(MUTATIONS))
    args = p.parse_args()
    contents = {path: (ROOT / path).read_text() for path in PATHS}
    if args.baseline_ref:
        contents[PATHS[0]] = subprocess.check_output(['git', '-C', str(ROOT), 'show', f'{args.baseline_ref}:{PATHS[0]}'], text=True)
    if args.mutation:
        if args.baseline_ref: p.error('mutation and baseline are exclusive')
        args.index_only = True
        mutate(contents, args.mutation)
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
        mutation_observed = False
        cases = ['queue-failure'] if args.mutation and args.mutation.startswith('queue-') else CASES[:4] if args.index_only else CASES
        for case in cases:
            result = subprocess.run(run + [case], timeout=20, capture_output=True, text=True, env={**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1'})
            print(result.stdout, end=''); print(result.stderr, end='')
            negative = {'index-repeat': 'ctx.emitted == before && ctx.validations == 101', 'index-discard': 'ctx.enc.index_binds == binds'} if args.index_only else NEGATIVES
            expected = 2 if args.baseline_ref and case in negative else 0
            if args.mutation:
                if result.returncode not in (0, 2) or (result.returncode == 2 and not result.stderr.startswith('FAIL ')) or 'Sanitizer' in result.stderr:
                    raise RuntimeError('Mutation crashed or failed outside an assertion')
                if case == MUTATIONS[args.mutation][2]:
                    if result.returncode != 2: raise RuntimeError('Mutation escaped its connected oracle')
                    mutation_observed = True
                continue
            if result.returncode != expected: raise RuntimeError(f'{case}: exit {result.returncode}, expected {expected}')
            if expected == 2 and result.stderr != f'FAIL {case}: {negative[case]}\n': raise RuntimeError(f'{case}: unrelated baseline failure')
        if args.mutation and not mutation_observed: raise RuntimeError('Mutation target not exercised')
    print(json.dumps({'mutation': args.mutation, 'arch': args.arch, 'sanitize': args.sanitize, 'baseline_ref': args.baseline_ref,
        'fixture_sha256': hashlib.sha256(fixture.encode()).hexdigest(),
        'test_sources_sha256': {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in (HERE / 'run.py', HERE / 'fixture.cpp')},
        'groups': len(cases), 'expected_negatives': len(negative) if args.baseline_ref else 0,
        'source_sha256': {path: hashlib.sha256(value.encode()).hexdigest() for path, value in {**contents, **{path: (ROOT / path).read_text() for path in ('src/util/com/com_pointer.hpp', 'src/util/rc/util_rc_ptr.hpp', 'src/util/util_bit.hpp')}}.items()}}, sort_keys=True))


if __name__ == '__main__': main()
