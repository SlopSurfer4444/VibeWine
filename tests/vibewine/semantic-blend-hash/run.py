#!/usr/bin/env python3
"""Actual PSO hash/equality plus immutable-state hash adapters; CPU-only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
BASELINE = 'a21579fb2a03aef5fbe93d7bcba1ea882e63a192'
PATHS = ['src/d3d11/d3d11_pipeline.hpp', 'src/d3d11/d3d11_state_object.hpp',
         'src/d3d11/d3d11_state_object.cpp', 'src/d3d11/d3d11_blend_hash.hpp']


def block(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def generated(contents):
    pipeline, state, impl, helper = [contents[p] for p in PATHS]
    key = block(pipeline, 'struct MTL_GRAPHICS_PIPELINE_DESC {') + ';'
    owner = block(impl, '  size_t PipelineHash(uint32_t num_rt) const noexcept final {')
    owner = owner.replace('const noexcept final {', 'const noexcept final { COUNT(accessor);')
    getter = block(impl, '  void STDMETHODCALLTYPE GetDesc1(D3D11_BLEND_DESC1 *pDesc) final {')
    getter = getter[getter.index('{')+1:getter.rindex('}')]
    begin = impl.index('  desc_normalized.AlphaToCoverageEnable =')
    normalization = impl[begin:impl.index('\n\n  if (cache.contains(desc_normalized))', begin)]
    assert 'pipeline_hash_(hashPipelineBlend(desc_, 8))' in impl
    assert 'virtual size_t PipelineHash(uint32_t num_rt) const noexcept = 0;' in state
    direct_hash = block(pipeline, 'template <> struct hash<MTL_GRAPHICS_PIPELINE_DESC>').replace('template <> struct hash<MTL_GRAPHICS_PIPELINE_DESC>', 'struct DirectHash').replace('v.BlendState->PipelineHash(v.NumColorAttachments)', 'directBlendHash(v.BlendState, v.NumColorAttachments)') + ';'
    replacements = {
        'DIRECT_HASH': direct_hash,
        'KEY': key, 'HASH': block(pipeline, 'template <> struct hash<MTL_GRAPHICS_PIPELINE_DESC>') + ';',
        'EQUALITY': block(pipeline, 'template <> struct equal_to<MTL_GRAPHICS_PIPELINE_DESC>') + ';',
        'STATE_HASH': block(state, 'template <> struct hash<D3D11_BLEND_DESC1>') + ';',
        'STATE_EQUALITY': block(state, 'template <> struct equal_to<D3D11_BLEND_DESC1>') + ';',
        'OWNER_METHOD': owner, 'GET_DESC': getter, 'NORMALIZATION': normalization,
    }
    fixture = (HERE / 'fixture.cpp.in').read_text()
    for marker, code in replacements.items():
        assert fixture.count('// ' + marker + '\n') == 1
        fixture = fixture.replace('// ' + marker + '\n', code + '\n')
    helper = helper.replace('state.add(', 'counted_add(state, ')
    return fixture, helper


def launch(binary, arch):
    command = [str(binary)]
    if arch and platform.machine() != arch:
        command = ['/usr/bin/arch', '-' + arch, *command]
    return subprocess.run(command, capture_output=True, text=True, timeout=60,
                          env={**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1'})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, default=REPO)
    parser.add_argument('--directx', required=True, type=Path)
    parser.add_argument('--arch', choices=['arm64', 'x86_64'])
    parser.add_argument('--benchmark', action='store_true', help='Unsanitized O2 five-sample CPU fixture timings; host contention applies')
    args = parser.parse_args()
    source = args.source_dir.resolve()
    contents = {p: (source/p).read_text() for p in PATHS}
    baseline = subprocess.check_output(['git','-C',str(source),'show',f'{BASELINE}:{PATHS[0]}'],text=True)
    old = dict(contents); old[PATHS[0]]=baseline
    variants = [('default',contents,[],None),('off',contents,['-DDXMT_SEMANTIC_BLEND_HASH=0'],None),
                ('enabled',contents,['-DDXMT_SEMANTIC_BLEND_HASH=1'],None),('historical',old,[],None)]
    if not args.benchmark:
        pointer=dict(contents);pointer[PATHS[0]]=pointer[PATHS[0]].replace('v.BlendState ? v.BlendState->PipelineHash(v.NumColorAttachments) : 0','reinterpret_cast<size_t>(v.BlendState)')
        full=dict(contents);full[PATHS[3]]=full[PATHS[3]].replace('desc.IndependentBlendEnable ? num_rt : 1','8')
        unconditional=dict(contents);unconditional[PATHS[3]]=unconditional[PATHS[3]].replace('if (target.BlendEnable)', 'if (true)')
        missing=dict(contents);missing[PATHS[3]]=missing[PATHS[3]].replace('  state.add(target.SrcBlend);\n','')
        variants += [(name,text,['-DDXMT_SEMANTIC_BLEND_HASH=1','-DHASH_CONTROL'],reason) for name,text,reason in
                     [('pointer-control',pointer,'equal distinct-pointer keys must hash equally'),
                      ('all-targets-control',full,'equal distinct-pointer keys must hash equally'),
                      ('inactive-fields-control',unconditional,'equal distinct-pointer keys must hash equally'),
                      ('missing-field-control',missing,'selected-field bounded hash sentinel')]]
    else:
        variants=[variants[0],variants[2]]
    results=[]
    with tempfile.TemporaryDirectory(prefix='dxmt-semantic-blend-') as directory:
        scratch=Path(directory)
        for name,text,defines,rejection in variants:
            fixture,helper=generated(text)
            (scratch/'fixture.cpp').write_text(fixture);(scratch/'d3d11_blend_hash.hpp').write_text(helper)
            binary=scratch/name
            command=[os.environ.get('CXX','clang++'),'-std=c++20','-fno-exceptions','-fno-rtti',
                     '-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-extern-c-compat',
                     '-I',str(scratch),'-I',str(source/'src/util'),'-I',str(source/'src/airconv'),
                     '-I',str(source/'include/native/windows'),'-I',str(args.directx),*defines]
            if args.benchmark:
                command+=['-O2','-DBENCHMARK','-Wno-unused-function']
            else:
                command+=['-O1','-gline-tables-only','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
            if args.arch: command+=['-arch',args.arch]
            subprocess.run(command+[str(scratch/'fixture.cpp'),'-o',str(binary)],check=True,timeout=120)
            run=launch(binary,args.arch);output=run.stdout+run.stderr
            if rejection:
                assert run.returncode==1 and 'CONTRACT: '+rejection in output,f'{name}: {run.returncode}\n{output}'
            else:
                assert run.returncode==0,f'{name}: {run.returncode}\n{output}'
            assert 'ERROR: AddressSanitizer' not in output and 'runtime error:' not in output,output
            work=[json.loads(line[5:]) for line in output.splitlines() if line.startswith('WORK ')]
            results.append({'variant':name,'exit':run.returncode,'expected_rejection':rejection,'work':work,
                            'build':[json.loads(line[6:]) for line in output.splitlines() if line.startswith('BUILD ')],
                            'observation':output.strip() if rejection else 'production comparator/hash corpus passed'})
            print(json.dumps({'variant':name,'exit':run.returncode,'work_cases':len(work),'expected_rejection':rejection}),flush=True)
        if not args.benchmark:
            assert results[0]['work']==results[1]['work']==results[3]['work'],'default/off historical work mismatch'
            base=next(v for v in results[0]['work'] if not v['independent'] and v['attachments']==1 and v['states']==256)
            enabled=next(v for v in results[2]['work'] if v['mode']==1 and not v['independent'] and v['attachments']==1 and v['states']==256)
            assert base['largest_bucket']==256 and enabled['largest_bucket']<32,'omitted-blend baseline distribution discriminator'
            assert enabled['hit_eq']*8<base['hit_eq'] and enabled['hit_GetDesc1']*8<base['hit_GetDesc1'],'actual equality/descriptor-call reduction'
        assert sum(p.stat().st_size for p in scratch.iterdir() if p.is_file())<64*1024*1024,'64 MiB scratch cap'
    print(json.dumps({'arch':args.arch or platform.machine(),'benchmark':args.benchmark,'baseline_ref':BASELINE,
                      'scope':'actual production key/hash/equality/GetDesc1 and owner hash method; CPU adapters; no native/GPU/FPS',
                      'sources_sha256':{p:hashlib.sha256((source/p).read_bytes()).hexdigest() for p in PATHS},
                      'fixture_sha256':hashlib.sha256((HERE/'fixture.cpp.in').read_bytes()).hexdigest(),
                      'runner_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'results':results},indent=2))


if __name__=='__main__': main()
