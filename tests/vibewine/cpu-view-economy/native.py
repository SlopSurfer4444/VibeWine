#!/usr/bin/env python3
"""Paired real Metal readback through extracted C++ owners and actual native bridge bodies."""
import argparse, hashlib, importlib.util, json, platform, subprocess, sys, tempfile
sys.dont_write_bytecode=True
from pathlib import Path
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('cpu_owners',HERE/'run.py');owners=importlib.util.module_from_spec(spec);spec.loader.exec_module(owners)
def generate(sources,root,mutate=False):
    text=owners.generate(sources,root)
    # Replace CPU interception with real native bridge bodies. Resource setup is
    # the fixture's; view owners, wrappers and native creation stay production.
    for name in ['NSObject_retain','NSObject_release','MTLTexture_newTextureView','MTLDevice_supportsFamily','MTLBuffer_newTexture']:
      import re
      marker=re.search(r'extern "C" [^\n]+ '+name+r'\(',text).group(0)
      text=text.replace(owners.block(text,marker),'')
    text=text.replace('native_refs[handle]=1;','')
    text=text.replace('// ACTUAL_VIEW_TYPES','')
    # TextureAllocation is fixture setup; its GPU ID comes from actual Metal.
    text=text.replace('gpuResourceID(handle+10000)','gpuResourceID([(id<MTLTexture>)handle gpuResourceID]._impl)')
    if mutate:
      text=owners.replace(text,'    info.pixel_format = format;','    info.pixel_format = WMTPixelFormatR8Uint;')
    native=(root/'src/winemetal/unix/winemetal_unix.c').read_text()
    parts=[]
    for marker in ['MTLPixelFormat to_metal_pixel_format(', 'void\nfill_texture_descriptor(',
      'static inline MTLTextureSwizzleChannels\nto_metal_swizzle(', 'static NTSTATUS\n_MTLBuffer_newTexture(', 'static NTSTATUS\n_MTLTexture_newTextureView(']:
      body=owners.block(native,marker)
      # Mechanical C->ObjC++ pointer conversions, no behavior changes.
      import re
      body=re.sub(r'(struct (\w+) \*params = )obj;',r'\1(struct \2 *)obj;',body)
      body=body.replace('struct WMTTextureInfo *info = params->info.ptr;','struct WMTTextureInfo *info = (struct WMTTextureInfo *)params->info.ptr;')
      parts.append(body)
    # Native definitions are after Metal.hpp declarations and before main.
    text=text.replace('#pragma once','')
    text+='\nusing NTSTATUS=int;\n#define STATUS_SUCCESS 0\n'+'\n'.join(parts)+'\n'+(HERE/'native-adapters.mm').read_text()
    return '#import <Foundation/Foundation.h>\n#import <Metal/Metal.h>\n#include "winemetal_thunks.h"\n'+text

def main():
    p=argparse.ArgumentParser();p.add_argument('--source-dir',type=Path,default=owners.ROOT);p.add_argument('--arch',choices=('native','x86_64'),default='native');a=p.parse_args();root=a.source_dir
    current=[(root/f).read_text() for f in owners.FILES];baseline=[subprocess.check_output(['git','-C',str(root),'show',owners.BASE+':'+f],text=True) for f in owners.FILES]
    result=[]
    with tempfile.TemporaryDirectory(prefix='dxmt-cpu-metal-') as d:
      d=Path(d)
      for name,sources,mutation in [('baseline',baseline,False),('candidate',current,False),('wrong-native-format',current,True)]:
        source=d/(name+'.mm');binary=d/name;source.write_text(generate(sources,root,mutation))
        cmd=['xcrun','clang++','-std=c++20','-O1','-g','-fno-objc-arc','-Wno-deprecated-declarations','-fsanitize=address,undefined','-fno-sanitize-recover=all','-framework','Foundation','-framework','Metal',*[ '-I'+str(root/f) for f in ('src/winemetal','src/util','src/dxmt','include')],str(source),str(HERE/'native-fixture.mm'),'-o',str(binary)]
        # The fixture includes the generated owner header; compile once only.
        source.write_text(source.read_text()+'\n'+(HERE/'native-fixture.mm').read_text());cmd.remove(str(HERE/'native-fixture.mm'))
        if a.arch=='x86_64':cmd+=['-arch','x86_64']
        subprocess.run(cmd,check=True,timeout=90)
        run=[str(binary)]
        if a.arch=='x86_64' and platform.machine()=='arm64':run=['/usr/bin/arch','-x86_64',*run]
        out=subprocess.run(run,capture_output=True,text=True,timeout=60);output=out.stdout+out.stderr
        if mutation:
          assert out.returncode==1 and 'behavioral assertion: buffer GPU pixel oracle' in output,(name,out.returncode,output)
          assert 'AddressSanitizer' not in output and 'runtime error:' not in output,(name,output)
        else:assert out.returncode==0,(name,out.returncode,output)
        result.append(dict(variant=name,exit=out.returncode,output=output.strip()))
    assert result[0]['output']==result[1]['output'],'baseline/candidate native image digest differ'
    print(json.dumps(dict(arch=a.arch,base=owners.BASE,scope='real Metal compute/readback using actual C++ view-owner bodies and native view-creation bridge bodies; fixture resource setup; no Wine/game/runtime install',sanitizers=['address','undefined'],sources_sha256={f:hashlib.sha256((root/f).read_bytes()).hexdigest() for f in owners.FILES+['src/winemetal/unix/winemetal_unix.c']+['tests/vibewine/cpu-view-economy/'+f for f in ('run.py','mocks.hpp','native.py','native-adapters.mm','native-fixture.mm')]},results=result),indent=2))
if __name__=='__main__':main()
