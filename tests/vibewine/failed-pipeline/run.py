#!/usr/bin/env python3
"""Run actual production scheduling lambdas and cancellation helpers on CPU seams."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def balanced(text, start, opening='(', closing=')'):
    # Comments and literals can contain delimiters (including warning messages).
    tokens = re.finditer(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[(){}]',
                         text[start:], re.S)
    depth = 0
    for token in tokens:
        value = token.group()
        if value == opening:
            depth += 1
        elif value == closing:
            depth -= 1
            if not depth:
                return start + token.end()
    raise ValueError('unbalanced source')


def method(text, name):
    found = re.search(r'^  ' + re.escape(name) + r'\(', text, re.M)
    if not found:
        raise ValueError('missing method: ' + name)
    start = found.start()
    brace = text.index('{', balanced(text, text.index('(', start)))
    end = balanced(text, brace, '{', '}')
    return text[start:end]


def emit_calls(text):
    result = []
    for found in re.finditer(r'\b(?:EmitOP|EmitST|EmitDrawOP|EmitDispatchOP)\(', text):
        # Each scheduling region here has no nested Emit* call.
        end = balanced(text, text.index('(', found.start()))
        if text[end:end + 1] != ';':
            raise ValueError('unexpected Emit terminator')
        result.append(text[found.start():end + 1])
    return result


def attributed(code, path, full):
    pos = full.find(code)
    if pos < 0:
        raise ValueError('extraction lost source identity')
    return '#line %d "%s"\n%s\n' % (full[:pos].count('\n') + 1, path, code)


def generate(contents, mutation):
    path = 'src/d3d11/d3d11_context_impl.cpp'
    full = contents[path]
    draw_names = ['Draw', 'DrawIndexed', 'DrawInstanced', 'DrawIndexedInstanced',
                  'TessellationDraw', 'TessellationDrawIndexed', 'GeometryDraw',
                  'GeometryDrawIndexed', 'DrawIndexedInstancedIndirect', 'DrawInstancedIndirect',
                  'GeometryDrawIndirect', 'GeometryDrawIndexedIndirect', 'TessellationDrawIndirect',
                  'TessellationDrawIndexedIndirect', 'Dispatch', 'DispatchIndirect']
    region = full.split('#pragma region DrawCall', 1)[1].split('#pragma endregion', 1)[0]
    if len(re.findall(r'\b(?:EmitOP|EmitDrawOP|EmitDispatchOP)\(', region)) != len(draw_names):
        raise ValueError('draw/dispatch route inventory changed: update fixture coverage')
    generated = []
    helpers = ''
    if '  EmitDrawOP(' in full:
        for name in ['EmitDrawOP', 'EmitDispatchOP']:
            helpers += 'template <CommandWithContext<ArgumentEncodingContext> cmd> void\n'
            helpers += attributed(method(full, name), path, full)
        helpers += 'static bool\n' + attributed(method(full, 'TrackRenderPipelineState'), path, full)
    if mutation == 'unguarded':
        helpers, count = re.subn(r'      if \(enc\.current(?:Render|Compute)Encoder\(\)->pipeline_failed\)\n        return;\n', '', helpers)
        if count != 2:
            raise ValueError('unguarded mutation must change two production guards')
    if mutation == 'sticky':
        helpers, count = re.subn(r'pipeline_failed = pso == nullptr;', 'pipeline_failed |= pso == nullptr;', helpers)
        if count != 1:
            raise ValueError('sticky mutation must change render resolution')
    generated.append(helpers)
    for index, name in enumerate(draw_names):
        calls = emit_calls(method(full, name))
        if len(calls) != 1:
            raise ValueError('expected one scheduling call in ' + name)
        generated.append('void route_%d() {\n route_locals;\n' % index + attributed(calls[0], path, full) + '}\n')
    pso_names = ['FinalizeCurrentRenderPipeline', 'FinalizeGeometryRenderPipeline',
                 'FinalizeTessellationRenderPipeline', 'FinalizeCurrentComputePipeline']
    for index, name in enumerate(pso_names):
        calls = [call for call in emit_calls(method(full, name)) if 'pso = std::move(pipeline)' in call]
        if len(calls) != 1:
            raise ValueError('expected PSO scheduling call in ' + name)
        call = calls[0]
        if mutation == 'sticky' and index == 3:
            call = call.replace('pipeline_failed = ComputePipeline.PipelineState == nullptr;',
                                'pipeline_failed |= ComputePipeline.PipelineState == nullptr;')
        if mutation == 'early-stage' and index == 2:
            flag = '      render_encoder->use_tessellation = 1;\n'
            if call.count(flag) != 1:
                raise ValueError('stage mutation missing source flag')
            call = call.replace(flag, '').replace('      if (!TrackRenderPipelineState(', flag + '      if (!TrackRenderPipelineState(')
        # Captures and bodies are otherwise byte-exact. Attribute original source.
        generated.append('void pso_%d(Pipeline *pipeline) { Shader shader; Shader *CS = &shader;\n' % index)
        generated.append('#line %d "%s"\n%s\n}\n' % (full[:full.index(calls[0])].count('\n') + 1, path, call))
    upload = method(full, 'UploadShaderStageResourceBinding')
    calls = emit_calls(upload)
    if len(calls) != 2:
        raise ValueError('unexpected resource upload scheduling shape')
    generated.append('template <PipelineStage stage, PipelineKind kind> void resource_uploads() {\n'
                     'Shader managed; Shader *managed_shader = &managed;\n'
                     'const Reflection *reflection = &managed.reflection(); uint64_t offset = 0;\n')
    generated.extend(attributed(call, path, full) for call in calls)
    generated.append('}\n')
    for name, function in [('UpdateVertexBuffer', 'vertex_uploads'), ('UpdateSOTargets', 'so_uploads')]:
        calls = emit_calls(method(full, name))
        generated.append('void ' + function + '() { uint32_t slot_mask = 1; uint64_t offset = 0;\n')
        generated.extend(attributed(call, path, full) for call in calls)
        generated.append('}\n')
    # Resource encoders themselves are unchanged in this repair. Refuse silent
    # cancellation inserted below scheduling, which these mocked encoders cannot see.
    bindings = contents['src/dxmt/dxmt_context.cpp']
    for match in re.finditer(r'ArgumentEncodingContext::encode(?:VertexBuffers|ConstantBuffers|ShaderResources|StreamOutputBuffers)[^;]*?\{', bindings):
        start = bindings.index('{', match.start())
        body = bindings[start:balanced(bindings, start, '{', '}')]
        if 'pipeline_failed' in body:
            raise ValueError('binding encoder gained failure guard: extend fixture before admission')
    return ''.join(generated), draw_names


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-ref', help='Read production bytes from this Git ref')
    parser.add_argument('--expect-baseline-failure', action='store_true')
    parser.add_argument('--sanitize', choices=['none', 'address'], default='address')
    parser.add_argument('--mutation', choices=['unguarded', 'sticky', 'early-stage'])
    parser.add_argument('--keep', type=Path, help='Save bounded generated source, logs and receipt; omit binary')
    args = parser.parse_args()
    if args.expect_baseline_failure and not args.source_ref:
        parser.error('baseline check requires --source-ref')
    if args.keep and (args.keep.exists() or args.keep.is_symlink()):
        parser.error('--keep destination must not exist')
    tests = Path(__file__).resolve().parent
    root = tests.parents[2]
    paths = ['src/d3d11/d3d11_context_impl.cpp', 'src/dxmt/dxmt_context.hpp',
             'src/dxmt/dxmt_context.cpp', 'src/dxmt/dxmt_command_list.hpp']
    contents = {p: subprocess.check_output(['git', 'show', args.source_ref + ':' + p], cwd=root, text=True)
                if args.source_ref else (root / p).read_text() for p in paths}
    compiler = shutil.which(os.environ.get('CXX', 'clang++'))
    if not compiler:
        parser.error('C++ compiler unavailable')
    generated, routes = generate(contents, args.mutation)
    with tempfile.TemporaryDirectory(prefix='dxmt-failed-pipeline-') as directory:
        work = Path(directory)
        (work / 'scheduling-under-test.hpp').write_text(generated)
        (work / 'dxmt_command_list.hpp').write_text(contents[paths[3]])
        internal = []
        for name, signature in [('startComputePass', 'void startComputePass()'),
                                ('setComputePSO', 'void setComputePSO(WMT::ComputePipelineState pso, WMTSize tgsize)'),
                                ('dispatch', 'void dispatch(WMTSize size)')]:
            body_source = contents[paths[2]]
            start = body_source.index('SimpleCommandContext<ArgumentEncodingContext>::' + name + '(')
            brace = body_source.index('{', balanced(body_source, body_source.index('(', start)))
            body = body_source[brace:balanced(body_source, brace, '{', '}')]
            internal.append(signature + '\n' + attributed(body, paths[2], body_source))
        (work / 'internal-under-test.hpp').write_text(''.join(internal))
        # Real declaration initializers, placed in lightweight encoder layouts.
        for kind in ['Render', 'Compute']:
            body = contents[paths[1]].split('struct ' + kind + 'EncoderData : EncoderData {', 1)[1].split('\n};', 1)[0]
            flags = re.findall(r'^  bool pipeline_failed = false;$', body, re.M)
            if len(flags) != (0 if args.expect_baseline_failure else 1):
                raise ValueError('unexpected per-encoder failure field for ' + kind)
            (work / (kind.lower() + '-flag.hpp')).write_text(flags[0] if flags else 'bool pipeline_failed = false; // baseline mock compatibility; unused by old code\n')
        command = [compiler, '-std=c++20', '-O1', '-g', '-Wall', '-Wextra', '-Wno-unused-variable',
                   '-I', str(work), str(tests / 'fixture.cpp'), '-o', str(work / 'fixture')]
        if args.sanitize == 'address':
            command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        subprocess.run(command, check=True, timeout=90)
        env = {**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0' if os.uname().sysname == 'Darwin' else 'detect_leaks=1',
               'UBSAN_OPTIONS': 'halt_on_error=1'}
        result = subprocess.run([str(work / 'fixture')] + (['baseline'] if args.expect_baseline_failure else []),
                                capture_output=True, text=True, timeout=30, env=env)
        report = {'source': str(root), 'ref': args.source_ref, 'sanitize': args.sanitize,
                  'mutation': args.mutation, 'status': result.returncode, 'routes': routes,
                  'source_sha256': {p: hashlib.sha256(v.encode()).hexdigest() for p, v in contents.items()}}
        (work / 'stdout.txt').write_text(result.stdout)
        (work / 'stderr.txt').write_text(result.stderr)
        (work / 'receipt.json').write_text(json.dumps(report, indent=2) + '\n')
        if args.keep:
            kept = [path for path in work.iterdir() if path.is_file() and path.name != 'fixture']
            if sum(path.stat().st_size for path in kept) > 4 * 1024 * 1024:
                raise ValueError('retained evidence exceeds 4 MiB cap')
            args.keep.mkdir(parents=True, exist_ok=False)
            for path in kept:
                shutil.copy2(path, args.keep / path.name)
        if args.expect_baseline_failure:
            expected = ['FAIL baseline stale draw submitted', 'FAIL baseline stale dispatch submitted']
            if result.returncode != 1 or result.stderr.splitlines() != expected:
                raise SystemExit('unexpected baseline outcome:\n' + result.stdout + result.stderr)
            print('PASS baseline exposes stale draw and dispatch')
        elif args.mutation:
            witness = {'unguarded': 'FAIL cached failure omits repeated work 0',
                       'sticky': 'FAIL success failure success route 0',
                       'early-stage': 'FAIL failed stage flags stay absent'}[args.mutation]
            if result.returncode == 0 or witness not in result.stderr:
                raise SystemExit('mutation survived or failed unexpectedly:\n' + result.stdout + result.stderr)
            print('PASS mutation killed: ' + args.mutation)
            print(result.stderr, end='')
        elif result.returncode or result.stderr or result.stdout != 'PASS failed pipeline production scheduling scenarios\n':
            raise SystemExit('unexpected candidate outcome:\n' + result.stdout + result.stderr)
        else:
            print(result.stdout, end='')
        print(json.dumps(report, sort_keys=True))


if __name__ == '__main__':
    main()
