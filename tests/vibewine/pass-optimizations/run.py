#!/usr/bin/env python3
"""Production-bound CPU fixtures; no Wine, Metal device, game or installed runtime."""
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
PATHS = ['src/dxmt/dxmt_context.cpp', 'src/dxmt/dxmt_context.hpp',
         'src/dxmt/dxmt_texture.hpp', 'src/dxmt/dxmt_pass_optimizations.hpp']

def block(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    depth, cursor = 1, opening + 1
    while depth:
        depth += (text[cursor] == '{') - (text[cursor] == '}')
        cursor += 1
    return text[start:cursor]

def replace(text, before, after):
    assert text.count(before) == 1, before
    return text.replace(before, after)

def sources(root):
    return {p: (root/p).read_text() for p in PATHS}

def generate(contents):
    cpp, ctx, tex, helper = (contents[p] for p in PATHS)
    flush = cpp.index('ArgumentEncodingContext::flushCommands(')
    optimizer_start = cpp.find('#if defined(DXMT_DEAD_ATTACHMENT_STORES)',flush)
    ordinary_start=cpp.index('  if (encoder_count > 1)',flush)
    optimizer_start=ordinary_start if optimizer_start < 0 else min(optimizer_start,ordinary_start)
    residency_start = cpp.find('#if defined(DXMT_PHYSICAL_RESIDENCY_DEDUP)',flush)
    hook = '' if residency_start < 0 else cpp[residency_start:cpp.index('#endif',residency_start)+len('#endif')]
    if residency_start >= 0:
        after_hook = cpp[residency_start+len(hook):cpp.index('      data->fence_update_vertex.forEach(',residency_start)]
        # Fusion may prepend stack setup only after this original-list dedup.
        assert 'setup[setup_count - 1].next.set(&data->cmd_head);' in after_hook
        assert 'encoder.encodeCommands(&data->cmd_head);' in after_hook
        assert 'DeduplicateRenderResidency' not in after_hook
    mocks = (HERE/'mocks.hpp').read_text()
    for marker, value in {
        'TEXTURE_ALLOCATION_FLAGS': block(tex, 'enum class TextureAllocationFlag')+';',
        'TEXTURE_VIEW_KEY': block(tex, 'struct TextureViewDescriptor')+';\n'+block(tex, 'struct TextureViewKey')+';',
        'ENCODER_STRUCTS': ctx[ctx.index('enum class EncoderType'):ctx.index('\nclass Presenter;',ctx.index('enum class EncoderType'))],
        'ENCODER_LIST_OP': block(ctx, 'enum DXMT_ENCODER_LIST_OP')+';',
        'OPTIMIZER': cpp[optimizer_start:cpp.index('\n  QueryReadbacks readbacks{};',flush)],
        'CONTEXT_METHODS': cpp[cpp.index('DXMT_ENCODER_LIST_OP\nArgumentEncodingContext::checkEncoderRelation'):cpp.index('\ntemplate <>',cpp.index('ArgumentEncodingContext::isResolveSignatureMatched('))],
        'HELPER': helper,
    }.items():
        mocks = replace(mocks, '/* '+marker+' */', value)
    fixture = replace((HERE/'fixture.cpp.in').read_text(),'/* ADAPTERS */',mocks)
    return replace(fixture,'/* RESIDENCY_HOOK */',hook)

def mutate(contents,kind):
    c = dict(contents)
    h='src/dxmt/dxmt_pass_optimizations.hpp'
    p='src/dxmt/dxmt_context.cpp'
    if kind=='baseline':
        c[h]=replace(c[h], '  color.store_action = WMTStoreActionDontCare;', '  (void)color;')
        c[h]=replace(c[h], '      command->type = WMTRenderCommandNop;', '      (void)command;')
    elif kind=='dead-after-fold':
        text=c[p]; start=text.index('#if defined(DXMT_DEAD_ATTACHMENT_STORES)',text.index('ArgumentEncodingContext::flushCommands(')); end=text.index('#endif',start)+len('#endif')
        pre=text[start:end];text=text[:start]+text[end:];marker='\n  QueryReadbacks readbacks{};';c[p]=replace(text,marker,'\n'+pre+marker)
    elif kind=='allow-partial':
        c[h]=replace(c[h],'clear->width != texture->width() || clear->height != texture->height()', 'false')
    elif kind=='ignore-native-extent':
        c[h]=replace(c[h], 'view->texture.width() != texture->width() || view->texture.height() != texture->height()', 'false')
    elif kind=='allow-resolve':
        c[h]=replace(c[h],'color.store_action != WMTStoreActionStore || color.resolve_attachment || color.level || color.slice ||','color.level || color.slice ||')
    elif kind=='ignore-usage':
        c[h]=replace(c[h],'(entry.usage & usage) == usage &&','true &&')
    elif kind=='ignore-stages':
        c[h]=replace(c[h],'(entry.stages & stages) == stages','true')
    elif kind=='combine-masks':
        c[h]=replace(c[h],'entry = {command->resource, usage, stages};','if (entry.resource == command->resource) { entry.usage |= usage; entry.stages |= stages; } else { entry = {command->resource, usage, stages}; }')
    elif kind=='ignore-identity':
        c[h]=replace(c[h],'entry.resource == command->resource && (entry.usage & usage)','(entry.usage & usage)')
    else: raise ValueError(kind)
    return c

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir',type=Path,default=REPO)
    parser.add_argument('--arch',choices=('native','x86_64'),default='native')
    parser.add_argument('--baseline-ref',default='e11476e33117b454bac60b997532f9823e34855b')
    args=parser.parse_args()
    current=sources(args.source_dir)
    compiler=shutil.which('clang++') or shutil.which('c++')
    assert compiler, 'C++ compiler unavailable'
    variants=[('default',current,[],True),('explicit-off',current,['-DDXMT_DEAD_ATTACHMENT_STORES=0','-DDXMT_PHYSICAL_RESIDENCY_DEDUP=0'],True),
              ('dead-only',current,['-DDXMT_DEAD_ATTACHMENT_STORES=1','-DDXMT_PHYSICAL_RESIDENCY_DEDUP=0'],True),
              ('residency-only',current,['-DDXMT_DEAD_ATTACHMENT_STORES=0','-DDXMT_PHYSICAL_RESIDENCY_DEDUP=1'],True),
              ('enabled',current,['-DDXMT_DEAD_ATTACHMENT_STORES=1','-DDXMT_PHYSICAL_RESIDENCY_DEDUP=1'],True)]
    baseline=dict(current)
    baseline['src/dxmt/dxmt_context.cpp']=subprocess.check_output(['git','-C',str(args.source_dir),'show',args.baseline_ref+':src/dxmt/dxmt_context.cpp'],text=True)
    variants.append(('historical-baseline',baseline,['-DDXMT_DEAD_ATTACHMENT_STORES=1','-DDXMT_PHYSICAL_RESIDENCY_DEDUP=1'],False))
    variants += [(name,mutate(current,name),['-DDXMT_DEAD_ATTACHMENT_STORES=1','-DDXMT_PHYSICAL_RESIDENCY_DEDUP=1'],False)
                 for name in ('baseline','dead-after-fold','allow-partial','ignore-native-extent','allow-resolve','ignore-usage','ignore-stages','combine-masks','ignore-identity')]
    results=[]
    with tempfile.TemporaryDirectory(prefix='dxmt-pass-optimizations-') as directory:
        scratch=Path(directory)
        for name,contents,defines,accepted in variants:
            cpp,binary=scratch/(name+'.cpp'),scratch/name
            cpp.write_text(generate(contents))
            command=[compiler,'-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-variable','-Wno-pragma-once-outside-header',
                     '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',*defines,
                     *['-I'+str(args.source_dir/p) for p in ('src/winemetal','src/dxmt','src/util','src/airconv','include')],str(cpp),'-o',str(binary)]
            if args.arch=='x86_64':command+=['-arch','x86_64']
            subprocess.run(command,check=True,timeout=90)
            run=[str(binary)]
            if args.arch=='x86_64' and platform.machine()=='arm64':run=['/usr/bin/arch','-x86_64',*run]
            result=subprocess.run(run,text=True,capture_output=True,timeout=30)
            output=(result.stdout+result.stderr).strip()
            assert (result.returncode==0)==accepted,f'{name}: unexpected exit {result.returncode}\n{output}'
            if not accepted:
                assert result.returncode==1 and 'behavioral assertion:' in output and 'ERROR: AddressSanitizer' not in output and 'runtime error:' not in output,output
            results.append({'variant':name,'accepted':accepted,'exit':result.returncode,'output':output})
    bindings={p:hashlib.sha256((args.source_dir/p).read_bytes()).hexdigest() for p in ('src/winemetal/Metal.hpp','src/winemetal/winemetal.h','src/dxmt/dxmt_deptrack.hpp','src/util/util_flags.hpp')}
    print(json.dumps({'binding_headers_sha256':bindings,'scope':'CPU production helper, optimizer, relation/signature/dependency methods; no GPU/Wine/game',
        'arch':args.arch,'baseline_ref':args.baseline_ref,'sanitizers':['address','undefined'],'sources_sha256':{p:hashlib.sha256(c.encode()).hexdigest() for p,c in current.items()},'results':results},indent=2))
if __name__=='__main__':main()
