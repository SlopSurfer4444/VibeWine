#!/usr/bin/env python3
"""Count actual scope clocks with the existing OS mapping and a clock seam."""
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
original=(source/'src/d3d11/cs2_trace.hpp').read_text()
clock='inline uint64_t now() {return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}'
replacement='''inline std::atomic<uint64_t> fixture_clock_calls{0},fixture_clock_ns{1};
inline uint64_t fixture_clock_step=100000;
inline uint64_t now(){fixture_clock_calls++;return fixture_clock_ns.fetch_add(fixture_clock_step);}'''
assert original.count(clock)==1
with tempfile.TemporaryDirectory(prefix='dxmt-observer-clocks-') as directory:
    work=Path(directory)
    windows=mapping.WINDOWS.replace('inline HANDLE CreateFileA(', 'inline unsigned fixture_create_calls=0;\ninline HANDLE CreateFileA(').replace('return (void*)1;}','fixture_create_calls++;return (void*)1;}',1)
    (work/'windows.h').write_text(windows);(work/'util_env.hpp').write_text(mapping.ENV)
    (work/'cs2_trace_protocol.hpp').write_bytes((source/'src/d3d11/cs2_trace_protocol.hpp').read_bytes())
    command=[os.environ.get('CXX','clang++'),'-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+directory]
    def build(text,name):
        (work/'cs2_trace.hpp').write_text(text.replace(clock,replacement))
        binary=work/name;subprocess.run(command+[str(here/'fixture.cpp'),'-o',str(binary)],check=True,timeout=60);return binary
    def run(binary,profile='',disabled=False,check=True):
        result=subprocess.run([str(binary),'disabled' if disabled else 'armed'],capture_output=True,text=True,timeout=20,env={**os.environ,
            'DXMT_CS2_TRACE_DIR':'' if disabled else 'fixture-only','DXMT_CS2_TRACE_PROFILE':profile,'DXMT_CS2_TRACE_DETAIL_LIMIT':'1000','UBSAN_OPTIONS':'halt_on_error=1'})
        if check and result.returncode:print(result.stdout+result.stderr,flush=True);result.check_returncode()
        return result
    binary=build(original,'fixture')
    for profile in ('','compile-waits','presentation-waits','invalid'):
        result=run(binary,profile);print(profile or 'full',result.stdout.strip())
    print(run(binary,disabled=True).stdout.strip())
    mutant=original.replace('bool active=gen && profileAllows(type)','bool active=gen')
    assert mutant!=original;result=run(build(mutant,'late-filter'),'compile-waits',check=False)
    assert result.returncode and 'fixture_clock_calls==clocks+expected+marker' in result.stderr,result.stderr
    mutant=original.replace('auto value=fn();result=uint32_t(value);','auto value=active?fn():17;result=uint32_t(value);')
    assert mutant!=original;result=run(build(mutant,'skipped-operation'),'compile-waits',check=False)
    assert result.returncode and 'native_calls==calls+3' in result.stderr,result.stderr
    if sum(p.stat().st_size for p in work.rglob('*') if p.is_file())>64*1024*1024:raise RuntimeError('scratch exceeds64MiB')
    print('PASS source mutants: late filter clock overhead and skipping excluded mandatory API operation both rejected')
