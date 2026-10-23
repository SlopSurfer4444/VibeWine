"""Compile the actual producer header; OS file mapping is the only mocked seam."""
import pathlib,subprocess,tempfile,unittest

ROOT=pathlib.Path(__file__).resolve().parents[2]
WINDOWS=r'''
#pragma once
#include <cstdint>
using HANDLE=void*;
constexpr HANDLE INVALID_HANDLE_VALUE=nullptr;
constexpr int GENERIC_READ=1,GENERIC_WRITE=2,FILE_SHARE_READ=4,FILE_SHARE_WRITE=8,CREATE_NEW=1,FILE_ATTRIBUTE_NORMAL=1,PAGE_READWRITE=1,FILE_MAP_ALL_ACCESS=1;
inline uint32_t GetCurrentProcessId(){return 123;}
inline uint32_t GetCurrentThreadId(){return 456;}
inline HANDLE CreateFileA(const char*,int,int,void*,int,int,void*){return (void*)1;}
inline HANDLE CreateFileMappingA(HANDLE,void*,int,int,uint64_t,void*){return (void*)1;}
inline void CloseHandle(HANDLE){}
inline void* MapViewOfFile(HANDLE,int,int,int,int){alignas(64) static unsigned char bytes[4096+131072*64]{};return bytes;}
'''
ENV=r'''
#pragma once
#include <cstdlib>
#include <string>
namespace dxmt::env { inline std::string getEnvVar(const char* key){auto p=std::getenv(key);return p?p:"";} }
'''
FIXTURE=r'''
void fixture_barrier(int);
#define DXMT_TRACE_WRITER_BARRIER(point) fixture_barrier(point)
#include "cs2_trace.hpp"
#include <cassert>
#include <thread>
#include <vector>
using namespace dxmt::cs2trace;
bool close_at_publication=false;
void fixture_barrier(int point) {
 if(point==1 && close_at_publication) {
  close_at_publication=false;auto h=buffer();auto word=h->control;
  h->control=controlWord(controlGeneration(word),CaptureState::Closing,controlWriters(word));
 }
}
int main(int argc,char**) {
 auto h=buffer(); assert(h); h->control=controlWord(10,CaptureState::Open);
 if(argc>1){auto token=beginDetail(100);assert(!token.gen);detailInstant(token,80,2,100,1);assert(h->next==0);return 0;}
 auto token=beginDetail(100);assert(token.gen==10&&token.serial&&token.owner==100);
 auto e=(Event*)((char*)h+4096);
 detailInstant(token,80,2,100,1); detailInstant(token,81,2,100,0);detailInstant(token,84,0,100,0);
 detailInstant(token,84,0,100,0);detailInstant(token,84,0,100,0);
 assert(h->next==4&&e[0].type==80&&e[1].type==81&&e[2].type==84&&e[3].type==89&&e[3].flags==1&&e[3].padding[0]==3);
 assert(e[0].reserved==10&&e[0].object==token.serial&&e[0].padding[0]==100&&e[0].padding[1]==1&&e[0].start==e[0].end);
 assert(controlWriters(h->control)==0);
 // Actual controller permits arbitrary unused generations, including 10 -> 9.
 h->control=controlWord(9,CaptureState::Open);
 auto next=beginDetail(100);detailInstant(next,80,2,100,1);assert(h->next==5&&e[4].reserved==9&&e[4].type==80);
 // A stale asynchronous callback creates failure evidence in the active capture.
 detailInstant(token,84,0,100,0);assert(h->next==6&&e[5].type==89&&e[5].flags==2&&e[5].reserved==9&&e[5].padding[0]==10);
 h->control=controlWord(8,CaptureState::Open);auto concurrent=beginDetail(100);
 std::vector<std::thread> threads;
 for(int t=0;t<8;t++)threads.emplace_back([&]{for(int n=0;n<100;n++)detailInstant(concurrent,81,2,100,0);});
 for(auto& t:threads)t.join();assert(h->next==10);
 unsigned ordinary=0,failures=0;for(unsigned i=6;i<10;i++){assert(e[i].reserved==8&&e[i].committed==i+1);ordinary+=e[i].type==81;failures+=e[i].type==89;}
 assert(ordinary==3&&failures==1&&h->dropped==0&&controlWriters(h->control)==0);
 h->control=controlWord(8,CaptureState::Closed);detailInstant(concurrent,81,2,100,0);assert(h->next==10);
 // STOP after the single writer admission must still drain the failure marker.
 h->control=controlWord(6,CaptureState::Open);auto closing=beginDetail(100);
 for(int i=0;i<3;i++)detailInstant(closing,81,2,100,0);
 close_at_publication=true;detailInstant(closing,81,2,100,0);
 assert(h->next==14&&e[13].type==89&&e[13].flags==1&&e[13].committed==14);
 assert(controlState(h->control)==CaptureState::Closed&&controlWriters(h->control)==0&&h->boundaryRejected==0);
}
'''

class DetailProducerTests(unittest.TestCase):
    def test_actual_producer_cap_generation_disabled_and_concurrency(self):
        import os
        with tempfile.TemporaryDirectory(prefix='detail-producer-') as td:
            p=pathlib.Path(td)
            (p/'windows.h').write_text(WINDOWS);(p/'util_env.hpp').write_text(ENV);(p/'fixture.cpp').write_text(FIXTURE)
            result=subprocess.run(['xcrun','clang++','-std=c++20','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-pthread','-I'+td,'-I'+str(ROOT/'components/dxmt/src/d3d11'),str(p/'fixture.cpp'),'-o',str(p/'fixture')],capture_output=True,text=True,timeout=60)
            self.assertEqual(result.returncode,0,result.stderr)
            for limit,args in [('3',[]),('0',['disabled']),('32769',['disabled']),('1.5',['disabled']),('', ['disabled'])]:
                env=dict(os.environ,DXMT_CS2_TRACE_DIR='fixture-only',DXMT_CS2_TRACE_DETAIL_LIMIT=limit)
                r=subprocess.run([str(p/'fixture'),*args],env=env,capture_output=True,text=True,timeout=15)
                self.assertEqual((r.returncode,r.stdout,r.stderr),(0,'',''),r.stderr)

if __name__=='__main__':unittest.main()
