#!/usr/bin/env python3
"""Whole render emission + exact GS/TS methods + real thunk/native interpreter, CPU receiver."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import tempfile

HERE=Path(__file__).resolve().parent
REPO=HERE.parents[2]
PATHS=['src/dxmt/dxmt_context.cpp','src/dxmt/dxmt_command.hpp','src/dxmt/dxmt_pass_optimizations.hpp',
       'src/winemetal/winemetal_thunks.c','src/winemetal/unix/winemetal_unix.c']

def block(text,marker):
    start=text.index(marker);cursor=text.index('{',start)+1;depth=1
    while depth:
        depth+=(text[cursor]=='{')-(text[cursor]=='}');cursor+=1
    return text[start:cursor]

def generate(sources):
    cpp,command,helper,thunk,native=(sources[p] for p in PATHS)
    start=cpp.index('      data->fence_wait.forEach(',cpp.index('ArgumentEncodingContext::flushCommands('))
    end=cpp.index('      data->~RenderEncoderData();',start)
    replacements={
      'GS_METHOD':block(command,'  void\n  MarshalGSDispatchArguments('),
      'TS_METHOD':block(command,'  void\n  MarshalTSDispatchArguments('),
      'RESOURCE_METHOD':block(command,'  void\n  UseMarshalTaskResources('),
      'PACKET_METHOD':block(command,'  void\n  MarshalDispatchArgumentsPacket('),
      'PRODUCTION_DEDUP':block(helper,'inline size_t\nDeduplicateRenderResidency('),
      'PRODUCTION_EMISSION':cpp[start:end],
    }
    fixture=(HERE/'fixture.cpp.in').read_text()
    for marker,body in replacements.items():
        assert fixture.count('// '+marker)==1
        fixture=fixture.replace('// '+marker,body)
    exact_thunk=block(thunk,'WINEMETAL_API void\nMTLRenderCommandEncoder_encodeCommands(')
    exact_native=block(native,'static NTSTATUS\n_MTLRenderCommandEncoder_encodeCommands(')
    return fixture,(HERE/'thunk.c.in').read_text().replace('// PE_THUNK',exact_thunk),\
        (HERE/'native.m.in').read_text().replace('// NATIVE_HANDLER',exact_native)

def mutate(sources,kind):
    changed=dict(sources);header='src/dxmt/dxmt_command.hpp';context='src/dxmt/dxmt_context.cpp'
    if kind=='drop-unbind':
        before='    packet.draw.next.set(&packet.unbind);';after='    packet.draw.next.set(nullptr);'
    elif kind=='wrong-resource-usage':
        before='    packet[1].usage = WMTResourceUsageWrite;';after='    packet[1].usage = WMTResourceUsageRead;'
    elif kind=='wrong-draw-instance':
        before='    packet.draw.instance_count = 1;'
        after='    packet.draw.instance_count = 2;'
    elif kind=='early-barrier':
        body=block(changed[context],'      if (data->gs_arg_marshal_tasks.size() > 0 || data->ts_arg_marshal_tasks.size() > 0)')
        assert changed[context].count(body)==1
        changed[context]=changed[context].replace(body,'')
        marker='      if (data->gs_arg_marshal_tasks.size()) {'
        assert changed[context].count(marker)==1
        changed[context]=changed[context].replace(marker,body+'\n'+marker)
        return changed
    elif kind=='bypass':
        for p in (header,context):changed[p]=changed[p].replace('defined(DXMT_MARSHAL_PACKETS) && DXMT_MARSHAL_PACKETS','defined(DXMT_MARSHAL_PACKETS) && 0')
        return changed
    else:raise ValueError(kind)
    assert changed[header].count(before)==1
    changed[header]=changed[header].replace(before,after)
    return changed

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir',type=Path,default=REPO)
    parser.add_argument('--arch',choices=('native','x86_64'),default='native')
    parser.add_argument('--benchmark',action='store_true')
    args=parser.parse_args();root=args.source_dir
    compiler=shutil.which('clang++');clang=shutil.which('clang');assert compiler and clang
    sources={p:(root/p).read_text() for p in PATHS}
    on=['-DDXMT_MARSHAL_PACKETS=1']
    variants=[('default',[],None),('off',['-DDXMT_MARSHAL_PACKETS=0'],None),('on',on,None),
              ('combined',on+['-DDXMT_RENDER_PASS_FUSION=1','-DDXMT_COMPUTE_PASS_FUSION=1','-DDXMT_PHYSICAL_RESIDENCY_DEDUP=1'],None),
              ('w4-reference',on+['-DDXMT_PERF_REF_W4_BATCHING=1'],None)]
    variants += [(k,on,k) for k in ('drop-unbind','wrong-resource-usage','wrong-draw-instance','early-barrier','bypass')]
    results=[];benchmarks=[]
    with tempfile.TemporaryDirectory(prefix='dxmt-transport-packets-') as temporary:
        scratch=Path(temporary)
        def common(sanitize):
            directory=scratch/('sanitized' if sanitize else 'release');directory.mkdir()
            _,thunk,native=generate(sources)
            (directory/'thunk.c').write_text(thunk);(directory/'native.m').write_text(native)
            flags=['-O1' if sanitize else '-O2','-g','-Wall','-Wextra','-Werror','-fno-lto',
                   *['-I'+str(root/p) for p in ('src/winemetal','src/dxmt','src/util','src/airconv','include')]]
            if args.arch=='x86_64':flags+=['-arch','x86_64']
            if sanitize:flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
            for source in ('thunk.c','native.m'):
                subprocess.run([clang,*flags,'-c',str(directory/source),'-o',str(directory/(source+'.o'))],check=True,timeout=90)
            return directory,flags
        directory,flags=common(True)
        def build_run(name,contents,defines,directory,flags,bench=False):
            text,_,_=generate(contents);cpp=directory/(name+'.cpp');binary=directory/name;cpp.write_text(text)
            subprocess.run([compiler,'-std=c++20',*flags,*defines,str(cpp),str(directory/'thunk.c.o'),str(directory/'native.m.o'),
                            '-framework','Foundation','-framework','Metal','-o',str(binary)],check=True,timeout=90)
            execution=[str(binary)]
            if args.arch=='x86_64' and platform.machine()=='arm64':execution=['/usr/bin/arch','-x86_64',*execution]
            if bench:return execution
            return subprocess.run(execution,text=True,capture_output=True,check=True,timeout=30).stdout
        observed={}
        for name,defines,mutation in variants:
            output=build_run(name,mutate(sources,mutation) if mutation else sources,defines,directory,flags)
            rows=[list(map(int,line.split())) for line in output.splitlines()];assert len(rows)==400
            observed[name]=rows
        for name,rows in observed.items():
            equal=all(a[4:]==b[4:] for a,b in zip(rows,observed['off']))
            def saved(row):return (row[1]+3 if row[1] else 0)+(row[2]+3 if row[2] else 0)
            counts=all(a[3]==b[3]-saved(a) for a,b in zip(rows,observed['off']))
            if name in ('drop-unbind','wrong-resource-usage','wrong-draw-instance','early-barrier','bypass'):
                assert not (equal and counts),f'{name} escaped command/count oracle'
            else:
                assert equal,f'{name} changed native command/payload order'
                if name in ('default','off'):assert rows==observed['off']
                if name=='on':assert counts
                if name=='combined':
                    for a,b in zip(rows,observed['off']):assert a[3]<=b[3]-saved(a)
                if name=='w4-reference':
                    for a,b in zip(rows,observed['off']):assert a[3]>=b[3]
            results.append(dict(variant=name,scenarios=len(rows),native_command_equality=equal,packet_count_contract=counts))
        if args.benchmark:
            directory,flags=common(False)
            executions={name:build_run('bench-'+name,sources,defines,directory,flags,True)
                        for name,defines in [('off',['-DDXMT_MARSHAL_PACKETS=0']),('on',on)]}
            for repeat,order in enumerate([['off','on'],['on','off'],['off','on']]):
                for name in order:
                    output=subprocess.run([*executions[name],'bench'],text=True,capture_output=True,check=True,timeout=45).stdout
                    rows=[line.split() for line in output.splitlines()]
                    assert all(row[0]=='bench' for row in rows)
                    benchmarks.append(dict(repeat=repeat,variant=name,rows=[dict(tasks=int(r[1]),median_ns=float(r[2]),entrypoints=int(r[3]),native_commands=int(r[4])) for r in rows]))
    assert "option('vibewine_marshal_packets', type : 'boolean', value : false," in (root/'meson.options').read_text()
    assert "'vibewine_marshal_packets' : 'DXMT_MARSHAL_PACKETS'" in (root/'meson.build').read_text()
    hashes=PATHS+['src/winemetal/Metal.hpp','src/winemetal/winemetal.h','src/winemetal/winemetal_thunks.h','src/dxmt/dxmt_command_constants.hpp',
                  'tests/vibewine/transport-packets/run.py','tests/vibewine/transport-packets/fixture.cpp.in','tests/vibewine/transport-packets/native.m.in','tests/vibewine/transport-packets/thunk.c.in']
    print(json.dumps(dict(scope='CPU real wrappers + exact PE opcode38 thunk + complete real native interpreter with Objective-C observer; separate TUs, no LTO; no Wine/device/GPU',
                         arch=args.arch,sources_sha256={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in hashes},
                         results=results,benchmark_scope='Host CPU proxy; includes observer selector cost; not Wine transition/driver timing',benchmarks=benchmarks),indent=2))

if __name__=='__main__':main()
