#!/usr/bin/env python3
"""Real optimizer/relation/signatures plus production dependency tracker; CPU only."""
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
BASELINE='96940d73ac28f57fe12df832d246d3e84965f2cd'
spec=importlib.util.spec_from_file_location('pass_fixture',HERE.parent/'pass-optimizations/run.py')
shared=importlib.util.module_from_spec(spec);spec.loader.exec_module(shared)

def generate(contents):
    # Reuse production-type adapters without coupling this optimizer regression
    # to another suite's render-emission adjacency assertions. Old baseline
    # emission and a later fused render stream are both outside this test seam.
    cpp,ctx,tex,helper=(contents[p] for p in shared.PATHS)
    flush=cpp.index('ArgumentEncodingContext::flushCommands(')
    ordinary=cpp.index('  if (encoder_count > 1)',flush)
    prepass=cpp.find('#if defined(DXMT_DEAD_ATTACHMENT_STORES)',flush)
    start=ordinary if prepass<0 else min(prepass,ordinary)
    residency=cpp.find('#if defined(DXMT_PHYSICAL_RESIDENCY_DEDUP)',flush)
    hook='' if residency<0 else cpp[residency:cpp.index('#endif',residency)+len('#endif')]
    mocks=(HERE.parent/'pass-optimizations/mocks.hpp').read_text()
    replacements={
        'TEXTURE_ALLOCATION_FLAGS':shared.block(tex,'enum class TextureAllocationFlag')+';',
        'TEXTURE_VIEW_KEY':shared.block(tex,'struct TextureViewDescriptor')+';\n'+shared.block(tex,'struct TextureViewKey')+';',
        'ENCODER_STRUCTS':ctx[ctx.index('enum class EncoderType'):ctx.index('\nclass Presenter;',ctx.index('enum class EncoderType'))],
        'ENCODER_LIST_OP':shared.block(ctx,'enum DXMT_ENCODER_LIST_OP')+';',
        'OPTIMIZER':cpp[start:cpp.index('\n  QueryReadbacks readbacks{};',flush)],
        'CONTEXT_METHODS':cpp[cpp.index('DXMT_ENCODER_LIST_OP\nArgumentEncodingContext::checkEncoderRelation'):cpp.index('\ntemplate <>',cpp.index('ArgumentEncodingContext::isResolveSignatureMatched('))],
        'HELPER':helper,
    }
    for marker,value in replacements.items():mocks=shared.replace(mocks,'/* '+marker+' */',value)
    generated=shared.replace((HERE.parent/'pass-optimizations/fixture.cpp.in').read_text(),'/* ADAPTERS */',mocks)
    generated=shared.replace(generated,'/* RESIDENCY_HOOK */',hook)
    generated=generated[:generated.rindex('\nint main()')]
    return generated+'\n'+(HERE/'fixture.cpp.in').read_text()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir',type=Path,default=REPO)
    parser.add_argument('--arch',choices=('native','x86_64'),default='native')
    args=parser.parse_args();root=args.source_dir;contents=shared.sources(root)
    original=generate(contents)
    old=dict(contents);old['src/dxmt/dxmt_context.cpp']=subprocess.check_output(
        ['git','-C',str(root),'show',BASELINE+':src/dxmt/dxmt_context.cpp'],text=True)
    baseline=generate(old)
    # Connected controls each weaken one part of the actual production driver.
    disabled=shared.replace(original,'if (intervening_live_encoder && encoders[j]->type == EncoderType::Render &&',
                            'if (false && intervening_live_encoder && encoders[j]->type == EncoderType::Render &&')
    null_blocks=shared.replace(original,'        if (encoders[i]->type == EncoderType::Null)\n          continue;',
                              '        if (encoders[i]->type == EncoderType::Null) { intervening_live_encoder=true; continue; }')
    absorbed_blocks=shared.replace(original,'        intervening_live_encoder |= encoders[i]->type != EncoderType::Null;',
                                  '        intervening_live_encoder = true;')
    boundary_bypass=shared.replace(original,'if (intervening_live_encoder && encoders[j]->type == EncoderType::Render &&',
                                  'if (i-j < 64 && intervening_live_encoder && encoders[j]->type == EncoderType::Render &&')
    ignore_dependency=shared.replace(original,'          if (hasDataDependency(encoders[i], encoders[j]))',
                                     '          if (false && hasDataDependency(encoders[i], encoders[j]))')
    block_all_kinds=shared.replace(original,'            encoders[i]->type == EncoderType::Resolve) {',
                                  '            true) {')
    off=['-DDXMT_BOUNDED_ENCODER_OPTIMIZER=0'];on=['-DDXMT_BOUNDED_ENCODER_OPTIMIZER=1']
    variants=[('default',original,[],True),('off',original,off,True),('bounded',original,on,True),
              ('combined',original,on+['-DDXMT_DEAD_ATTACHMENT_STORES=1','-DDXMT_PHYSICAL_RESIDENCY_DEDUP=1'],True),
              ('old-default',baseline,[],False),('old-bounded',baseline,on,False),
              ('disable-live-guard',disabled,on,False),('null-is-live',null_blocks,on,False),
              ('absorbed-is-live',absorbed_blocks,on,False),('bypass-at-64',boundary_bypass,on,False),
              ('ignore-dependency-stop',ignore_dependency,on,False),('block-all-kinds',block_all_kinds,on,False)]
    compiler=shutil.which('clang++') or shutil.which('c++');assert compiler
    results=[]
    with tempfile.TemporaryDirectory(prefix='dxmt-resolve-adjacency-') as temporary:
        scratch=Path(temporary)
        for name,source,flags,accepted in variants:
            cpp=scratch/(name+'.cpp');binary=scratch/name;cpp.write_text(source)
            command=[compiler,'-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
                     '-Wno-unused-variable','-Wno-pragma-once-outside-header','-fsanitize=address,undefined',
                     '-fno-sanitize-recover=all','-fno-omit-frame-pointer',*flags,
                     *['-I'+str(root/p) for p in ('src/winemetal','src/dxmt','src/util','src/airconv','include')],
                     str(cpp),str(root/'src/dxmt/dxmt_deptrack.cpp'),'-o',str(binary)]
            if args.arch=='x86_64':command+=['-arch','x86_64']
            subprocess.run(command,check=True,timeout=90)
            execution=[str(binary)]
            if args.arch=='x86_64' and platform.machine()=='arm64':execution=['/usr/bin/arch','-x86_64',*execution]
            run=subprocess.run(execution,capture_output=True,text=True,timeout=30)
            output=run.stdout+run.stderr
            assert (run.returncode==0)==accepted,(name,run.returncode,output)
            if not accepted:
                assert run.returncode==1 and 'behavioral assertion:' in output and 'ERROR: AddressSanitizer' not in output and 'runtime error:' not in output
            results.append(dict(variant=name,accepted=accepted,exit=run.returncode,output=output.strip()))
    hashes={p:hashlib.sha256(c.encode()).hexdigest() for p,c in contents.items()}
    hashes.update({p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in ['src/dxmt/dxmt_deptrack.cpp','src/dxmt/dxmt_deptrack.hpp','src/winemetal/winemetal.h','src/winemetal/Metal.hpp','tests/vibewine/resolve-adjacency/fixture.cpp.in','tests/vibewine/resolve-adjacency/run.py']})
    print(json.dumps(dict(scope='CPU extracted production driver, relation/signature/dependency functions; actual production tracker linked; no Metal/GPU/Wine',
                         arch=args.arch,baseline_ref=BASELINE,sanitizers=['address','undefined'],sources_sha256=hashes,results=results),indent=2))

if __name__=='__main__':main()
