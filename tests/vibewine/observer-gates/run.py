#!/usr/bin/env python3
"""Exact production observation paths with counted native transport endpoints."""
from pathlib import Path
import importlib.util
import os
import resource
import subprocess
import tempfile

resource.setrlimit(resource.RLIMIT_CORE,(0,0))
here=Path(__file__).resolve().parent;source=here.parents[2]
hub=Path(os.environ.get('DXMT_TRACE_TEST_HUB',source.parents[1]))
spec=importlib.util.spec_from_file_location('mapping_fixture',hub/'tools/telemetry/test_detail_producer.py')
mapping=importlib.util.module_from_spec(spec);spec.loader.exec_module(mapping)
text=(source/'src/dxmt/dxmt_presenter.cpp').read_text()
presenter=text[text.index('namespace {\nW7FBRequest feedbackRequest'):text.index('\nWMT::MetalDrawable\nPresenter::encodeCommands')]
text=(source/'src/dxmt/dxmt_command_queue.cpp').read_text()
begin=text.index('    if (chunk.trace_generation &&',text.index('CommandQueue::WaitForFinishThread'))
queue=text[begin:text.index('    if (chunk.attached_cmdbuf.status() == WMTCommandBufferStatusError)',begin)]
metal=(source/'src/winemetal/Metal.hpp').read_text();begin=metal.index('  uint64_t\n  gpuStartTime()')
getters=metal[begin:metal.index('  void\n  encodeSignalEvent',begin)]
metal_h=(source/'src/winemetal/winemetal.h').read_text();begin=metal_h.index('enum WMTCommandBufferProperty :')
property_enum=metal_h[begin:metal_h.index('};',begin)+2]
fixture=(here/'fixture.cpp.in').read_text().replace('// EXACT_PROPERTY_ENUM',property_enum).replace('// EXACT_GPU_GETTERS',getters).replace('// EXACT_PRESENTER',presenter).replace('// EXACT_QUEUE_OBSERVATION',queue)
with tempfile.TemporaryDirectory(prefix='dxmt-observer-gates-') as directory:
    work=Path(directory);(work/'windows.h').write_text(mapping.WINDOWS);(work/'util_env.hpp').write_text(mapping.ENV)
    command=[os.environ.get('CXX','clang++'),'-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+directory,'-I'+str(source/'src/d3d11'),'-I'+str(source/'src/winemetal')]
    def build(text,name):
        path=work/(name+'.cpp');path.write_text(text);binary=work/name;subprocess.run(command+[str(path),'-o',str(binary)],check=True,timeout=60);return binary
    def run(binary,profile,check=True):
        result=subprocess.run([str(binary)],capture_output=True,text=True,timeout=20,env={**os.environ,'DXMT_CS2_TRACE_DIR':'fixture-only','DXMT_CS2_TRACE_PROFILE':profile,'DXMT_CS2_TRACE_DETAIL_LIMIT':'1000','UBSAN_OPTIONS':'halt_on_error=1'})
        if check and result.returncode:print(result.stdout+result.stderr,flush=True);result.check_returncode()
        return result
    binary=build(fixture,'fixture')
    for profile in ('','compile-waits','presentation-waits','invalid'):
        result=run(binary,profile);print(profile or 'full',result.stdout.strip())
    mutant=fixture.replace('  if (!feedback_allowed)\n    return;\n','')
    assert mutant!=fixture;result=run(build(mutant,'ungated-feedback'),'compile-waits',False)
    assert result.returncode and 'feedback_native_calls()==0' in result.stderr,result.stderr
    mutant=fixture.replace(' && cs2trace::profileAllows(24)','')
    assert mutant!=fixture;result=run(build(mutant,'ungated-gpu'),'compile-waits',False)
    assert result.returncode and 'gpu_calls==0' in result.stderr,result.stderr
    if sum(path.stat().st_size for path in work.rglob('*') if path.is_file())>64*1024*1024:raise RuntimeError('scratch exceeds64MiB')
    print('PASS source-mutant controls: removed native feedback gate and GPU property gate both fail')
