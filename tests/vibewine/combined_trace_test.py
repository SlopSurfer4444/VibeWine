#!/usr/bin/env python3
"""Combined profile production producer/header integration; no game or capture."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
COMPONENT = Path(__file__).resolve().parents[2]
HUB = Path(os.environ.get('DXMT_TRACE_TEST_HUB', COMPONENT.parents[1]))
spec = importlib.util.spec_from_file_location('detail_fixture', HUB / 'tools/telemetry/test_detail_producer.py')
existing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(existing)

class CombinedProducerTests(unittest.TestCase):
    def test_production_headers(self):
        with tempfile.TemporaryDirectory(prefix='dxmt-combined-') as d:
            work = Path(d)
            (work / 'windows.h').write_text(existing.WINDOWS)
            (work / 'util_env.hpp').write_text(existing.ENV)
            binary = work / 'fixture'
            subprocess.run(['xcrun','clang++','-std=c++20','-Wall','-Wextra','-Werror','-pthread',
                '-fsanitize=address,undefined','-I'+d,'-I'+str(COMPONENT/'src/d3d11'),
                str(Path(__file__).with_name('combined_trace_fixture.cpp')),'-o',str(binary)],
                check=True,capture_output=True,text=True,timeout=60)
            cases=[('enabled','combined-bounded','512','fixture-only'),
                   ('cap','combined-bounded','3','fixture-only'),
                   ('no-env','combined-bounded','3','')]
            cases += [('disabled','combined-bounded',limit,'fixture-only') for limit in ('','0','x','32769')]
            cases += [('old',profile,'3','fixture-only') for profile in ('','compile-waits','presentation-waits','stall-waits','invalid')]
            for mode,profile,limit,directory in cases:
                with self.subTest(mode=mode,profile=profile,limit=limit):
                    env=dict(os.environ, DXMT_CS2_TRACE_DIR=directory,DXMT_CS2_TRACE_PROFILE=profile,
                             DXMT_CS2_TRACE_DETAIL_LIMIT=limit,UBSAN_OPTIONS='halt_on_error=1')
                    result=subprocess.run([str(binary),mode],env=env,capture_output=True,text=True,timeout=15)
                    self.assertEqual((result.returncode,result.stdout,result.stderr),(0,'',''),result.stderr)

    def test_actual_swapchain_join_body_and_negative_control(self):
        source=(COMPONENT/'src/d3d11/d3d11_swapchain.cpp').read_text()
        start=source.index('    if (auto gen = cs2trace::generation()) {', source.index('auto &cmd_queue = device_->GetDXMTDevice().queue();', source.index('HRESULT Present')) if 'HRESULT Present' in source else source.index('auto sync_state = SyncFrame('))
        end=source.index('    auto chunk = cmd_queue.CurrentChunk();',start)
        body=source[start:end]
        fixture=r'''#include "cs2_trace.hpp"
#include <cassert>
namespace cs2trace=dxmt::cs2trace;
int main(){
 using namespace cs2trace;auto h=buffer();assert(h);h->control=controlWord(10,CaptureState::Open);
 uint64_t trace_serial_=81;struct{uint64_t frame_id=101;}state;
 struct{uint64_t traceSerial(){return 91;}uint64_t CurrentFrameSeq(){return 8;}}cmd_queue;
 BODY
 auto e=reinterpret_cast<Event*>(reinterpret_cast<char*>(h)+4096);
 auto p=profile();bool joined=p==Profile::CombinedBounded || p==Profile::PresentationWaits;
 auto offset=p==Profile::Full?0u:1u;
 assert(h->next==offset+1+unsigned(joined));
 assert(e[offset].type==25 && e[offset].object==81 && e[offset].padding[0]==91 && e[offset].padding[1]==7);
 if(joined){assert(e[offset+1].type==99 && e[offset+1].object==81 && e[offset+1].padding[0]==101 && e[offset+1].padding[1]==7);
 assert(e[offset].start==e[offset+1].start && e[offset].end==e[offset+1].end && e[offset].reserved==e[offset+1].reserved);}
}'''
        with tempfile.TemporaryDirectory(prefix='dxmt-frame-join-') as d:
            work=Path(d);(work/'windows.h').write_text(existing.WINDOWS);(work/'util_env.hpp').write_text(existing.ENV)
            for mutant in (False,True):
                candidate=body.replace('cs2trace::presentationScopesAllowed(cs2trace::profile())','cs2trace::profile()==cs2trace::Profile::PresentationWaits') if mutant else body
                if mutant:self.assertNotEqual(candidate,body)
                cpp=work/'join.cpp';cpp.write_text(fixture.replace('BODY',candidate));binary=work/'join'
                subprocess.run(['xcrun','clang++','-std=c++20','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-I'+d,'-I'+str(COMPONENT/'src/d3d11'),str(cpp),'-o',str(binary)],check=True,capture_output=True,text=True,timeout=60)
                for profile in ('','presentation-waits','combined-bounded'):
                    env=dict(os.environ,DXMT_CS2_TRACE_DIR='fixture-only',DXMT_CS2_TRACE_PROFILE=profile,DXMT_CS2_TRACE_DETAIL_LIMIT='3')
                    result=subprocess.run([str(binary)],env=env,capture_output=True,text=True,timeout=15)
                    self.assertEqual(result.returncode==0,not(mutant and profile=='combined-bounded'),result.stderr)

    def test_actual_ts_and_eager_clear_callsite_bodies(self):
        ts=(COMPONENT/'src/d3d11/d3d11_pipeline_ts.cpp').read_text()
        start=ts.index('      std::unique_lock<dxmt::mutex> lock(ts_global_mutex, std::defer_lock);')
        ts_body=ts[start:ts.index('\n    }\n    if (state_rasterization_',start)]
        clear=(COMPONENT/'src/dxmt/dxmt_command_clear.hpp').read_text()
        start=clear.index('    const uint64_t trace_owner =')
        clear_body=clear[start:clear.index('#undef CREATE_COMPUTE_PIPELINE',start)+len('#undef CREATE_COMPUTE_PIPELINE')]
        rtv_start=clear.index('      auto pso = cs2trace::helperCompile(')
        rtv_body=clear[rtv_start:clear.index('      if (!pso)',rtv_start)]
        import re
        names=re.findall(r'    CREATE_COMPUTE_PIPELINE\((\w+)\);',clear_body)
        self.assertEqual(len(names),10)
        fixture=r'''#include "cs2_compile_trace.hpp"
#include <cassert>
#include <mutex>
#include <stdexcept>
#include <string>
namespace dxmt {using mutex=std::mutex;}
namespace cs2trace=dxmt::cs2trace;
struct Ref{bool valid=false;bool operator!=(std::nullptr_t)const{return valid;}};
struct Device {bool success=true;bool throwing=false;unsigned calls=0;
 Device& GetMTLDevice(){return *this;}Ref newRenderPipelineState(int,int&){++calls;if(throwing)throw std::runtime_error("fixture");return {success};}};
int main(){
 using namespace cs2trace;auto h=buffer();assert(h);h->control=controlWord(10,CaptureState::Open);generation();
 auto e=reinterpret_cast<Event*>(reinterpret_cast<char*>(h)+4096);Device device;auto device_=&device;
 dxmt::mutex ts_global_mutex;Ref state_rasterization_;const uint64_t trace_serial_=newSerial();int info=0,err=0;
 auto run=[&]{TS_BODY};
 auto before=h->next;run();assert(device.calls==1 && state_rasterization_.valid && h->next==before+2);
 assert(e[before].type==120 && e[before].flags==1 && e[before].padding[0]==1 && e[before].padding[1]==trace_serial_);
 assert(e[before+1].type==120 && e[before+1].flags==1 && e[before+1].padding[0]==2 && e[before+1].padding[1]==trace_serial_);
 assert(e[before].end<=e[before+1].start && e[before].object!=e[before+1].object);
 device.success=false;before=h->next;run();assert(!state_rasterization_.valid && h->next==before+2 && e[before+1].flags==2);
 device.throwing=true;before=h->next;try{run();assert(false);}catch(const std::runtime_error&){}
 assert(h->next==before+2 && e[before+1].flags==3 && ts_global_mutex.try_lock());ts_global_mutex.unlock();
 struct {unsigned calls=0;Ref getComputePipeline(std::string){return {++calls!=4};}}ctx_;
 DECLARATIONS
 before=h->next;
 CLEAR_BODY
 assert(ctx_.calls==10 && h->next==before+10);uint64_t owner=e[before].padding[1];assert(owner && owner!=trace_serial_);
 for(unsigned i=0;i<10;i++)assert(e[before+i].type==120 && e[before+i].flags==(i==3?2u:1u) && e[before+i].padding[0]==3 && e[before+i].padding[1]==owner);
 device.throwing=false;device.success=true;before=h->next;
 {auto &device_=device;int pipeline_info=0;RTV_BODY
 assert(pso.valid);}
 assert(h->next==before+1 && e[before].type==120 && e[before].flags==1 && e[before].padding[0]==5 && !e[before].padding[1]);
}'''
        fixture=fixture.replace('TS_BODY',ts_body).replace('CLEAR_BODY',clear_body).replace('RTV_BODY',rtv_body).replace('DECLARATIONS','Ref '+','.join(name+'_' for name in names)+';')
        with tempfile.TemporaryDirectory(prefix='dxmt-helper-callsite-') as d:
            work=Path(d);(work/'windows.h').write_text(existing.WINDOWS);(work/'util_env.hpp').write_text(existing.ENV)
            cpp=work/'helper.cpp';cpp.write_text(fixture);binary=work/'helper'
            subprocess.run(['xcrun','clang++','-std=c++20','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                '-I'+d,'-I'+str(COMPONENT/'src/d3d11'),str(cpp),'-o',str(binary)],check=True,capture_output=True,text=True,timeout=60)
            env=dict(os.environ,DXMT_CS2_TRACE_DIR='fixture-only',DXMT_CS2_TRACE_PROFILE='combined-bounded',DXMT_CS2_TRACE_DETAIL_LIMIT='512')
            result=subprocess.run([str(binary)],env=env,capture_output=True,text=True,timeout=15)
            self.assertEqual((result.returncode,result.stdout,result.stderr),(0,'',''),result.stderr)

if __name__=='__main__': unittest.main()
