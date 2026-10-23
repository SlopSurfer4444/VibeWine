"""Production scheduler, task base, trait and producer -> actual analyzer joins."""
import os,pathlib,subprocess,tempfile,unittest
from analyze import analyze
from test_analyze import event,rows
from test_detail_producer import ROOT,WINDOWS,ENV

FIXTURE=r'''
#include "cs2_trace.hpp"
#include "mocks.hpp"
#include "dxmt_tasks.hpp"
#include <iostream>
namespace dxmt {
// TASK_BASE
// ACTUAL_TRAIT
}
struct Task:dxmt::ThreadpoolWork {
 std::atomic<bool> done{false}; std::function<dxmt::ThreadpoolWork*()> run;
 dxmt::post_completion_work post;
 dxmt::ThreadpoolWork* RunThreadpoolWork() override{return run();}
 bool GetIsDone() override{return done.load();}
 void SetIsDone(bool value) override{done.store(value);}
 dxmt::post_completion_work TakePostCompletionWork() override{return std::move(post);}
};
int main(){
 using namespace dxmt::cs2trace;auto h=buffer();h->control=controlWord(7,CaptureState::Open);
 auto events=(Event*)((char*)h+4096);
 Task shader,pipeline;std::atomic<bool> io_entered{false},release{false};
 shader.run=[&]()->dxmt::ThreadpoolWork*{return &shader;};
 shader.post={[&]{io_entered=true;until([&]{return release.load();});},64,1};
 pipeline.run=[&]()->dxmt::ThreadpoolWork*{return shader.done.load()?&pipeline:&shader;};
 {
  dxmt::task_scheduler<dxmt::ThreadpoolWork*> scheduler(true);
  scheduler.submit(&pipeline);
  until([&]{auto n=std::atomic_ref<uint64_t>(h->next).load();for(uint64_t i=0;i<n;i++)if(std::atomic_ref<uint64_t>(events[i].committed).load(std::memory_order_acquire)&&events[i].type==82)return true;return false;});
  scheduler.submit(&shader);
  until([&]{return pipeline.done.load()&&io_entered.load();});
  release=true;
 }
 for(uint64_t i=0;i<h->next;i++){auto&e=events[i];assert(e.committed==i+1);std::cout<<e.type<<' '<<e.start<<' '<<e.end<<' '<<e.object<<' '<<e.flags<<' '<<e.padding[0]<<' '<<e.padding[1]<<'\n';}
}
'''

class SchedulerProducerTests(unittest.TestCase):
    def test_production_scheduler_events_join_after_dependency_and_optional_io(self):
        source=ROOT/'components/dxmt'
        shader=(source/'src/d3d11/d3d11_shader.hpp').read_text()
        taskbase=shader[shader.index('class ThreadpoolWork {'):shader.index('class CompiledShader :')]
        pipeline=(source/'src/d3d11/d3d11_pipeline_cache.cpp').read_text()
        trait=pipeline[pipeline.index('template <> struct task_trait<ThreadpoolWork *>'):pipeline.index('class PipelineCache :')]
        with tempfile.TemporaryDirectory(prefix='detail-scheduler-') as td:
            p=pathlib.Path(td)
            (p/'windows.h').write_text(WINDOWS);(p/'util_env.hpp').write_text(ENV)
            for name in ('thread.hpp','util_win32_compat.h'):(p/name).write_text('#include "mocks.hpp"\n')
            fixture=p/'fixture.cpp';fixture.write_text(FIXTURE.replace('// TASK_BASE',taskbase).replace('// ACTUAL_TRAIT',trait))
            binary=p/'fixture'
            command=['xcrun','clang++','-std=c++20','-pthread','-fsanitize=address,undefined','-I'+td,'-I'+str(source/'tests/vibewine/compiler-persistence'),'-I'+str(source/'src/d3d11'),'-I'+str(source/'src/dxmt'),str(fixture),'-o',str(binary)]
            r=subprocess.run(command,capture_output=True,text=True,timeout=60);self.assertEqual(r.returncode,0,r.stderr)
            env=dict(os.environ,DXMT_CS2_TRACE_DIR='fixture-only',DXMT_CS2_TRACE_DETAIL_LIMIT='32768')
            r=subprocess.run([str(binary)],env=env,capture_output=True,text=True,timeout=15)
            self.assertEqual((r.returncode,r.stderr),(0,''),r.stderr)
            es=[]
            for line in r.stdout.splitlines():
                t,start,end,obj,flags,p0,p1=map(int,line.split());es.append(event(t,start,end,obj=obj,flags=flags,seq=p0,frame=p1))
            result=analyze(rows(es))['detailed_probes'];self.assertTrue(result['capture_usable'],result)
            self.assertEqual(len(result['tasks']),2)
            self.assertTrue(all(t['join_state']=='complete_observed_task' for t in result['tasks']))
            linked=next(t for t in result['tasks'] if t['dependency_waits']);self.assertEqual(linked['dependency_waits'][0]['dependency_join'],'observed_ready_task')
            saved=next(t for t in result['tasks'] if t['persistence']);self.assertEqual(saved['persistence']['callable_result'],'returned_result_unknown')
            self.assertEqual(saved['persistence']['charged_bytes'],64);self.assertIsNone(saved['persistence']['cache_write_status'])

if __name__=='__main__':unittest.main()
