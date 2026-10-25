#!/usr/bin/env python3
"""Extract and execute whole native render decoder on a CPU ObjC receiver; no GPU."""
from pathlib import Path
import argparse, hashlib, json, subprocess, tempfile, statistics
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
def block(text,marker):
    start=text.index(marker);pos=text.index('{',start)+1;depth=1
    while depth:depth+=(text[pos]=='{')-(text[pos]=='}');pos+=1
    return text[start:pos]
def main():
    p=argparse.ArgumentParser();p.add_argument('--source-dir',type=Path,default=ROOT);p.add_argument('--bench',action='store_true');args=p.parse_args();root=args.source_dir
    native=(root/'src/winemetal/unix/winemetal_unix.c').read_text();decoder=block(native,'static NTSTATUS\n_MTLRenderCommandEncoder_encodeCommands(')
    # Objective-C++ requires explicit void-pointer conversions accepted by C.
    # These three mechanical casts leave the production loop/switch unchanged.
    decoder=decoder.replace('struct unixcall_generic_obj_cmd_noret *params = obj;', 'struct unixcall_generic_obj_cmd_noret *params = (struct unixcall_generic_obj_cmd_noret *)obj;').replace('const struct wmtcmd_base *next = params->cmd_head.ptr;', 'const struct wmtcmd_base *next = (const struct wmtcmd_base *)params->cmd_head.ptr;').replace('next = next->next.ptr;', 'next = (const struct wmtcmd_base *)next->next.ptr;')
    base=['xcrun','clang++','-std=c++20','-O2','-g','-Wall','-Wextra','-Werror','-DNDEBUG','-Wno-unused-parameter','-Wno-deprecated-declarations','-fno-objc-arc','-framework','Foundation','-framework','Metal']
    inc=['-I'+str(root/p) for p in ('include','include/native','src/airconv','src/winemetal')]
    modes=[('default',[],None),('off',['-DDXMT_NATIVE_RASTER_SHADOW=0'],None),('on',['-DDXMT_NATIVE_RASTER_SHADOW=1'],None),('no-icb-invalidation',['-DDXMT_NATIVE_RASTER_SHADOW=1'],'icb'),('float-numeric',['-DDXMT_NATIVE_RASTER_SHADOW=1'],'numeric')]
    results={};executables={}
    with tempfile.TemporaryDirectory(prefix='dxmt-native-raster-') as temp:
        temp=Path(temp)
        for name,flags,mutation in modes:
            code=decoder
            if mutation=='icb':
                start=code.index('case WMTRenderCommandExecuteCommandsInBuffer:');before,after=code[:start],code[start:];assert after.count('raster_shadow = NULL;')==1;code=before+after.replace('raster_shadow = NULL;','/* mutation: no invalidation */')
            if mutation=='numeric':
                for field in ('depth_bias','scole_scale','depth_bias_clamp'):
                    old=f'memcmp(&raster_shadow->{field}, &body->{field}, sizeof(body->{field}))';assert old in code;code=code.replace(old,f'(raster_shadow->{field} != body->{field})')
            source=temp/(name+'.mm');binary=temp/name;source.write_text((HERE/'fixture.mm.in').read_text().replace('// PRODUCTION_DECODER',code))
            subprocess.run(base+inc+flags+['-fsanitize=address,undefined','-fno-sanitize-recover=all',str(source),'-o',str(binary)],check=True,timeout=60)
            rows=subprocess.check_output([str(binary)],text=True,timeout=20);results[name]={v[0]:list(map(int,v[1:])) for line in rows.splitlines() if (v:=line.split())}
        expected={'empty':0,'single':5,'repeat':5,'cull':6,'signedzero':6,'nanrepeat':5,'nanpayload':6,'reserved':5,'allchanged':10,'icb':10,'unknown':10,'calls':10,'nil':0,'arraysunbind':5,**{f'field{i}':6 for i in range(7)}}
        assert results['default']==results['off']
        for key,row in results['on'].items():
            assert row[1:]==results['off'][key][1:],(key,'draw-visible state or other calls changed')
            assert row[0]==expected[key],(key,row[0],expected[key])
        assert results['no-icb-invalidation']['icb'][1:]!=results['off']['icb'][1:],'ICB mutation escaped'
        assert results['float-numeric']['signedzero']!=results['on']['signedzero'],'numeric mutation escaped signed zero'
        assert results['float-numeric']['nanrepeat']!=results['on']['nanrepeat'],'numeric mutation escaped NaN'
        # Compile complete production Objective-C translation unit, Wine/native modes.
        for flags in ([],['-DDXMT_NATIVE']):
            subprocess.run(['xcrun','clang','-fsyntax-only','-x','objective-c','-fblocks','-fno-objc-arc','-Werror','-Wno-deprecated-declarations',*inc,*flags,'-DDXMT_NATIVE_RASTER_SHADOW=1',str(root/'src/winemetal/unix/winemetal_unix.c')],check=True,timeout=60)
        timings={}
        if args.bench:
            for name,flags in (('off',['-DDXMT_NATIVE_RASTER_SHADOW=0']),('on',['-DDXMT_NATIVE_RASTER_SHADOW=1'])):
                source=temp/(name+'.mm');binary=temp/(name+'-bench');subprocess.run(base+inc+flags+[str(source),'-o',str(binary)],check=True,timeout=60);executables[name]=binary
            for name in ('off','on','on','off'):
                rows=subprocess.check_output([str(executables[name]),'bench'],text=True,timeout=30)
                for line in rows.splitlines():mode,iteration,ns=map(int,line.split());timings.setdefault(name,{}).setdefault(mode,[]).append(ns/1000000)
        print(json.dumps(dict(scope='whole production render decoder and actual ABI, CPU mock Objective-C receiver; no Metal device/GPU/Wine/game',scenarios=len(expected),counts={key:{name:results[name][key][0] for name in ('off','on')} for key in expected},mutations_rejected=['missing ICB invalidation','numeric float comparison'],full_native_syntax=['Wine','native'],source_sha256=hashlib.sha256(native.encode()).hexdigest(),bench_ns_per_decode={name:{str(mode):{'median':statistics.median(v),'min':min(v),'max':max(v)} for mode,v in modes.items()} for name,modes in timings.items()},note='bench includes list relink and cheap mock selectors; synthetic diagnostic, not production CPU or FPS'),indent=2))
if __name__=='__main__':main()
