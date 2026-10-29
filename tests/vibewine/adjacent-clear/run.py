#!/usr/bin/env python3
"""Extract the actual clear optimizer; prepare-only never invokes a compiler."""
import argparse, hashlib, importlib.util, json, platform, shutil, subprocess, tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
REPO=HERE.parents[2]
BASE='b3e92edfb749eac59d90dcf2932dfd4cef24bbe5'
spec=importlib.util.spec_from_file_location('pass_fixture',HERE.parent/'pass-optimizations/run.py')
shared=importlib.util.module_from_spec(spec);spec.loader.exec_module(shared)

def generate(contents):
    generated=shared.generate(contents)
    return generated[:generated.rindex('\nint main()')]+ '\n'+(HERE/'fixture.cpp.in').read_text()

def variants(root):
    contents=shared.sources(root);current=generate(contents)
    old=dict(contents)
    for p in ('src/dxmt/dxmt_context.cpp','src/dxmt/dxmt_pass_optimizations.hpp'):
        old[p]=subprocess.check_output(['git','-C',str(root),'show',BASE+':'+p],text=True)
    on=['-DDXMT_DEAD_ATTACHMENT_STORES=1','-DDXMT_PHYSICAL_RESIDENCY_DEDUP=1']
    cases=[('default',current,[],True),('off',current,['-DDXMT_DEAD_ATTACHMENT_STORES=0'],True),('on',current,on,True),('exact-base',generate(old),on,False)]
    # Mutate only the new production helper, leaving old helper controls intact.
    split=current.index('CoalesceAdjacentColorClears('); end=current.index('inline size_t\nDeduplicateRenderResidency',split)
    helper=current[split:end]
    changes=[
      ('disable', '  auto *view = latter->attachment.ptr();','  return false;\n  auto *view = latter->attachment.ptr();'),
      ('ignore-identity','former->attachment.ptr() != latter->attachment.ptr()','false'),
      ('ignore-generation-overlap','former->fence_wait.intersectedWith(former->fence_update)','false'),
      ('allow-partial','former->width != texture->width()','false'),
      ('allow-shared','allocation->flags().test(TextureAllocationFlag::Shared)','false'),
      ('allow-native-mismatch','view->texture.width() != latter->width','false'),
      ('drop-wait','  latter->fence_wait.merge(former->fence_wait);','  /* dropped external wait */'),
      ('drop-update','  latter->fence_update.merge(former->fence_update);','  /* dropped completion */'),
      ('retain-internal-wait','  latter->fence_wait.subtract(former->fence_update);','  /* retained self wait */'),
      ('wrong-value','  former->~ClearEncoderData();','  latter->color = former->color;\n  former->~ClearEncoderData();'),
    ]
    for name,before,after in changes:
        mutated=shared.replace(helper,before,after)
        cases.append((name,current[:split]+mutated+current[end:],on,False))
    # Exercise driver adjacency, rather than only mutating a helper predicate.
    bypass=shared.replace(current,'    else if (encoders[i - 1]->type == EncoderType::Clear','    else if (false && encoders[i - 1]->type == EncoderType::Clear')
    cases.append(('bypass-production-hook',bypass,on,False))
    unlimited=shared.replace(current,'      ++i;\n    }','      /* unlimited chain aggregation */\n    }')
    cases.append(('unlimited-chain',unlimited,on,False))
    return contents,cases

def main():
    p=argparse.ArgumentParser();p.add_argument('--source-dir',type=Path,default=REPO);p.add_argument('--arch',choices=('native','x86_64'),default='native');p.add_argument('--prepare-only',action='store_true');a=p.parse_args()
    contents,cases=variants(a.source_dir)
    results=[]
    if not a.prepare_only:
      compiler=shutil.which('clang++');assert compiler
      with tempfile.TemporaryDirectory(prefix='dxmt-adjacent-clear-') as t:
       for name,source,flags,accepted in cases:
        cpp=Path(t)/(name+'.cpp');exe=Path(t)/name;cpp.write_text(source)
        command=[compiler,'-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-variable','-Wno-pragma-once-outside-header','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',*flags,*['-I'+str(a.source_dir/q) for q in ('src/winemetal','src/dxmt','src/util','src/airconv','include')],str(cpp),'-o',str(exe)]
        if a.arch=='x86_64':command+=['-arch','x86_64']
        subprocess.run(command,check=True,timeout=90)
        run=[str(exe)]
        if a.arch=='x86_64' and platform.machine()=='arm64':run=['/usr/bin/arch','-x86_64',*run]
        r=subprocess.run(run,text=True,capture_output=True,timeout=30);output=r.stdout+r.stderr
        assert r.returncode==(0 if accepted else 1),(name,r.returncode,output)
        if not accepted:assert 'behavioral assertion:' in output and 'AddressSanitizer' not in output and 'runtime error:' not in output,output
        results.append(dict(variant=name,exit=r.returncode,output=output.strip()))
    print(json.dumps(dict(scope='prepared source only' if a.prepare_only else 'extracted production optimizer CPU; no Metal/GPU/Wine',base=BASE,arch=a.arch,sources_sha256={p:hashlib.sha256(s.encode()).hexdigest() for p,s in contents.items()},prepared=[n for n,_,_,_ in cases],results=results),indent=2))
if __name__=='__main__':main()
