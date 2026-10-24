#!/usr/bin/env python3
"""Compile verbatim production Present1/SyncFrame/Flush methods with CPU adapters.

Reject historical ordering and connected ownership/admission mutations. No Wine,
Metal device, installed runtime, game or GUI is used. Scratch is removed on exit.
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


def block(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    depth, cursor = 1, opening + 1
    while depth:
        depth += (text[cursor] == '{') - (text[cursor] == '}')
        cursor += 1
    return text[start:cursor]


def sources(source=REPO, ref=None):
    paths = ['src/d3d11/d3d11_swapchain.cpp', 'src/d3d11/d3d11_context_imm.cpp']
    return {path: (subprocess.check_output(['git', '-C', str(source), 'show', f'{ref}:{path}'], text=True)
                   if ref else (source / path).read_text()) for path in paths}


def generate(contents):
    swap, imm = contents.values()
    replacements = {
        'SYNC_FRAME_STATE': block(swap, '  class SyncFrameState {') + ';',
        'SYNC_FRAME': block(swap, '  SyncFrameState SyncFrame('),
        'PRESENT': block(swap, '  DXMT_HOTPATCHABLE\n  HRESULT\n  STDMETHODCALLTYPE\n  Present1('),
        'FLUSH': block(imm, '  void\n  STDMETHODCALLTYPE\n  Flush() override'),
        'FLUSH1': block(imm, '  void\n  STDMETHODCALLTYPE\n  Flush1('),
        'PREPARE_FLUSH': block(imm, '  void PrepareFlush() override'),
        'COMMIT': block(imm, '  void\n  Commit() override'),
    }
    generated = (HERE / 'fixture.cpp').read_text()
    for key, value in replacements.items():
        marker = '// ' + key + '\n'
        assert generated.count(marker) == 1, key
        generated = generated.replace(marker, value + '\n')
    return generated


def mutate(contents, kind):
    contents = dict(contents)
    key = 'src/d3d11/d3d11_swapchain.cpp'
    text = contents[key]
    if kind == 'credit':
        before = 'sync_state = std::move(sync_state),'
        assert text.count(before) == 2
        text = text.replace(before, 'sync_state = SyncFrameState(),')
    elif kind == 'admission':
        admission = '    auto sync_state = SyncFrame(++presentation_count_);\n'
        assert text.count(admission) == 1
        text = text.replace(admission, '')
        marker = '#endif\n    auto &cmd_queue = device_->GetDXMTDevice().queue();'
        assert text.count(marker) == 1
        text = text.replace(marker, '#endif\n' + admission + '    auto &cmd_queue = device_->GetDXMTDevice().queue();')
    elif kind == 'empty':
        text = text.replace('device_context_->Flush(); }', 'device_context_->Commit(); }')
    elif kind == 'early_fence':
        before = '    auto sync_state = SyncFrame(++presentation_count_);'
        text = text.replace(before, '    device_->GetDXMTDevice().queue().CurrentChunk()->signal_frame_latency_fence_ = device_->GetDXMTDevice().queue().CurrentFrameSeq();\n' + before)
    else:
        raise ValueError(kind)
    contents[key] = text
    return contents


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, default=REPO)
    parser.add_argument('--baseline-ref', default='236089329156243f6c3777b4114be38b50bb4301')
    parser.add_argument('--arch', choices=('native', 'x86_64'), default='native')
    args = parser.parse_args()
    current = sources(args.source_dir)
    compiler = shutil.which('clang++') or shutil.which('c++')
    if not compiler:
        parser.error('C++ compiler unavailable')
    variants = [('default',current,[],True), ('explicit-off',current,['-DDXMT_SCENE_SUBMISSION=0'],True),
                ('enabled',current,['-DDXMT_SCENE_SUBMISSION=1'],True),
                ('baseline',sources(args.source_dir,args.baseline_ref),['-DDXMT_SCENE_SUBMISSION=1'],False)]
    variants += [(name,mutate(current,name),['-DDXMT_SCENE_SUBMISSION=1'],False)
                 for name in ['credit','admission','empty','early_fence']]
    results = []
    with tempfile.TemporaryDirectory(prefix='dxmt-scene-submission-') as directory:
        scratch = Path(directory)
        for name, contents, defines, expected in variants:
            cpp, binary = scratch / (name+'.cpp'), scratch / name
            cpp.write_text(generate(contents))
            # Undefined selection follows the production default-off #if path.
            if not defines:
                cpp.write_text(cpp.read_text().replace('const bool split=DXMT_SCENE_SUBMISSION &&', 'const bool split=false &&'))
            command = [compiler,'-std=c++20','-O1','-g','-Wall','-Wextra','-Werror',
                       '-Wno-unused-variable','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all',
                       '-fno-omit-frame-pointer',*defines,str(cpp),'-o',str(binary)]
            if args.arch == 'x86_64': command += ['-arch','x86_64']
            subprocess.run(command, check=True, timeout=60)
            run = [str(binary)]
            if args.arch == 'x86_64' and platform.machine() == 'arm64': run = ['/usr/bin/arch','-x86_64',*run]
            result = subprocess.run(run,text=True,capture_output=True,timeout=20)
            assert (result.returncode==0)==expected, f'{name}: unexpected result {result.returncode}\n{result.stdout}{result.stderr}'
            # A negative must fail the behavioral contract, not sanitizer setup.
            if not expected:
                assert result.returncode==1 and 'ERROR: AddressSanitizer' not in result.stderr and 'runtime error:' not in result.stderr
            results.append({'variant':name,'accepted':expected,'output':(result.stdout+result.stderr).strip()})
    option = (args.source_dir / 'meson.options').read_text()
    build = (args.source_dir / 'meson.build').read_text()
    assert "option('vibewine_scene_submission', type : 'boolean', value : false," in option
    assert "add_project_arguments(vibewine_scene_define, language: ['c', 'cpp'])" in build
    assert "add_project_arguments(vibewine_scene_define, language: ['c', 'cpp'], native: true)" in build
    print(json.dumps({'scope':'CPU production-method extraction with observable adapters; no renderer/GPU/Wine',
                      'arch':args.arch,'sanitizers':['address','undefined'],'baseline_ref':args.baseline_ref,
                      'sources_sha256':{p:hashlib.sha256(c.encode()).hexdigest() for p,c in current.items()},
                      'results':results},indent=2))


if __name__ == '__main__':
    main()
