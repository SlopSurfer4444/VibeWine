#!/usr/bin/env python3
"""Actual native descriptor path and real Metal descriptor snapshots; no GPU/device."""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile,statistics,platform
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
def block(text,marker):
    start=text.index(marker);pos=text.index('{',start)+1;depth=1
    while depth:depth+=(text[pos]=='{')-(text[pos]=='}');pos+=1
    return text[start:pos]
def main():
    p=argparse.ArgumentParser();p.add_argument('--source-dir',type=Path,default=ROOT);p.add_argument('--bench',action='store_true');args=p.parse_args();root=args.source_dir
    paths=['src/winemetal/unix/winemetal_unix.c','src/winemetal/Metal.hpp','src/winemetal/winemetal.h'];contents={f:(root/f).read_text() for f in paths}
    native=block(contents[paths[0]],'static NTSTATUS\n_MTLCommandBuffer_renderCommandEncoder(')
    # One C-to-ObjC++ void-pointer conversion; statements/branches stay intact.
    native=native.replace('struct unixcall_generic_obj_uint64_obj_ret *params = obj;','struct unixcall_generic_obj_uint64_obj_ret *params = (struct unixcall_generic_obj_uint64_obj_ret *)obj;')
    marker='    MTLRenderPassColorAttachmentDescriptor *color = colors[i];';assert native.count(marker)==1
    instrumented=native.replace(marker,'#ifdef EMPTY_SLOT_COUNT\n    ++visited_slots; ++indexed_getters;\n#endif\n'+marker)
    reference_macro='#define W4_COLOR_ATTACHMENT descriptor.colorAttachments[i]'
    assert instrumented.count(reference_macro)==1
    instrumented=instrumented.replace(reference_macro,'#ifdef EMPTY_SLOT_COUNT\n#define W4_COLOR_ATTACHMENT (++indexed_getters, descriptor.colorAttachments[i])\n#else\n'+reference_macro+'\n#endif')
    reference_slot='    W4_COLOR_ATTACHMENT.clearColor = MTLClearColorMake('
    assert instrumented.count(reference_slot)==1
    instrumented=instrumented.replace(reference_slot,'#if defined(EMPTY_SLOT_COUNT) && defined(DXMT_PERF_REF_W4_GETTERS) && DXMT_PERF_REF_W4_GETTERS\n    ++visited_slots;\n#endif\n'+reference_slot)
    init=block(contents[paths[1]],'inline void\nInitializeRenderPassInfo(')
    template=(HERE/'fixture.mm.in').read_text().replace('// PRODUCTION_INITIALIZER',init)
    base=['xcrun','clang++','-std=c++20','-O2','-g','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fno-objc-arc','-I'+str(root/'src/winemetal'),'-framework','Foundation','-framework','Metal']
    variants=[('default',[],None),('off',['-DDXMT_EMPTY_COLOR_SLOTS=0'],None),('on',['-DDXMT_EMPTY_COLOR_SLOTS=1'],None),('nil-only',['-DDXMT_EMPTY_COLOR_SLOTS=1'],'nil'),('numeric-zero',['-DDXMT_EMPTY_COLOR_SLOTS=1'],'numeric'),('ignore-resolve-depth',['-DDXMT_EMPTY_COLOR_SLOTS=1'],'index'),('bypass',['-DDXMT_EMPTY_COLOR_SLOTS=1'],'bypass'),('reference-default',['-DDXMT_PERF_REF_W4_GETTERS=1'],None),('reference-off',['-DDXMT_PERF_REF_W4_GETTERS=1','-DDXMT_EMPTY_COLOR_SLOTS=0'],None),('reference-on',['-DDXMT_PERF_REF_W4_GETTERS=1','-DDXMT_EMPTY_COLOR_SLOTS=1'],None),('reference-guard-regression',['-DDXMT_PERF_REF_W4_GETTERS=1','-DDXMT_EMPTY_COLOR_SLOTS=1'],'reference')]
    results={};benchmarks={};samples={}
    with tempfile.TemporaryDirectory(prefix='dxmt-empty-slots-') as temp:
        temp=Path(temp)
        for name,flags,mutation in variants:
            code=instrumented
            if mutation in ('nil','bypass'):
                start=code.index('    if (!slot->texture');end=code.index('      continue;',start)
                condition=code[start:end];assert condition.rstrip().endswith(')')
                condition=condition.replace('    if (','    if ('+('!slot->texture || (' if mutation=='nil' else 'false && ('),1).rstrip()+')\n';code=code[:start]+condition+code[end:]
            if mutation=='numeric':
                for field,value in (('r','clear_zero'),('g','clear_zero'),('b','clear_zero'),('a','clear_one')):
                    old=f'!memcmp(&slot->clear_color.{field}, &{value}, sizeof({value}))';assert old in code;code=code.replace(old,f'(slot->clear_color.{field} == {value})')
            if mutation=='index':code=code.replace('!slot->resolve_depth_plane &&','true &&')
            if mutation=='reference':
                suffix=' && \\n    (!defined(DXMT_PERF_REF_W4_GETTERS) || !DXMT_PERF_REF_W4_GETTERS)'
                suffix=suffix.replace('\\n','\\\n')
                assert code.count(suffix)==2
                code=code.replace(suffix,'')
            source=temp/(name+'.mm');binary=temp/name;source.write_text(template.replace('// PRODUCTION_DESCRIPTOR_PATH',code))
            subprocess.run(base+flags+['-DEMPTY_SLOT_COUNT','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(source),'-o',str(binary)],check=True,timeout=60)
            rows=json.loads(subprocess.check_output([str(binary)],text=True,timeout=20));results[name]={r['name']:r for r in rows}
        assert results['default']==results['off']
        assert results['reference-default']==results['reference-off']==results['reference-on']
        for rows in (results['reference-default'],results['reference-off'],results['reference-on']):
            for name,row in rows.items():
                assert row['snapshot']==results['off'][name]['snapshot'],name
                assert row['visited']==8 and row['indexedGetters']==88,(name,row)
        for row in results['off'].values():assert row['indexedGetters']==8

        for name,row in results['on'].items():
            assert 'exception' not in row,(name,row)
            old=results['off'][name];assert row['snapshot']==old['snapshot'],name
            assert row['status']==old['status'] and row['nilReturn']==old['nilReturn'],name
            expected=int(name.rsplit('-',1)[1]) if name.startswith('nil-noncanonical-') else 3 if name.startswith('sparse-') else 0 if name=='nil-depth-stencil-global-control' else 1
            assert row['visited']==expected,(name,row['visited'],expected)
            assert row['indexedGetters']==expected,(name,row['indexedGetters'],expected)
            assert old['visited']==8
        for mutation in ('nil-only','numeric-zero','ignore-resolve-depth'):
            assert any(row.get('snapshot')!=results['off'][name].get('snapshot') or 'exception' in row for name,row in results[mutation].items()),mutation+' escaped'
        assert any(row['visited']!=results['on'][name]['visited'] for name,row in results['bypass'].items()),'bypass escaped count contract'
        assert any(row['indexedGetters']!=88 for row in results['reference-guard-regression'].values()),'reference guard regression escaped count contract'
        # Original full native C translation unit: Wine/native and getter-reference branches.
        inc=['-I'+str(root/p) for p in ('include','include/native','src/airconv','src/winemetal')]
        for flags in ([],['-DDXMT_NATIVE'],['-DDXMT_PERF_REF_W4_GETTERS=1']):
            subprocess.run(['xcrun','clang','-fsyntax-only','-x','objective-c','-fblocks','-fno-objc-arc','-Werror','-Wno-deprecated-declarations',*inc,*flags,'-DDXMT_EMPTY_COLOR_SLOTS=1',str(root/paths[0])],check=True,timeout=60)
        if args.bench:
            for name,flags in (('off',['-DDXMT_EMPTY_COLOR_SLOTS=0']),('on',['-DDXMT_EMPTY_COLOR_SLOTS=1'])):
                source=temp/(name+'.mm');binary=temp/(name+'-bench');source.write_text(template.replace('// PRODUCTION_DESCRIPTOR_PATH',native))
                subprocess.run(base+flags+[str(source),'-o',str(binary)],check=True,timeout=60);benchmarks[name]=binary
            for order,name in enumerate(('off','on','on','off')):
                output=subprocess.check_output([str(benchmarks[name]),'bench'],text=True,timeout=45)
                for line in output.splitlines():mode,iteration,ns=map(int,line.split());samples.setdefault(name,{}).setdefault(mode,[]).append({'order':order,'block':iteration,'ns_per_call':ns/10000})
    report=dict(scope='exact production native descriptor function; real Metal descriptor objects and CPU capture receiver; no device, real texture, commandbuffer or render encoder created',architecture=platform.machine(),scenarios=len(results['on']),snapshot_equality=True,reference_integrity={v:{'scenarios':len(results[v]),'slot_visits_each':8,'indexed_getters_each':88,'snapshots_match_off':True} for v in ('reference-default','reference-off','reference-on')},visits={name:{v:results[v][name]['visited'] for v in ('off','on')} for name in results['on']},mutations_rejected=['nil-only guard','numeric double equality','omitted resolve_depth_plane','bypassed optimization','reference guard priority regression'],full_native_syntax=['Wine','native','getter-reference'],source_sha256={f:hashlib.sha256(c.encode()).hexdigest() for f,c in contents.items()},benchmark=dict(unit='ns per native descriptor creation/release and autoreleasepool; actual descriptor setters/getters, no encoder creation',modes={'0':'8 canonical nil slots','1':'7 canonical + 1 noncanonical nil slot','2':'8 noncanonical nil slots, no hits'},order=['off','on','on','off'],summary={name:{str(mode):{'median':statistics.median(x['ns_per_call'] for x in values),'min':min(x['ns_per_call'] for x in values),'max':max(x['ns_per_call'] for x in values)} for mode,values in modes.items()} for name,modes in samples.items()},samples=samples),limits=['0/1/4/8 and sparse branches use nil noncanonical sentinels, not real active render targets','Real active MRT/depth/MSAA/resolve, encoder validation/private metadata and GPU/query progress require root GPU acceptance','No universal performance or FPS claim'])
    print(json.dumps(report,indent=2))
if __name__=='__main__':main()
