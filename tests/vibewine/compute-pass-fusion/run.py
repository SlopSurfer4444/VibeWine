#!/usr/bin/env python3
"""Real wrapper/exact PE thunk, whole production compute emission; no GPU/Wine."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile

HERE=Path(__file__).resolve().parent
REPO=HERE.parents[2]

def block(text,marker):
    start=text.index(marker);cursor=text.index('{',start)+1;depth=1
    while depth:
        depth+=(text[cursor]=='{')-(text[cursor]=='}');cursor+=1
    return text[start:cursor]

def generate(cpp,thunks):
    start=cpp.index('      data->fence_wait.forEach(',cpp.index('    case EncoderType::Compute: {',cpp.index('ArgumentEncodingContext::flushCommands(')))
    end=cpp.index('      data->~ComputeEncoderData();',start)
    return (HERE/'fixture.cpp.in').read_text().replace('// PRODUCTION_EMISSION',cpp[start:end]).replace(
        '// PRODUCTION_THUNK',block(thunks,'WINEMETAL_API void\nMTLComputeCommandEncoder_encodeCommands('))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir',type=Path,default=REPO)
    parser.add_argument('--arch',choices=('native','x86_64'),default='native')
    args=parser.parse_args();root=args.source_dir
    paths=['src/dxmt/dxmt_context.cpp','src/winemetal/winemetal_thunks.c']
    cpp,thunks=[(root/p).read_text() for p in paths]
    compiler=shutil.which('clang++') or shutil.which('c++');assert compiler
    on=['-DDXMT_COMPUTE_PASS_FUSION=1']
    variants=[('default',[],None),('off',['-DDXMT_COMPUTE_PASS_FUSION=0'],None),('on',on,None),
              ('combined',on+['-DDXMT_RENDER_PASS_FUSION=1'],None),
              ('reference',on+['-DDXMT_PERF_REF_W4_BATCHING=1'],None),
              ('drop-stream',on,'drop'),('double-setup',on,'double'),('bypass-fusion',on,'bypass'),
              ('reverse-stream',on,'reverse')]
    results=[]
    with tempfile.TemporaryDirectory(prefix='dxmt-compute-fusion-') as temporary:
        scratch=Path(temporary)
        def run(name,text,defines):
            source=scratch/(name+'.cpp');binary=scratch/name;source.write_text(text)
            command=[compiler,'-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                     '-fno-sanitize-recover=all','-fno-omit-frame-pointer',*defines,
                     '-I'+str(root/'include'),'-I'+str(root/'src/winemetal'),'-I'+str(root/'src/airconv'),str(source),'-o',str(binary)]
            if args.arch=='x86_64': command+=['-arch','x86_64']
            subprocess.run(command,check=True,timeout=60)
            execution=[str(binary)]
            if args.arch=='x86_64' and platform.machine()=='arm64':execution=['/usr/bin/arch','-x86_64',*execution]
            return subprocess.run(execution,check=True,capture_output=True,text=True,timeout=20).stdout
        observed={}
        for name,defines,mutation in variants:
            modified=cpp
            if mutation=='drop':modified=modified.replace('setup.back().next.set(&data->cmd_head);','setup.back().next.set(nullptr);')
            if mutation=='double':modified=modified.replace('if (!fuse_compute_setup)','if (true)')
            if mutation=='bypass':modified=modified.replace('if (!fuse_compute_setup)','if (true)').replace('if (fuse_compute_setup) {','if (false && fuse_compute_setup) {')
            if mutation=='reverse':
                target='        setup.back().next.set(&data->cmd_head);'
                assert target in modified
                modified=modified.replace(target,'        encoder.encodeCommands(&data->cmd_head);')
            rows=[list(map(int,line.split())) for line in run(name,generate(modified,thunks),defines).splitlines()]
            assert len(rows)==32;observed[name]=rows
        for name,rows in observed.items():
            equal=all(a[3:]==b[3:] for a,b in zip(rows,observed['off']))
            counts=all(a[2]==b[2]-a[1] for a,b in zip(rows,observed['off']))
            if name in ('drop-stream','double-setup','bypass-fusion','reverse-stream'):
                assert not (equal and counts),f'{name} escaped command/count contract'
            else:
                assert equal,f'{name} changed command payload/order'
                if name in ('default','off'):assert rows==observed['off']
                if name in ('on','combined'):assert counts and sum(a[1] for a in rows)==8
                if name=='reference':assert all(a[2]==b[2]+1 for a,b in zip(rows,observed['off']))
            results.append(dict(variant=name,scenarios=len(rows),command_equality=equal,enabled_count_contract=counts))
        # Compile and execute both whole emission fixtures with both flags set;
        # independently compare render output against render-on/compute-off.
        spec=importlib.util.spec_from_file_location('render_fixture',root/'tests/vibewine/render-pass-fusion/run.py')
        render=importlib.util.module_from_spec(spec);spec.loader.exec_module(render)
        render_source=render.generate(cpp,thunks,(root/'src/dxmt/dxmt_pass_optimizations.hpp').read_text())
        render_baseline=run('render-only',render_source,['-DDXMT_RENDER_PASS_FUSION=1','-DDXMT_COMPUTE_PASS_FUSION=0'])
        render_combined=run('render-combined',render_source,['-DDXMT_RENDER_PASS_FUSION=1','-DDXMT_COMPUTE_PASS_FUSION=1'])
        assert render_baseline==render_combined
    assert "option('vibewine_compute_pass_fusion', type : 'boolean', value : false," in (root/'meson.options').read_text()
    assert "'vibewine_compute_pass_fusion' : 'DXMT_COMPUTE_PASS_FUSION'" in (root/'meson.build').read_text()
    hashes=paths+['src/winemetal/Metal.hpp','src/winemetal/winemetal.h','src/winemetal/winemetal_thunks.h','src/airconv/airconv_public.h','include/ftl.hpp','tests/vibewine/compute-pass-fusion/run.py','tests/vibewine/compute-pass-fusion/fixture.cpp.in','src/winemetal/unix/winemetal_unix.c','tests/vibewine/render-pass-fusion/run.py','tests/vibewine/render-pass-fusion/fixture.cpp.in']
    print(json.dumps(dict(scope='CPU extracted whole compute emission with actual Metal.hpp/ABI and exact opcode-37 thunk; native adapter; no GPU/Wine',
                         arch=args.arch,sanitizers=['address','undefined'],eligible_crossings='2 -> 1 excluding unchanged fence crossings',
                         sources_sha256={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in hashes},results=results,
                         combined_render_compile_and_equality=True),indent=2))

if __name__=='__main__':main()
