#!/usr/bin/env python3
"""Production-bound explicit discard admission/API/encoder-merge CPU checks. No GPU."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

HERE=Path(__file__).resolve().parent
REPO=HERE.parents[2]
spec=importlib.util.spec_from_file_location('pass_fixture',HERE.parent/'pass-optimizations/run.py')
pf=importlib.util.module_from_spec(spec);spec.loader.exec_module(pf)
PATHS=[*pf.PATHS,'src/d3d11/d3d11_context_impl.cpp','src/dxmt/dxmt_discard_hints.hpp']

def method(s,name):
    start=s.index('\n  '+name+'(')
    return 'void\n'+pf.block(s[start:],'  '+name+'(').replace(' override {',' {')

def generate(contents,old_api=None):
    cpp,ctx,tex,helper=(contents[p] for p in pf.PATHS)
    mocks=(HERE.parent/'pass-optimizations/mocks.hpp').read_text()
    mocks=mocks.replace('class Texture {\npublic:','class Texture {\npublic:\n  TextureAllocation *current_alloc{};\n  TextureAllocation *current() const { return current_alloc; }\n  TextureViewKey fullView;')
    mocks=mocks.replace('  Statistics statistics{};','  Statistics statistics{};\n  EncoderData *encoder_last{};\n  EncoderData *encoder_current{};\n  void discardColorStore(Texture *, TextureViewKey);')
    for marker,value in {
        'TEXTURE_ALLOCATION_FLAGS':pf.block(tex,'enum class TextureAllocationFlag')+';',
        'TEXTURE_VIEW_KEY':pf.block(tex,'struct TextureViewDescriptor')+';\n'+pf.block(tex,'struct TextureViewKey')+';',
        'ENCODER_STRUCTS':ctx[ctx.index('enum class EncoderType'):ctx.index('\nclass Presenter;',ctx.index('enum class EncoderType'))],
        'ENCODER_LIST_OP':pf.block(ctx,'enum DXMT_ENCODER_LIST_OP')+';',
        'OPTIMIZER':'',
        'CONTEXT_METHODS':cpp[cpp.index('DXMT_ENCODER_LIST_OP\nArgumentEncodingContext::checkEncoderRelation'):cpp.index('\ntemplate <>',cpp.index('ArgumentEncodingContext::isResolveSignatureMatched('))],
        'HELPER':helper+'\n'+contents['src/dxmt/dxmt_discard_hints.hpp'],
    }.items():mocks=pf.replace(mocks,'/* '+marker+' */',value)
    context_method=pf.block(cpp,'void\nArgumentEncodingContext::discardColorStore')
    mocks+='\nnamespace dxmt {\n'+context_method+'\n}\n'
    apis=contents['src/d3d11/d3d11_context_impl.cpp']
    extracted='\n'.join(method(old_api if old_api and n!='DiscardColorResource' else apis,n) for n in ('DiscardColorResource','DiscardResource','DiscardView','DiscardView1'))
    fixture=(HERE/'fixture.cpp.in').read_text()
    return pf.replace(pf.replace(fixture,'/* ADAPTERS */',mocks),'/* API_METHODS */',extracted)

def main():
    contents={p:(REPO/p).read_text() for p in PATHS}
    old_api=subprocess.check_output(['git','-C',str(REPO),'show','c7e4ab455d7a666768ea8cd1f7e719d879bd1218:src/d3d11/d3d11_context_impl.cpp'],text=True)
    variants=[('default',contents,[],True,None),('explicit-off',contents,['-DDXMT_DISCARD_HINTS=0'],True,None),('enabled',contents,['-DDXMT_DISCARD_HINTS=1'],True,None),('historical-noop',contents,['-DDXMT_DISCARD_HINTS=1'],False,old_api)]
    h='src/dxmt/dxmt_discard_hints.hpp'
    for name,before,after in [
      ('wrong-allocation','allocation != texture->current() ||','false ||'),
      ('wrong-view','uint64_t(attachment->key) != uint64_t(view) ||','false ||'),
      ('pending-resolve','color.store_action != WMTStoreActionStore || color.resolve_attachment ||','color.store_action != WMTStoreActionStore ||'),
      ('wrong-mip','texture->miplevelCount() != 1 ||','false ||'),
      ('wrong-shared','allocation->flags().test(TextureAllocationFlag::Shared) ||','false ||'),
      ('noop-action','color.store_action = WMTStoreActionDontCare;','(void)color;'),
    ]:
      changed=dict(contents);changed[h]=pf.replace(changed[h],before,after)
      variants.append((name,changed,['-DDXMT_DISCARD_HINTS=1'],False,None))
    results=[]
    compiler=shutil.which('clang++') or shutil.which('c++');assert compiler
    with tempfile.TemporaryDirectory(prefix='dxmt-discard-hints-') as tmp:
      for name,source,defines,accepted,old in variants:
        p=Path(tmp)/name;cpp=p.with_suffix('.cpp');cpp.write_text(generate(source,old))
        command=[compiler,'-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-variable','-Wno-pragma-once-outside-header','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',*defines,*['-I'+str(REPO/p) for p in ('src/winemetal','src/dxmt','src/util','src/airconv','include')],str(cpp),'-o',str(p)]
        subprocess.run(command,check=True,timeout=90)
        run=subprocess.run([str(p)],capture_output=True,text=True,timeout=30)
        output=(run.stdout+run.stderr).strip()
        assert (run.returncode==0)==accepted,(name,run.returncode,output)
        if not accepted:assert run.returncode==1 and 'behavioral assertion:' in output and 'runtime error:' not in output and 'AddressSanitizer' not in output
        results.append(dict(variant=name,exit=run.returncode,accepted=accepted,output=output))
    print(json.dumps(dict(scope='production API methods, ordered state lambda, admission helper, relation/signature/dependency methods; adapters model COM/native identity only; no GPU/build/install',sanitizers=['address','undefined'],sources_sha256={p:hashlib.sha256(c.encode()).hexdigest() for p,c in contents.items()},results=results),indent=2))
if __name__=='__main__':main()
