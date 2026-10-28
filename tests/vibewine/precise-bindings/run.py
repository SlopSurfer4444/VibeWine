#!/usr/bin/env python3
"""Exercise the production DISCARD branches, rename methods and CB table path.

All generated files are private temporary files. No Wine, Metal, game, runtime,
Git index or installed files are changed. Negative variants must fail naturally.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
BASELINE = 'e11476e33117b454bac60b997532f9823e34855b'
PATHS = ['src/d3d11/d3d11_context_imm.cpp', 'src/d3d11/d3d11_context_def.cpp',
         'src/d3d11/d3d11_context_impl.cpp', 'src/d3d11/d3d11_context_state.hpp',
         'src/d3d11/d3d11_precise_bindings.hpp', 'src/dxmt/dxmt_context.cpp',
         'src/dxmt/dxmt_context.hpp', 'src/dxmt/dxmt_dynamic.hpp',
         'src/dxmt/dxmt_dynamic.cpp', 'src/dxmt/dxmt_binding_set.hpp',
         'src/util/util_bit.hpp', 'src/util/rc/util_rc_ptr.hpp',
         'src/airconv/airconv_public.h', 'src/dxmt/dxmt_buffer.hpp']


def block(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    depth, cursor = 1, opening + 1
    while depth:
        depth += (text[cursor] == '{') - (text[cursor] == '}')
        cursor += 1
    return text[start:cursor]


def sources(source, ref=None):
    result = {}
    for path in PATHS:
        if ref:
            obj = subprocess.run(['git', '-C', str(source), 'show', f'{ref}:{path}'],
                                 text=True, capture_output=True, check=False)
            if obj.returncode and path.endswith('d3d11_precise_bindings.hpp'):
                result[path] = ''  # Historical source has no helper and no call.
            else:
                obj.check_returncode()
                result[path] = obj.stdout
        else:
            result[path] = (source / path).read_text()
    return result


def generate(contents):
    def get(suffix):
        return next(text for path, text in contents.items() if path.endswith(suffix))

    imm, deferred, impl = (get(name) for name in ['d3d11_context_imm.cpp', 'd3d11_context_def.cpp',
                                                'd3d11_context_impl.cpp'])
    dynamic = get('dxmt_dynamic.cpp')
    destructor = (block(dynamic, 'DynamicBuffer::~DynamicBuffer()')
                  if 'DynamicBuffer::~DynamicBuffer()' in dynamic else '')
    replacements = {
        'HELPER': get('d3d11_precise_bindings.hpp').replace('#pragma once', ''),
        'DYNAMIC_CLASS': block(get('dxmt_dynamic.hpp'), 'class DynamicBuffer {') + ';',
        'DYNAMIC_METHODS': destructor + '\n' + '\n'.join(block(dynamic, marker) for marker in [
            'template <typename Queue>\nvoid\ntrimCompletedDynamicAllocations(',
            'DynamicBuffer::DynamicBuffer(', 'void\nDynamicBuffer::incRef()',
            'void\nDynamicBuffer::decRef()', 'Rc<BufferAllocation>\nDynamicBuffer::allocate(',
            'void\nDynamicBuffer::updateImmediateName(', 'void\nDynamicBuffer::recycle(',
            'uint32_t\nDynamicBuffer::nextSuballocation()']),
        'CB_BINDING': block(get('d3d11_context_state.hpp'), 'struct CONSTANT_BUFFER_B {') + ';\n' +
                      block(get('d3d11_context_state.hpp'),
                            'template <> struct redundant_binding_trait<CONSTANT_BUFFER_B> {') + ';',
        'OTHER_BINDINGS': '\n'.join(block(get('d3d11_context_state.hpp'), marker) + ';' for marker in [
            'struct SRV_B {', 'template <> struct redundant_binding_trait<SRV_B> {',
            'struct VERTEX_BUFFER_B {', 'template <> struct redundant_binding_trait<VERTEX_BUFFER_B> {']),
        'BUFFER_SLICE': block(get('dxmt_buffer.hpp'), 'struct BufferSlice {') + ';',
        'BIND_SRV': block(get('dxmt_context.hpp'), '  template <PipelineStage stage>\n  void\n  bindBuffer('),
        'BIND_VB': block(get('dxmt_context.hpp'), '  void\n  bindVertexBuffer('),
        'ENCODE_VB': block(get('dxmt_context.cpp'), 'template <PipelineKind kind>\nvoid\nArgumentEncodingContext::encodeVertexBuffers('),
        'SRV_CASE': block(block(get('dxmt_context.cpp'), 'template <PipelineStage stage, PipelineKind kind>\nvoid\nArgumentEncodingContext::encodeShaderResources('), '    case SM50BindingType::SRV:'),
        'SRV_TAIL': block(get('dxmt_context.cpp'), 'template <PipelineStage stage, PipelineKind kind>\nvoid\nArgumentEncodingContext::encodeShaderResources(').split('  if constexpr (stage == PipelineStage::Compute)', 1)[1].rsplit('}', 1)[0],
        'ALIGN': block(get('dxmt_context.hpp'), 'inline std::size_t\nalign_forward_adjustment('),
        'BIND_CB': block(get('dxmt_context.hpp'),
                         '  template <PipelineStage stage>\n  void\n  bindConstantBuffer('),
        'BIND_CB_OFFSET': block(get('dxmt_context.hpp'),
                                '  template <PipelineStage stage>\n  void\n  bindConstantBufferOffset('),
        'ENCODE_CB': block(get('dxmt_context.cpp'),
                           'template <PipelineStage stage, PipelineKind kind>\nvoid\nArgumentEncodingContext::encodeConstantBuffers('),
        'ALLOCATE_ARGUMENTS': block(impl, '  uint64_t PreAllocateArgumentBuffer('),
        'UPLOAD': block(impl, '  template <PipelineStage stage, PipelineKind kind>\n  void\n  UploadShaderStageResourceBinding()'),
        'SET_CB': block(impl, '  template <PipelineStage Stage>\n  void\n  SetConstantBuffer('),
        'USED_DYNAMIC': block(impl, 'struct used_dynamic_buffer {') + ';',
        'IMM_DYNAMIC_MAP': block(imm, '  void *\n  MapDynamicBuffer('),
        'DEF_DYNAMIC_MAP': block(deferred, '  void *\n  MapDynamicBuffer('),
        'IMM_MAP_BRANCH': block(imm, '    if (auto dynamic = GetDynamicBuffer(pResource, &buffer_length, &bind_flag))'),
        'DEF_MAP_BRANCH': block(deferred, '    if (auto dynamic = GetDynamicBuffer(pResource, &buffer_length, &bind_flag))'),
        'EXECUTE_DYNAMIC': block(imm, '    for (const auto &used_dynamic : cmdlist->used_dynamic_buffers)'),
    }
    generated = (HERE / 'fixture.cpp').read_text()
    for marker, replacement in replacements.items():
        token = '// ' + marker + '\n'
        assert generated.count(token) == 1, marker
        generated = generated.replace(token, replacement + '\n')
    return generated


def mutate(contents, kind):
    contents = dict(contents)
    helper = 'src/d3d11/d3d11_precise_bindings.hpp'
    if kind in ('missing-dependent', 'unrelated', 'first-alias', 'broad-slot'):
        text = contents[helper]
        if kind == 'missing-dependent':
            text = text.replace('== discarded)', '== nullptr)')
        elif kind == 'unrelated':
            text = text.replace('== discarded)', '!= discarded)')
        elif kind == 'first-alias':
            text = text.replace('stage.ConstantBuffers.set_dirty(slot);',
                                '{ stage.ConstantBuffers.set_dirty(slot); return; }')
        else:
            text = text.replace('stage.ConstantBuffers.set_dirty(slot);', 'stage.ConstantBuffers.set_dirty();')
        assert text != contents[helper]
        contents[helper] = text
    elif kind in ('immediate-callsite', 'deferred-callsite'):
        key = 'src/d3d11/d3d11_context_' + ('imm.cpp' if kind.startswith('immediate') else 'def.cpp')
        before = 'InvalidateDiscardedConstantBuffers(state_.ShaderStages, dynamic->buffer);'
        assert contents[key].count(before) == 1
        contents[key] = contents[key].replace(before, '(void)dynamic;')
    elif kind == 'residency-drop':
        key = 'src/dxmt/dxmt_context.cpp'
        before = 'makeResident<stage, kind>(argbuf.ptr());'
        assert contents[key].count(before) == 1
        contents[key] = contents[key].replace(before, '(void)argbuf;')
    elif kind == 'rename-order':
        key = 'src/d3d11/d3d11_context_imm.cpp'
        before = 'auto _ = buffer->rename(forward_rc(allocation));'
        assert contents[key].count(before) == 1
        contents[key] = contents[key].replace(before, '(void)buffer;')
    elif kind == 'suballocation-order':
        key = 'src/d3d11/d3d11_context_imm.cpp'
        before = 'allocation->useSuballocation(next_sub);'
        assert contents[key].count(before) == 1
        contents[key] = contents[key].replace(before, '(void)allocation; (void)next_sub;')
    elif kind.startswith(('vb-', 'srv-')) and kind != 'srv-residency-drop':
        token = 'VertexBuffers' if kind.startswith('vb-') else 'ShaderResources'
        text = contents[helper]
        marker = 'InvalidateDiscarded' + token + '('
        function = block(text, marker)
        if kind.endswith('missing'):
            changed = function.replace('== discarded)', '== nullptr)')
        elif kind.endswith('broad'):
            changed = function.replace('set_dirty(slot)', 'set_dirty()')
        elif kind.endswith('first-alias'):
            changed = function.replace('stage.SRVs.set_dirty(slot);', '{ stage.SRVs.set_dirty(slot); return; }')
        elif kind.endswith('upper-qword'):
            changed = function.replace('== discarded)', '== discarded && slot < 64)')
        else:
            raise ValueError(kind)
        assert function != changed
        contents[helper] = text.replace(function, changed)
    elif kind == 'srv-residency-drop':
        key = 'src/dxmt/dxmt_context.cpp'
        before = 'makeResident<stage, kind>(srv.buffer.ptr());'
        assert contents[key].count(before) == 1
        contents[key] = contents[key].replace(before, '(void)srv;')
    elif kind in ('immediate-resource-callsite', 'deferred-resource-callsite'):
        key = 'src/d3d11/d3d11_context_' + ('imm.cpp' if kind.startswith('immediate') else 'def.cpp')
        for before in ('InvalidateDiscardedVertexBuffers(state_.InputAssembler.VertexBuffers, dynamic->buffer);',
                       'InvalidateDiscardedShaderResources(state_.ShaderStages, dynamic->buffer);'):
            assert contents[key].count(before) == 1
            contents[key] = contents[key].replace(before, '(void)dynamic;')
    else:
        raise ValueError(kind)
    return contents


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, default=REPO)
    parser.add_argument('--baseline-ref', default=BASELINE)
    parser.add_argument('--arch', choices=('native', 'x86_64'), default='native')
    args = parser.parse_args()
    current = sources(args.source_dir)
    compiler = shutil.which('clang++') or shutil.which('c++')
    if not compiler:
        parser.error('C++ compiler unavailable')
    variants = [('default', current, [], False, True),
                ('explicit-off', current, ['-DDXMT_PRECISE_BINDINGS=0'], False, True),
                ('enabled', current, ['-DDXMT_PRECISE_BINDINGS=1'], True, True),
                ('baseline', sources(args.source_dir, args.baseline_ref),
                 ['-DDXMT_PRECISE_BINDINGS=1'], True, False),
                ('resource-baseline', sources(args.source_dir, '8cf59a015b88361ed6f6320eb8a781c68b3b36c1'),
                 ['-DDXMT_PRECISE_BINDINGS=1'], True, False)]
    variants += [(kind, mutate(current, kind), ['-DDXMT_PRECISE_BINDINGS=1'], True, False)
                 for kind in ('missing-dependent', 'unrelated', 'first-alias', 'broad-slot',
                              'immediate-callsite', 'deferred-callsite', 'rename-order', 'suballocation-order',
                              'vb-missing', 'vb-broad', 'srv-missing', 'srv-broad', 'srv-first-alias',
                              'srv-upper-qword', 'srv-residency-drop',
                              'immediate-resource-callsite', 'deferred-resource-callsite')]
    variants += [('residency-drop', mutate(current, 'residency-drop'), ['-DDXMT_PRECISE_BINDINGS=1'], True, False)]
    results = []
    with tempfile.TemporaryDirectory(prefix='dxmt-precise-bindings-') as directory:
        scratch = Path(directory)
        for name, contents, defines, precise, accept in variants:
            cpp, binary = scratch / (name + '.cpp'), scratch / name
            cpp.write_text(generate(contents))
            command = [compiler, '-std=c++20', '-O0', '-gline-tables-only', '-Wall', '-Wextra', '-Werror',
                       '-Wno-unused-variable', '-Wno-unused-parameter', '-Wno-missing-field-initializers',
                       '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                       '-fno-omit-frame-pointer', '-pthread', f'-DEXPECT_PRECISE={int(precise)}',
                       *defines, '-I', str(args.source_dir / 'src/util'),
                       '-I', str(args.source_dir / 'src/dxmt'),
                       '-I', str(args.source_dir / 'src/airconv'), str(cpp), '-o', str(binary)]
            if args.arch == 'x86_64':
                command += ['-arch', 'x86_64']
            subprocess.run(command, check=True, timeout=60)
            run = [str(binary)]
            if args.arch == 'x86_64' and platform.machine() == 'arm64':
                run = ['/usr/bin/arch', '-x86_64', *run]
            result = subprocess.run(run, capture_output=True, text=True, timeout=20)
            output = result.stdout + result.stderr
            assert (result.returncode == 0) == accept, f'{name}: unexpected exit {result.returncode}\n{output}'
            if not accept:
                assert result.returncode == 1 and 'CONTRACT:' in output, f'{name}: not a behavioral rejection\n{output}'
                assert 'ERROR: AddressSanitizer' not in output and 'runtime error:' not in output
            results.append({'variant': name, 'accepted': accept, 'exit': result.returncode, 'output': output.strip()})
            print(json.dumps(results[-1]), flush=True)
    print(json.dumps({'scope': 'Production dynamic-buffer Map, exact CB/VB/SRV invalidation, rename, CB/SRV upload and buffer encoding with CPU adapters',
                      'arch': args.arch, 'sanitizers': ['address', 'undefined'], 'baseline_ref': args.baseline_ref,
                      'sources_sha256': {path: hashlib.sha256(text.encode()).hexdigest()
                                         for path, text in current.items()},
                      'fixture_sha256': hashlib.sha256((HERE / 'fixture.cpp').read_bytes()).hexdigest(),
                      'runner_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                      'results': results}, indent=2))


if __name__ == '__main__':
    main()
