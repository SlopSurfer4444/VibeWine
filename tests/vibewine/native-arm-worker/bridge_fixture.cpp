/* SPDX-License-Identifier: MIT
 * Adapted from the owned 2026-10-28 bridge_trial.cpp validation source. */
#ifdef NDEBUG
#error "bridge acceptance fixtures require assertions (-UNDEBUG)"
#endif
#include "airconv_worker_bridge.h"
#include "airconv_worker_protocol.hpp"
#include <cerrno>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <thread>
#include <vector>
#include <cassert>
#include <unistd.h>
#include <signal.h>
extern "C" uint64_t WMTWorkerTestSuccesses();
extern "C" uint64_t WMTWorkerTestGeneration();
extern "C" int WMTWorkerTestPID();
using Bytes=std::vector<unsigned char>;
static Bytes read(const char* path){std::ifstream f(path,std::ios::binary);return Bytes(std::istreambuf_iterator<char>(f),{});}
static Bytes compile(sm50_shader_t shader,SM50_SHADER_COMPILATION_ARGUMENT_DATA* args,const char* name,bool remote,double& ms) {
  sm50_bitcode_t bitcode=nullptr;sm50_error_t error=nullptr;auto start=std::chrono::steady_clock::now();
  int status=remote?WMTWorkerCompile(shader,args,name,&bitcode,&error):SM50Compile(shader,args,name,&bitcode,&error);
  ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  if(status){char b[4096]{};if(error){if(remote){WMTWorkerGetError(error,b,sizeof b);WMTWorkerFreeError(error);}else{SM50GetErrorMessage(error,b,sizeof b);SM50FreeError(error);}}fprintf(stderr,"compile failure: %s\n",b);return {};}
  SM50_COMPILED_BITCODE out{};if(remote)WMTWorkerGetBitcode(bitcode,&out);else SM50GetCompiledBitcode(bitcode,&out);
  auto* p=static_cast<unsigned char*>(out.Data);Bytes b(p,p+out.Size);if(remote)WMTWorkerDestroyBitcode(bitcode);else SM50DestroyBitcode(bitcode);return b;
}
int main(int argc,char** argv) {
  setbuf(stdout,nullptr);
  if(argc==2 && !strcmp(argv[1],"--build-info")) {
#if defined(__x86_64__)
    const char* arch="x86_64";
#else
    const char* arch="other";
#endif
    printf("{\"build_id\":\"%s\",\"architecture\":\"%s\",\"protocol\":%u}\n",DXMT_WORKER_BUILD_ID,arch,dxmt::worker::Protocol);return 0;
  }
  assert(argc>=3 && argc<=4);const char* mode=argv[1];
  assert(!strcmp(mode,"parity")||!strcmp(mode,"rotate")||!strcmp(mode,"concurrent")||!strcmp(mode,"invalid")||!strcmp(mode,"cycle")||!strcmp(mode,"error")||!strcmp(mode,"cancel"));
  auto dxbc=read(argv[2]);assert(dxmt::worker::ordinaryDXBC(dxbc));
  uint32_t stage=UINT32_MAX;
  for(unsigned i=0;i<dxmt::worker::get32(dxbc.data()+28);++i){
    auto* chunk=dxbc.data()+dxmt::worker::get32(dxbc.data()+32+4*i);
    if(!memcmp(chunk,"SHDR",4)||!memcmp(chunk,"SHEX",4))stage=dxmt::worker::get32(chunk+8)>>16;
  }
  sm50_shader_t shader=nullptr;sm50_error_t error=nullptr;MTL_SHADER_REFLECTION refl{};
  assert(!WMTWorkerInitialize(dxbc.data(),dxbc.size(),&shader,&refl,&error));
  SM50_SHADER_COMMON_DATA common{};common.type=SM50_SHADER_COMMON;common.metal_version=SM50_SHADER_METAL_320;
  SM50_SHADER_PSO_PIXEL_SHADER_DATA pixel{};pixel.type=SM50_SHADER_PSO_PIXEL_SHADER;pixel.next=&common;pixel.sample_mask=0xffffffff;
  SM50_SHADER_GS_PASS_THROUGH_DATA pass{};pass.type=SM50_SHADER_GS_PASS_THROUGH;pass.next=&common;pass.DataEncoded=0xffffffff;pass.RasterizationDisabled=true;
  auto* args=reinterpret_cast<SM50_SHADER_COMPILATION_ARGUMENT_DATA*>(stage==0?static_cast<void*>(&pixel):(stage==1?static_cast<void*>(&pass):static_cast<void*>(&common)));
  SM50_SHADER_COMMON_DATA duplicate=common;
  if(!strcmp(mode,"invalid"))common.next=&duplicate;
  if(!strcmp(mode,"cycle"))common.next=&common;
  double direct=0,remote=0;
  if(!strcmp(mode,"invalid")||!strcmp(mode,"cycle")){auto output=compile(shader,args,"owned_shader",true,remote);assert(output.empty());assert(WMTWorkerTestSuccesses()==0);printf("{\"mode\":\"recipe_rejected\",\"ms\":%.4f}\n",remote);assert(WMTWorkerTestPID()==-1);WMTWorkerShutdown();WMTWorkerDestroy(shader);return 0;}
  auto baseline=compile(shader,args,"owned_shader",false,direct);assert(!baseline.empty());
  if(!strcmp(mode,"error")) {
    auto output=compile(shader,args,"owned_shader",true,remote);assert(output.empty());assert(WMTWorkerTestPID()==-1);assert(WMTWorkerTestSuccesses()==0);
    printf("{\"mode\":\"error\",\"remote_successes\":%llu,\"ms\":%.4f}\n",WMTWorkerTestSuccesses(),remote);
  } else if(!strcmp(mode,"cancel")) {
    std::atomic<bool> done{false};std::thread t([&]{double n;auto output=compile(shader,args,"owned_shader",true,n);assert(output.empty());done=true;});
    const char* ready=getenv("DXMT_WORKER_CANCEL_READY");assert(ready);auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!std::ifstream(ready).good() && std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(std::ifstream(ready).good());WMTWorkerShutdown();t.join();assert(done);assert(WMTWorkerTestPID()==-1);assert(WMTWorkerTestSuccesses()==0);
    double n;assert(compile(shader,args,"owned_shader",true,n).empty());puts("{\"mode\":\"cancel\",\"joined\":true,\"shutdown_closed\":true}");
  } else {
    unsigned loops=!strcmp(mode,"rotate")?258:12;
    for(unsigned n=0;n<loops;++n) {
      common.flags=SM50_SHADER_FLAG(n%4);auto expected=compile(shader,args,"owned_shader",false,direct);assert(!expected.empty());
      auto actual=compile(shader,args,"owned_shader",true,remote);assert(actual==expected);
      if(n==0 && argc>3){std::ofstream a(std::string(argv[3])+"-local.metallib",std::ios::binary);a.write(reinterpret_cast<const char*>(expected.data()),expected.size());std::ofstream b(std::string(argv[3])+"-remote.metallib",std::ios::binary);b.write(reinterpret_cast<const char*>(actual.data()),actual.size());}
      printf("{\"case\":%u,\"direct_ms\":%.4f,\"remote_ms\":%.4f,\"bytes\":%zu,\"successes\":%llu,\"generation\":%llu}\n",n,direct,remote,actual.size(),WMTWorkerTestSuccesses(),WMTWorkerTestGeneration());
    }
    if(!strcmp(mode,"concurrent")) {
      std::atomic<unsigned> passed{0},ready{0};std::atomic<bool> go{false};std::vector<std::thread> threads;
      for(unsigned n=0;n<8;++n)threads.emplace_back([&]{double a,b;auto expected=compile(shader,args,"owned_shader",false,a);++ready;while(!go.load())std::this_thread::yield();auto actual=compile(shader,args,"owned_shader",true,b);assert(actual==expected);++passed;});
      while(ready.load()!=8)std::this_thread::yield();go=true;
      for(auto& t:threads)t.join();assert(passed==8);assert(WMTWorkerTestSuccesses()<loops+8);printf("{\"concurrent_passed\":%u,\"successes\":%llu}\n",passed.load(),WMTWorkerTestSuccesses());
    }
    if(getenv("EXPECT_REMOTE"))assert(WMTWorkerTestSuccesses()>=loops);
    if(!strcmp(mode,"rotate"))assert(WMTWorkerTestGeneration()==2 && WMTWorkerTestSuccesses()==loops);
    if(getenv("EXPECT_LOCAL"))assert(WMTWorkerTestSuccesses()==0);
    int pid=WMTWorkerTestPID();WMTWorkerShutdown();assert(WMTWorkerTestPID()==-1);if(pid>0){assert(kill(pid,0)<0 && errno==ESRCH);}
  }
  WMTWorkerDestroy(shader);return 0;
}
