"""Exercise the actual completion producer; only Windows endpoints are mocked."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import test_detail_producer as existing

ROOT = Path(__file__).resolve().parents[2]
WINDOWS = existing.WINDOWS + r'''
struct FILETIME { uint32_t dwLowDateTime=0,dwHighDateTime=0; };
inline uint64_t fixture_cpu=100;
inline bool fixture_cpu_valid=true;
inline unsigned fixture_cpu_calls=0;
inline HANDLE GetCurrentThread(){return (void*)1;}
inline bool GetThreadTimes(HANDLE,FILETIME*,FILETIME*,FILETIME* kernel,FILETIME* user){
 ++fixture_cpu_calls;
 if(!fixture_cpu_valid)return false;
 kernel->dwLowDateTime=uint32_t(fixture_cpu);kernel->dwHighDateTime=uint32_t(fixture_cpu>>32);
 user->dwLowDateTime=0;user->dwHighDateTime=0;fixture_cpu+=40;return true;
}
'''
FIXTURE = r'''
void fixture_barrier(int);
#define DXMT_TRACE_WRITER_BARRIER(point) fixture_barrier(point)
#include "cs2_trace_completion.hpp"
#include <cassert>
#include <cstring>
using namespace dxmt::cs2trace;
static bool close_at_publication=false;
void fixture_barrier(int point){
 if(point==1 && close_at_publication){close_at_publication=false;auto h=buffer();
  h->control=controlWord(controlGeneration(h->control),CaptureState::Closing,controlWriters(h->control));}
}
int main(int argc,char **argv){
 assert(argc==2);auto h=buffer();h->control=controlWord(7,CaptureState::Open);
 auto e=(Event*)((char*)h+4096);
 StallCompletionWait card(7,42,1,0,1,6);
 if(!std::strcmp(argv[1],"disabled")){
  assert(!card.active());card.begin();assert(!card.finish());assert(fixture_cpu_calls==0);
  card.publish(3,4,10,20);assert(!h->next || (h->next==1 && e[0].type==95));return 0;
 }
 assert(card.active());assert(h->next==1 && e[0].type==95);
 card.initializer_observed=5;card.ready_encode=3;card.ready_commit=2;card.ongoing=1;card.coherent=0;card.latency=0;
 card.begin();card.start=1;
 if(!std::strcmp(argv[1],"cpu-invalid"))fixture_cpu_valid=false;
 if(!std::strcmp(argv[1],"cpu-backward"))fixture_cpu=0;
 assert(card.finish());
 if(!std::strcmp(argv[1],"stale")){
  h->control=controlWord(6,CaptureState::Open);card.publish(3,4,10,20);
  assert(h->next==3 && e[1].type==95 && e[2].type==89 && e[2].flags==2 && e[2].padding[0]==7);
  return 0;
 }
 if(!std::strcmp(argv[1],"stop-before")){
  h->control=controlWord(7,CaptureState::Closed);card.publish(3,4,10,20);assert(h->next==1);return 0;
 }
 if(!std::strcmp(argv[1],"stop-during"))close_at_publication=true;
 unsigned after=!std::strcmp(argv[1],"error")?5:4;
 uint64_t gpu_end=!std::strcmp(argv[1],"gpu-backward")?9:!std::strcmp(argv[1],"gpu-zero")?10:30;
 if(!std::strcmp(argv[1],"shared-cap"))detailRecord(7,80,10,10,99,2,100,1);
 card.publish(3,after,10,gpu_end);
 if(!std::strcmp(argv[1],"shared-cap")){
  assert(h->next==8 && e[1].type==80 && e[2].type==111 && e[6].type==115 && e[7].type==89 && e[7].padding[0]==6);
  return 0;
 }
 if(!std::strcmp(argv[1],"cap")){
  assert(h->next==5 && e[1].type==111 && e[3].type==113 && e[4].type==89 && e[4].flags==1);
  card.publish(3,4,10,20);assert(h->next==5);return 0;
 }
 assert(h->next==7);for(unsigned n=1;n<=6;n++){
  assert(e[n].type==110+n && e[n].reserved==7 && e[n].object==42 && e[n].thread==456);
  assert(e[n].start==card.start && e[n].end==card.end && e[n].committed==n+1);
 }
 const bool cpu=!std::strcmp(argv[1],"cpu-invalid")||!std::strcmp(argv[1],"cpu-backward")?false:true;
 const bool gpu=after==4 && gpu_end>=10;
 assert(e[1].flags==(3u|(after<<3)|(unsigned(cpu)<<6)|(unsigned(gpu)<<7)));
 assert(e[1].padding[0]==1 && !e[1].padding[1]);
 assert(e[2].padding[0]==1 && e[2].padding[1]==6);
 assert(e[3].padding[0]==5 && e[3].padding[1]==(cpu?40:0));
 assert(e[4].flags==1 && e[4].padding[0]==3 && e[4].padding[1]==2);
 assert(!e[5].padding[0] && !e[5].padding[1]);
 assert(e[6].padding[0]==(gpu?gpu_end-10:0) && !e[6].padding[1]);
 assert(controlWriters(h->control)==0);
 if(!std::strcmp(argv[1],"stop-during"))assert(controlState(h->control)==CaptureState::Closed);
 // No begin records: a new short wait contributes nothing until its caller
 // observes the inclusive wall threshold. Unarmed/stale chunks never upgrade.
 if(controlState(h->control)==CaptureState::Open){
  StallCompletionWait short_wait(7,42,2,1,~0ull,0);short_wait.begin();assert(!short_wait.finish());assert(h->next==7);
  StallCompletionWait prearm(0,42,3,1,~0ull,0);assert(!prearm.active());
 }
}
'''

class CompletionProducerTests(unittest.TestCase):
    def test_actual_header_threshold_cpu_gpu_cap_generation_and_stop(self):
        with tempfile.TemporaryDirectory(prefix='completion-producer-') as td:
            work = Path(td)
            (work/'windows.h').write_text(WINDOWS)
            (work/'util_env.hpp').write_text(existing.ENV)
            (work/'fixture.cpp').write_text(FIXTURE)
            built = subprocess.run(['xcrun','clang++','-std=c++20','-Wall','-Wextra','-Werror',
                                    '-fsanitize=address,undefined','-I'+td,
                                    '-I'+str(ROOT/'components/dxmt/src/d3d11'),str(work/'fixture.cpp'),
                                    '-o',str(work/'fixture')],capture_output=True,text=True,timeout=60)
            self.assertEqual(built.returncode,0,built.stderr)
            for mode in ('normal','cpu-invalid','cpu-backward','gpu-backward','gpu-zero','error','cap','shared-cap','stale','stop-before','stop-during'):
                env = dict(os.environ,DXMT_CS2_TRACE_DIR='fixture-only',DXMT_CS2_TRACE_PROFILE='stall-waits',
                           DXMT_CS2_TRACE_DETAIL_LIMIT='3' if mode=='cap' else '6' if mode=='shared-cap' else '20')
                result = subprocess.run([str(work/'fixture'),mode],env=env,capture_output=True,text=True,timeout=15)
                self.assertEqual((result.returncode,result.stdout,result.stderr),(0,'',''),mode+result.stderr)
            for profile,limit in (('', '20'),('compile-waits','20'),('presentation-waits','20'),('stall-waits','0')):
                env = dict(os.environ,DXMT_CS2_TRACE_DIR='fixture-only',DXMT_CS2_TRACE_PROFILE=profile,DXMT_CS2_TRACE_DETAIL_LIMIT=limit)
                result = subprocess.run([str(work/'fixture'),'disabled'],env=env,capture_output=True,text=True,timeout=15)
                self.assertEqual((result.returncode,result.stdout,result.stderr),(0,'',''),profile+result.stderr)

if __name__=='__main__':
    unittest.main()
