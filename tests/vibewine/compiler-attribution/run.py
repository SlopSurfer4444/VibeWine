#!/usr/bin/env python3
"""Production-extracted attribution paths, no native runtime/database/game."""
from pathlib import Path
import argparse
import importlib.util
import os
import resource
import subprocess
import tempfile

parser=argparse.ArgumentParser()
parser.add_argument('--tsan',action='store_true')
args=parser.parse_args()
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
here=Path(__file__).resolve().parent
source=here.parents[2]
hub=Path(os.environ.get('DXMT_TRACE_TEST_HUB',source.parents[1]))
spec=importlib.util.spec_from_file_location('mapping_fixture',hub/'tools/telemetry/test_detail_producer.py')
mapping=importlib.util.module_from_spec(spec);spec.loader.exec_module(mapping)
shared=here.parent/'compiler-persistence'
shader=(source/'src/d3d11/d3d11_shader.cpp').read_text()
shader=shader[shader.index('template <typename Proc>'):shader.index('\ntemplate <>',shader.index('template <typename Proc>'))]
cache=(source/'src/dxmt/dxmt_shader_cache.cpp').read_text()
cache=cache[cache.index('WMT::Reference<WMT::DispatchData>\nShaderCache::read'):cache.index('\n} // namespace dxmt',cache.index('ShaderCache::read'))]
base=(source/'src/d3d11/d3d11_shader.hpp').read_text()
base=base[base.index('class ThreadpoolWork {'):base.index('\nclass CompiledShader',base.index('class ThreadpoolWork {'))]
prefix=(shared/'shader_fixture.cpp.in').read_text().split('\nint main()')[0]
prefix=prefix[:prefix.index('template <> struct task_trait<ThreadpoolWork *>')]+ '}\n'
begin=prefix.index('struct ThreadpoolWork {');end=prefix.index('\nstruct CompiledShader',begin)
prefix=prefix[:begin]+base+prefix[end:]
prefix=prefix.replace('return cache_hit ? WMT::DispatchData{std::make_shared<int>(42)} : WMT::DispatchData{};','return cache.find(digest);')
prefix=prefix.replace('ShaderCache::ShaderCache(WMTMetalVersion)', 'ShaderCache::ShaderCache(WMTMetalVersion version)')
prefix=prefix.replace('writer_state_->writer = WMT::CacheWriter{fake_writer};','writer_state_->writer = WMT::CacheWriter{fake_writer}; scache_reader_=WMT::CacheReader{version!=99};trace_serial_=cs2trace::newSerial();')
prefix=prefix.replace('Function newFunction(const char *) { return {!fail_function}; }','Function newFunction(const char *) { if(throw_function)throw 8;return {!fail_function && !fail_cached_function.exchange(false)}; }')
prefix=prefix.replace('error.failed = fail_library','if(throw_library)throw 9;error.failed = fail_library')
prefix=prefix.replace('namespace WMT {','inline bool throw_library=false,throw_function=false;\ninline std::atomic<bool> fail_cached_function{false};\nnamespace WMT {',1)

