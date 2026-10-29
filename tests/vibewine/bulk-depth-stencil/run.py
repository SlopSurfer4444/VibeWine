#!/usr/bin/env python3
"""Actual initializer/ring/tracker + Metal.hpp, candidate and historical negative control."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import resource
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BASE = 'e3845197978e3fde46a07b584073ba499fd0b25b'
PRODUCTION = ('src/dxmt/dxmt_allocation.hpp', 'src/dxmt/dxmt_allocation.cpp',
              'src/dxmt/dxmt_ring_bump_allocator.hpp', 'src/dxmt/dxmt_resource_initializer.hpp',
              'src/dxmt/dxmt_resource_initializer.cpp')
DEPENDENCIES = ('src/winemetal/Metal.hpp', 'src/winemetal/winemetal.h', 'src/winemetal/wave7_layer_apply.h',
                'include/ftl.hpp', 'src/util/util_math.hpp', 'src/util/util_likely.hpp',
                'src/util/util_error.hpp', 'src/dxmt/dxmt_context.hpp')

def strip(text):
    return '\n'.join(line for line in text.splitlines() if not line.startswith('#include') and line != '#pragma once')

def limits():
    resource.setrlimit(resource.RLIMIT_FSIZE, (4*1024*1024, 4*1024*1024))

def execute(command, scratch, name, timeout, env=None):
    out, err = scratch/(name+'.stdout'), scratch/(name+'.stderr')
    with out.open('wb') as stdout, err.open('wb') as stderr:
        result = subprocess.run(command, stdout=stdout, stderr=stderr, timeout=timeout,
                                check=False, env=env, preexec_fn=limits)
    return result.returncode, out.read_text(errors='replace')+err.read_text(errors='replace')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir',type=Path,default=ROOT)
    parser.add_argument('--arch',choices=('native','x86_64'),default='native')
    parser.add_argument('--gpu',action='store_true',help='native Metal adapter with real depth/stencil plane readback')
    args=parser.parse_args(); root=args.source_dir.resolve()
    if platform.system()!='Darwin':parser.error('requires macOS sandbox-exec; no unsandboxed fallback')
    compiler=shutil.which('clang++'); sandbox=shutil.which('sandbox-exec')
    if not compiler or not sandbox:parser.error('clang++ and sandbox-exec required')
    snapshots={name:(root/name).read_bytes() for name in PRODUCTION+DEPENDENCIES}
    fixtures={name:(HERE/name).read_bytes() for name in ('fixture-prelude.hpp','fixture-main.hpp','fixture-gpu.hpp','run.py')}
    fixtures['fixture-resources.hpp']=(root/'tests/vibewine/initializer-transaction/fixture-resources.hpp').read_bytes()
    context=snapshots[DEPENDENCIES[-1]].decode();begin=context.index('inline std::size_t\nalign_forward_adjustment');end=context.index('\nenum class PipelineStage',begin)
    previous=subprocess.check_output(['git','-C',str(root),'show',BASE+':'+PRODUCTION[-1]])
    results=[]
    with tempfile.TemporaryDirectory(prefix='dxmt-bulk-depth-stencil-') as directory:
        scratch=Path(directory).resolve()
        for name,data in snapshots.items():
            p=scratch/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
        if args.gpu:
            prelude=fixtures['fixture-prelude.hpp'].decode()
            prelude=prelude.replace('inline void *fixture_malloc', '#include "fixture-gpu.hpp"\ninline void *fixture_malloc')
            prelude=prelude.replace('obs.capacities[h]=info->length;', 'obs.capacities[h]=info->length;gpuNewBuffer(h,info);')
            prelude=prelude.replace('std::memcpy(obs.bytes.at(buffer).data()+offset,data.ptr,length);', 'gpuUpdate(buffer,offset,data.ptr,length);std::memcpy(obs.bytes.at(buffer).data()+offset,data.ptr,length);')
            prelude=prelude.replace('assert(command);auto &batch=', 'assert(command);gpuCommit();auto &batch=')
            fixtures['fixture-prelude.hpp']=prelude.encode()
            main=fixtures['fixture-main.hpp'].decode().replace('legacy=argc>1', 'gpu_negative=argc>1&&std::string(argv[1])=="negative";legacy=argc>1')
            main=main.replace('CPU/mock native C API only','real Metal plane blits/readback through test C endpoints')
            main=main.replace('bad_source_range();upload_failure();if(!legacy)scratch_failure();', 'check(gpu_pairs==20,"twenty real Metal plane pairs");std::cout<<"NATIVE_METAL_READBACK_PASSED pairs="<<gpu_pairs<<" updates="<<gpu_updates<<" didModifyRange="<<gpu_notifications<<" bytes="<<gpu_bytes<<"\\n";')
            fixtures['fixture-main.hpp']=main.encode()
        for name,data in fixtures.items():(scratch/name).write_bytes(data)
        for variant,body in ([('candidate',snapshots[PRODUCTION[-1]])] if args.gpu else [('candidate',snapshots[PRODUCTION[-1]]),('baseline',previous)]):
            generated='\n'.join(('#include "fixture-prelude.hpp"','namespace dxmt {',context[begin:end],'}',
                strip(snapshots[PRODUCTION[0]].decode()),strip(snapshots[PRODUCTION[1]].decode()),
                '#include "fixture-resources.hpp"',strip(snapshots[PRODUCTION[2]].decode()),
                '#define private public',strip(snapshots[PRODUCTION[3]].decode()),'#undef private',
                '#define malloc fixture_malloc','#define calloc fixture_calloc','#define free fixture_free',strip(body.decode()),
                '#undef free','#undef calloc','#undef malloc','#include "fixture-main.hpp"'))
            cpp=scratch/(variant+('.mm' if args.gpu else '.cpp'));cpp.write_text(generated);binary=scratch/variant
            command=[compiler,'-std=c++20','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',
                     '-fno-omit-frame-pointer','-I'+str(scratch),'-I'+str(scratch/'src/winemetal'),
                     '-I'+str(scratch/'src/util'),'-I'+str(scratch/'include'),str(cpp),'-o',str(binary)]
            if args.gpu:command+=['-fobjc-arc','-framework','Foundation','-framework','Metal']
            if args.arch=='x86_64':command+=['-arch','x86_64']
            code,output=execute(command,scratch,variant+'-compile',60)
            if code:raise RuntimeError(output)
            runs=[('candidate',[],0)] if variant=='candidate' else [('baseline-data',['legacy'],0),('baseline-target',[],1)]
            if args.gpu:runs=[('native-metal',[],0),('native-metal-negative',['negative'],20)]
            for label,arguments,expected in runs:
                profile='(version 1)(allow default)(deny network*)(deny file-write*)(allow file-write* (subpath '+json.dumps(str(scratch))+'))'
                guest=[str(binary),*arguments]
                if args.arch=='x86_64' and platform.machine()=='arm64':guest=['/usr/bin/arch','-x86_64',*guest]
                command=[sandbox,'-p',profile,*guest]
                code,output=execute(command,scratch,label,45,{**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
                marker='EXPECTED_NEGATIVE native stencil source corruption' if args.gpu else 'behavioral assertion: bulk row bridge call count'
                if code!=expected or (expected and marker not in output) or 'AddressSanitizer' in output or 'runtime error:' in output:
                    raise RuntimeError(f'{label}: unexpected exit {code}\n{output}')
                results.append(dict(variant=label,exit=code,output=output,generated_sha256=hashlib.sha256(generated.encode()).hexdigest()))
    print(json.dumps(dict(arch=args.arch,baseline=BASE,sanitizers=['address','undefined'],
        gpu=args.gpu,scope=('full production initializer/ring/tracker and Metal.hpp; test C endpoints to real native Metal buffers/plane blits/readback; no Wine/runtime/game' if args.gpu else 'full production initializer/ring/tracker and real Metal.hpp wrapper; native C API mocked, no GPU/runtime/game'),
        sources_sha256={name:hashlib.sha256(data).hexdigest() for name,data in snapshots.items()},
        fixtures_sha256={name:hashlib.sha256(data).hexdigest() for name,data in fixtures.items()},results=results),indent=2))

if __name__=='__main__':main()
