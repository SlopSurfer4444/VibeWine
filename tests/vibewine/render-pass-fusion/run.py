#!/usr/bin/env python3
"""Compare whole production render emission via real wrappers and exact opcode-38 thunk."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile

HERE=Path(__file__).resolve().parent
REPO=HERE.parents[2]

def block(text,marker):
    start=text.index(marker)
    cursor=text.index('{',start)+1
    depth=1
    while depth:
        depth+=(text[cursor]=='{')-(text[cursor]=='}')
        cursor+=1
    return text[start:cursor]

def generate(cpp,thunk,helper):
    start=cpp.index('      data->fence_wait.forEach(',cpp.index('ArgumentEncodingContext::flushCommands('))
    end=cpp.index('      data->~RenderEncoderData();',start)
    fixture=(HERE/'fixture.cpp.in').read_text()
    fixture=fixture.replace('// PRODUCTION_EMISSION',cpp[start:end])
    fixture=fixture.replace('// PRODUCTION_THUNK',block(thunk,'WINEMETAL_API void\nMTLRenderCommandEncoder_encodeCommands('))
    return fixture.replace('// PRODUCTION_DEDUP',block(helper,'inline size_t\nDeduplicateRenderResidency('))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir',type=Path,default=REPO)
    parser.add_argument('--arch',choices=('native','x86_64'),default='native')
    args=parser.parse_args()
    root=args.source_dir
    paths=['src/dxmt/dxmt_context.cpp','src/winemetal/winemetal_thunks.c','src/dxmt/dxmt_pass_optimizations.hpp']
    sources=[(root/p).read_text() for p in paths]
    compiler=shutil.which('clang++') or shutil.which('c++')
    assert compiler
    variants=[('default',[],None),('off',['-DDXMT_RENDER_PASS_FUSION=0'],None),
              ('on',['-DDXMT_RENDER_PASS_FUSION=1'],None),
              ('reference',['-DDXMT_RENDER_PASS_FUSION=1','-DDXMT_PERF_REF_W4_BATCHING=1'],None),
              ('drop-stream',['-DDXMT_RENDER_PASS_FUSION=1'],'drop'),
              ('double-setup',['-DDXMT_RENDER_PASS_FUSION=1'],'double'),
              ('unsafe-marshal',['-DDXMT_RENDER_PASS_FUSION=1'],'marshal'),
              ('bypass-fusion',['-DDXMT_RENDER_PASS_FUSION=1'],'bypass')]
    results=[]
    with tempfile.TemporaryDirectory(prefix='dxmt-pass-fusion-') as temp:
        scratch=Path(temp)
        for dedup in (0,1):
            observed={}
            for name,defines,mutation in variants:
                cpp=sources[0]
                if mutation=='drop': cpp=cpp.replace('setup[setup_count - 1].next.set(&data->cmd_head);','setup[setup_count - 1].next.set(nullptr);')
                if mutation=='bypass': cpp=cpp.replace('if (!fuse_render_setup)','if (true)').replace('if (fuse_render_setup) {','if (false && fuse_render_setup) {')
                if mutation=='double': cpp=cpp.replace('if (!fuse_render_setup)','if (true)')
                if mutation=='marshal':
                    a='encoder && gpu_buffer_ && !data->use_geometry && !data->use_tessellation &&\n                                     !data->use_emulated_so && data->gs_arg_marshal_tasks.empty() &&\n                                     data->ts_arg_marshal_tasks.empty()'
                    assert a in cpp
                    cpp=cpp.replace(a,'encoder && gpu_buffer_ && !data->use_geometry && !data->use_tessellation && !data->use_emulated_so')
                source=scratch/f'{name}-{dedup}.cpp'
                binary=scratch/f'{name}-{dedup}'
                source.write_text(generate(cpp,*sources[1:]))
                command=[compiler,'-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                         '-fno-sanitize-recover=all','-fno-omit-frame-pointer',*defines,f'-DDXMT_PHYSICAL_RESIDENCY_DEDUP={dedup}',
                         '-I'+str(root/'include'),'-I'+str(root/'src/winemetal'),'-I'+str(root/'src/airconv'),str(source),'-o',str(binary)]
                if args.arch=='x86_64':command+=['-arch','x86_64']
                subprocess.run(command,check=True,timeout=60)
                run=[str(binary)]
                if args.arch=='x86_64' and platform.machine()=='arm64':run=['/usr/bin/arch','-x86_64',*run]
                output=subprocess.run(run,text=True,capture_output=True,check=True,timeout=20)
                rows=[list(map(int,line.split())) for line in output.stdout.splitlines()]
                assert len(rows)==256
                observed[name]=rows
            for name,rows in observed.items():
                equal=all(row[3:]==base[3:] for row,base in zip(rows,observed['off']))
                count_contract=all(row[2]==base[2]-row[1] for row,base in zip(rows,observed['off']))
                if name in ('drop-stream','double-setup','unsafe-marshal','bypass-fusion'):
                    assert not (equal and count_contract),f'{name} mutation escaped command/count contract'
                else:
                    assert equal,f'{name} changed command order/payload'
                    if name in ('default','off'):assert rows==observed['off']
                    if name=='on':
                        for row,base in zip(rows,observed['off']):
                            assert row[2]==base[2]-row[1],f'wrong crossing count: {row} vs {base}'
                        assert sum(row[1] for row in rows)==2
                    if name=='reference':
                        for row,base in zip(rows,observed['off']): assert row[2]>=base[2]
                results.append(dict(dedup=dedup,variant=name,scenarios=len(rows),command_equality=equal,enabled_count_contract=count_contract))
    options=(root/'meson.options').read_text();build=(root/'meson.build').read_text()
    assert "option('vibewine_render_pass_fusion', type : 'boolean', value : false," in options
    assert "'vibewine_render_pass_fusion' : 'DXMT_RENDER_PASS_FUSION'" in build
    print(json.dumps(dict(scope='CPU whole production render emission, actual Metal.hpp/ABI and exact PE thunk; native dispatch adapter, no GPU/Wine',
                         arch=args.arch,sanitizers=['address','undefined'],eligible_crossings='2 -> 1 excluding unchanged fence crossings',
                         sources_sha256={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths+['src/winemetal/Metal.hpp','src/winemetal/winemetal.h','src/winemetal/winemetal_thunks.h','src/airconv/airconv_public.h','include/ftl.hpp','tests/vibewine/render-pass-fusion/run.py','tests/vibewine/render-pass-fusion/fixture.cpp.in','src/winemetal/unix/winemetal_unix.c']},results=results),indent=2))

if __name__=='__main__':main()
