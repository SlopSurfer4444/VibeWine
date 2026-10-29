/* SPDX-License-Identifier: MIT
 * Adapted from the owned 2026-10-28 cap_trial.cpp validation source. */
#ifdef NDEBUG
#error "bridge acceptance fixtures require assertions (-UNDEBUG)"
#endif
#include "airconv_worker_bridge.h"
#include <fstream>
#include <vector>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdint>
extern "C" uint64_t WMTWorkerTestSuccesses();
extern "C" int WMTWorkerTestPID();
int main(int argc,char** argv){
  assert(argc==2);std::ifstream file(argv[1],std::ios::binary);std::vector<char> bytes(std::istreambuf_iterator<char>(file),{});
  sm50_shader_t shader=nullptr;sm50_error_t error=nullptr;MTL_SHADER_REFLECTION reflection{};
  assert(!WMTWorkerInitialize(bytes.data(),bytes.size(),&shader,&reflection,&error));
  SM50_SHADER_COMMON_DATA common{};common.type=SM50_SHADER_COMMON;common.metal_version=SM50_SHADER_METAL_320;common.next=&common;
  std::vector<sm50_error_t> errors;
  for(unsigned i=0;i<1026;++i){sm50_bitcode_t bitcode=(void*)1;sm50_error_t e=nullptr;assert(WMTWorkerCompile(shader,reinterpret_cast<SM50_SHADER_COMPILATION_ARGUMENT_DATA*>(&common),"owned_shader",&bitcode,&e));assert(!bitcode&&e);errors.push_back(e);}
  char message[4096];WMTWorkerGetError(errors.back(),message,sizeof message);assert(strstr(message,"retained error cap"));
  assert(errors[1024]==errors[1025]);for(auto e:errors)WMTWorkerFreeError(e);
  sm50_bitcode_t bitcode=nullptr;assert(WMTWorkerCompile(shader,reinterpret_cast<SM50_SHADER_COMPILATION_ARGUMENT_DATA*>(&common),"owned_shader",&bitcode,&error));
  WMTWorkerGetError(error,message,sizeof message);assert(strstr(message,"cyclic recipe"));WMTWorkerFreeError(error);assert(WMTWorkerTestSuccesses()==0 && WMTWorkerTestPID()==-1);WMTWorkerDestroy(shader);WMTWorkerShutdown();puts("{\"retained_error_cap\":1024,\"shared_cap_error_freed_safely\":true,\"capacity_recovers\":true}");
}