with tempfile.TemporaryDirectory(prefix='dxmt-compiler-attribution-') as directory:
    work=Path(directory)
    (work/'windows.h').write_text(mapping.WINDOWS);(work/'util_env.hpp').write_text(mapping.ENV)
    # Only clock observation changes; exact admission/budget/profile code executes.
    trace=(source/'src/d3d11/cs2_trace.hpp').read_text().replace('inline uint64_t now() {','inline std::atomic<uint64_t> fixture_clock_calls{0};\ninline uint64_t now() {fixture_clock_calls++;',1)
    (work/'cs2_trace.hpp').write_text(trace)
    for name in ['cs2_compile_trace.hpp','cs2_trace_protocol.hpp']:(work/name).write_bytes((source/'src/d3d11'/name).read_bytes())
    mocks=(shared/'mocks.hpp').read_text().replace('namespace dxmt {','inline thread_local std::function<void()> before_mutex_lock;\nnamespace dxmt {',1).replace('void lock() { native.lock(); }','void lock() { if(before_mutex_lock)before_mutex_lock();native.lock(); }')
    mocks=mocks.replace('struct CacheReader {\n  explicit operator bool() const { return false; }\n};', '''struct CacheReader {
  bool valid=false;explicit operator bool() const { return valid; }
  DispatchData get_checked(const void *,uint64_t,uint32_t &status) {
    if(reader_entry)reader_entry();if(reader_throw)throw 17;
    status=reader_hit?100:101;return reader_hit?DispatchData{std::make_shared<int>(42)}:DispatchData{};
  }
};''')
    mocks=mocks.replace('namespace WMT {','inline bool reader_hit=false,reader_throw=false;\ninline std::function<void()> reader_entry;\nnamespace WMT {',1)
    (work/'mocks.hpp').write_text(mocks)
    for name in ['thread.hpp','util_win32_compat.h','Metal.hpp']:(work/name).write_text('#include "mocks.hpp"\n')
    (work/'compiler_cache_compat.hpp').write_text('#pragma once\nconstexpr uint64_t kDXMTCompilerCacheCompatibility=0x1a00000002ull;\n')
    fixture=(here/'fixture.cpp.in').read_text().replace('// EXISTING_SHADER_FIXTURE_PREFIX',prefix).replace('// EXACT_CACHE_READ','namespace dxmt {\n'+cache+'\n}')
    sanitizer='thread' if args.tsan else 'address,undefined'
    command=[os.environ.get('CXX','clang++'),'-std=c++20','-O1','-g','-Wno-inconsistent-missing-override','-pthread','-fsanitize='+sanitizer,'-I'+str(work),'-I'+str(source/'src/dxmt')]
    def build(text,name):
        path=work/(name+'.cpp');path.write_text(text);binary=work/name
        subprocess.run(command+[str(path),'-o',str(binary)],check=True,timeout=60)
        return binary
    binary=build(fixture.replace('// EXACT_GENERAL_SHADER_TASK',shader),'fixture')
    def run(binary,mode,profile='',limit='1000',check=True):
        return subprocess.run([str(binary),mode],check=check,timeout=20,capture_output=True,text=True,env={**os.environ,
            'DXMT_CS2_TRACE_DIR':'fixture-only','DXMT_CS2_TRACE_PROFILE':profile,'DXMT_CS2_TRACE_DETAIL_LIMIT':limit,
            'UBSAN_OPTIONS':'halt_on_error=1','TSAN_OPTIONS':'symbolize=0:abort_on_error=0:halt_on_error=1:exitcode=66'})
    for mode,profile,limit in [('armed','','1000'),('armed','compile-waits','1000'),('disabled','','0'),('excluded','presentation-waits','1000'),('excluded','invalid','1000'),('closed','','1000'),('budget','compile-waits','2')]:
        result=run(binary,mode,profile,limit,check=False)
        if result.returncode:print(result.stdout+result.stderr,flush=True)
        result.check_returncode();print(mode,profile or 'full',result.stdout.strip())
    # Negative controls mutate the actual extracted wiring, not a parallel model.
    no_parent=shader.replace('cs2trace::CompilerWorkScope work_scope(task_trace_generation.load(std::memory_order_acquire), task_trace_serial);','')
    assert no_parent!=shader
    mutant=build(fixture.replace('// EXACT_GENERAL_SHADER_TASK',no_parent),'missing-parent')
    rejected=run(mutant,'armed',check=False)
    assert rejected.returncode!=0 and 'e.padding[0]==task.task_trace_serial' in rejected.stderr, rejected.stderr
    after_lock=fixture.replace('  cs2trace::CacheReaderLockScope wait(wait_trace, trace_serial_);\n','').replace('    wait.acquired();','    cs2trace::CacheReaderLockScope wait(wait_trace, trace_serial_);\n    wait.acquired();')
    assert after_lock!=fixture
    mutant=build(after_lock.replace('// EXACT_GENERAL_SHADER_TASK',shader),'late-lock-timer')
    rejected=run(mutant,'armed',check=False)
    assert rejected.returncode!=0 and 'wait.start<released' in rejected.stderr, rejected.stderr
    if sum(path.stat().st_size for path in work.rglob('*') if path.is_file())>64*1024*1024:raise RuntimeError('scratch exceeds64MiB')
    print('PASS negative controls: missing task parent and starting timer after mutex acquisition both rejected')
