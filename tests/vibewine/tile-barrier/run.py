#!/usr/bin/env python3
"""Extract real tile/resolver bodies plus all actual draw scheduling lambdas."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--arch', choices=['arm64', 'x86_64'], default='arm64')
    p.add_argument('--mutation', choices=['ignore-failure', 'cache-nil', 'ignore-draw-guard', 'early-marshal'])
    p.add_argument('--keep', type=Path)
    args = p.parse_args()
    if args.keep and args.keep.exists():
        p.error('--keep must not exist')
    tests = Path(__file__).resolve().parent
    root = tests.parents[2]
    previous = tests.parent / 'failed-pipeline'
    spec = importlib.util.spec_from_file_location('scheduling_runner', previous / 'run.py')
    runner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)
    paths = ['src/d3d11/d3d11_context_impl.cpp', 'src/dxmt/dxmt_context.hpp',
             'src/dxmt/dxmt_context.cpp', 'src/dxmt/dxmt_command_list.hpp',
             'src/dxmt/dxmt_command.hpp', 'src/dxmt/dxmt_command.cpp', 'src/dxmt/dxmt_deptrack.hpp']
    contents = {path: (root / path).read_text() for path in paths}
    scheduling, routes = runner.generate(contents, None)
    if args.mutation == 'ignore-draw-guard':
        scheduling, count = re.subn(r'if \(!enc.resolveRenderPassBarrier\(\)\)\n\s*return;',
                                   'enc.resolveRenderPassBarrier();', scheduling)
        assert count == 14
    if args.mutation == 'early-marshal':
        scheduling, count = re.subn(r'(if \(!enc.resolveRenderPassBarrier\(\)\)\n\s*return;\n\s*enc.bumpVisibilityResultOffset\(\);\n)(\s*enc.encode[GT]SDispatchArgumentsMarshal\([\s\S]*?\);)',
                                   r'\2\n\1', scheduling)
        assert count == 4
    command = contents[paths[5]]
    methods = []
    for name, return_type in [('TileBarrierContext', ''), ('dispatch', 'bool\n'), ('getPSO', 'WMT::RenderPipelineState\n')]:
        start = command.index('TileBarrierContext::' + name + '(')
        brace = command.index('{', runner.balanced(command, command.index('(', start)))
        body = command[start:runner.balanced(command, brace, '{', '}')]
        if args.mutation == 'cache-nil' and name == 'getPSO':
            body, count = re.subn(r'(if \(!pso\) \{\n)', r'\1    psos_.emplace(key, std::move(pso));\n', body)
            assert count == 1
        methods.append(return_type + runner.attributed(body, paths[5], command) if args.mutation != 'cache-nil' or name != 'getPSO'
                       else return_type + body + '\n')
    context = contents[paths[2]]
    start = context.index('ArgumentEncodingContext::resolveRenderPassBarrier()')
    brace = context.index('{', start)
    body = context[start:runner.balanced(context, brace, '{', '}')]
    if args.mutation == 'ignore-failure':
        body, count = re.subn(r'if \(!tile_barrier_cmd.dispatch\(\)\)\n\s*return false;',
                             'tile_barrier_cmd.dispatch();', body)
        assert count == 1
    methods.append('bool\n' + body + '\n')
    # The declaration itself is production bytes, including the owning PSO map.
    header = contents[paths[4]]
    start = header.index('class TileBarrierContext {')
    brace = header.index('{', start)
    declaration = header[start:runner.balanced(header, brace, '{', '}') + 1]
    deptrack = contents[paths[6]]
    start = deptrack.index('struct EncoderBarrierState {')
    brace = deptrack.index('{', start)
    barrier_state = deptrack[start:runner.balanced(deptrack, brace, '{', '}') + 1]
    fixture = (previous / 'fixture.cpp').read_text()
    fixture, count = re.subn(r'int main\(int argc, char \*\*\)', 'int application_pipeline_fixture_main(int argc, char **)', fixture)
    assert count == 1
    fixture = fixture.replace('enum class PipelineStage', '#include "seams.hpp"\nenum class PipelineStage', 1)
    fixture = fixture.replace('  WMTRenderCommandSetPSO,', '  WMTRenderCommandDispatchThreadsPerTile, WMTRenderCommandMemoryBarrier, WMTRenderCommandSetPSO,', 1)
    fixture = fixture.replace('  int64_t base_vertex{};', '  unsigned width{}, height{}, scope{}, stages_before{}, stages_after{};\n  int64_t base_vertex{};', 1)
    fixture = fixture.replace('using wmtcmd_render_setpso = Command;', 'using wmtcmd_render_dispatch_threads_per_tile = Command;\nusing wmtcmd_render_memory_barrier = Command;\nusing wmtcmd_render_setpso = Command;', 1)
    fixture = fixture.replace('struct RenderEncoderData {', 'struct RenderEncoderData {\n  EncoderType type = EncoderType::Render;\n  EncoderBarrierState barrier_state;\n  TileBarrierPSOKey tile_barrier_pso_key;', 1)
    fixture = fixture.replace('  ComputeEncoderData compute;', '  ComputeEncoderData compute;\n  RenderEncoderData *encoder_current = &render;\n  uint64_t intrapass_barrier_control_bits_ = 0;\n  InternalCommandLibrary library;\n  TileBarrierContext tile_barrier_cmd{{}, library, *this};', 1)
    old = '  bool resolveRenderPassBarrier() { ++render_barriers; return true; }'
    assert fixture.count(old) == 1
    fixture = fixture.replace(old, '  bool resolveRenderPassBarrier();')
    # Existing independent compute helper bodies are still required by the shared
    # scheduling fixture, but are not substitutes for the tile path under test.
    internal = []
    for name, signature in [('startComputePass', 'void startComputePass()'),
                            ('setComputePSO', 'void setComputePSO(WMT::ComputePipelineState pso, WMTSize tgsize)'),
                            ('dispatch', 'void dispatch(WMTSize size)')]:
        start = context.index('SimpleCommandContext<ArgumentEncodingContext>::' + name + '(')
        brace = context.index('{', runner.balanced(context, context.index('(', start)))
        internal.append(signature + context[brace:runner.balanced(context, brace, '{', '}')])
    with tempfile.TemporaryDirectory(prefix='dxmt-tile-failure-') as temporary:
        work = Path(temporary)
        generated = {'fixture.cpp': fixture + '\n' + (tests / 'checks.cpp').read_text(),
                     'seams.hpp': (tests / 'seams.hpp').read_text(),
                     'tile-declaration-under-test.hpp': declaration,
                     'barrier-state-under-test.hpp': barrier_state,
                     'tile-methods-under-test.hpp': '\n'.join(methods),
                     'scheduling-under-test.hpp': scheduling,
                     'internal-under-test.hpp': '\n'.join(internal),
                     'dxmt_command_list.hpp': contents[paths[3]]}
        for kind in ['Render', 'Compute']:
            data = contents[paths[1]].split('struct ' + kind + 'EncoderData : EncoderData {', 1)[1].split('\n};', 1)[0]
            flags = re.findall(r'^  bool pipeline_failed = false;$', data, re.M)
            assert len(flags) == 1
            generated[kind.lower() + '-flag.hpp'] = flags[0]
        for name, data in generated.items():
            (work / name).write_text(data)
        compiler = shutil.which(os.environ.get('CXX', 'clang++'))
        command = [compiler, '-std=c++20', '-O1', '-g', '-Wall', '-Wextra', '-Wno-unused-variable',
                   '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-arch', args.arch,
                   '-I', str(work), str(work / 'fixture.cpp'), '-o', str(work / 'fixture')]
        compiled = subprocess.run(command, capture_output=True, text=True, timeout=90)
        if compiled.returncode:
            raise SystemExit(compiled.stdout + compiled.stderr)
        launch = ['arch', '-' + args.arch, str(work / 'fixture')]
        result = subprocess.run(launch, capture_output=True, text=True, timeout=30,
                                env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0', 'UBSAN_OPTIONS': 'halt_on_error=1'})
        report = {'source': str(root), 'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
                  'arch': args.arch, 'command': command, 'launch': launch, 'mutation': args.mutation,
                  'sanitize': 'address,undefined', 'status': result.returncode, 'production_draw_routes': routes[:14],
                  'sha256': {path: hashlib.sha256(data.encode()).hexdigest() for path, data in contents.items()},
                  'fixture_sha256': {path: hashlib.sha256(data.encode()).hexdigest() for path, data in generated.items()},
                  'runner_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
        if args.keep:
            args.keep.mkdir(parents=True)
            for name, data in generated.items():
                (args.keep / name).write_text(data)
            (args.keep / 'receipt.json').write_text(json.dumps(report, indent=2) + '\n')
            (args.keep / 'stdout.txt').write_text(result.stdout)
            (args.keep / 'stderr.txt').write_text(result.stderr)
            (args.keep / 'compile.txt').write_text(compiled.stdout + compiled.stderr)
            assert sum(f.stat().st_size for f in args.keep.iterdir()) < 4 * 1024 * 1024
        if args.mutation:
            witness = {'ignore-failure': 'FAIL nil emits no helper or draw',
                       'cache-nil': 'FAIL retry creates owned cached PSO',
                       'ignore-draw-guard': 'FAIL nil emits no helper or draw',
                       'early-marshal': 'FAIL nil omits visibility and queued marshal'}[args.mutation]
            if result.returncode != 1 or witness not in result.stderr or 'AddressSanitizer:' in result.stderr or 'runtime error:' in result.stderr:
                raise SystemExit('mutation survived or failed unexpectedly:\n' + result.stdout + result.stderr)
            print('PASS mutation killed: ' + args.mutation)
        elif result.returncode or result.stderr:
            raise SystemExit(result.stdout + result.stderr)
        else:
            print(result.stdout, end='')
        print(json.dumps(report, sort_keys=True))


if __name__ == '__main__':
    main()
