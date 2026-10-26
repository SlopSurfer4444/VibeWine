#pragma once
#include "cs2_trace.hpp"
#include <limits>

namespace dxmt::cs2trace {
// GetThreadTimes reports kernel/user CPU in 100 ns units. This is a separate
// domain from steady wall time; no native/PE epoch subtraction is permitted.
struct ThreadCpuSample {
 uint64_t ticks100ns=0; bool valid=false;
};
inline ThreadCpuSample threadCpuSample() {
 FILETIME creation{},exit{},kernel{},user{};
 if(!GetThreadTimes(GetCurrentThread(),&creation,&exit,&kernel,&user))return {};
 auto ticks=[](const FILETIME &t) {return (uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
 auto k=ticks(kernel),u=ticks(user);
 if(u>std::numeric_limits<uint64_t>::max()-k)return {};
 return {k+u,true};
}
// Opt-in, completed slow waits only. No begin record and no asynchronous
// collector: a wait unfinished at capture stop remains unobserved.
struct StallCompletionWait {
 uint32_t gen=0;
 uint64_t queue,chunk,frame,final_target,initializer_target;
 uint64_t initializer_observed=0,ready_encode=0,ready_commit=0,coherent=0,latency=0;
 uint32_t ongoing=0;
 uint64_t start=0,end=0,cpu100ns=0; bool cpu_valid=false;
 ThreadCpuSample cpu_before;
 StallCompletionWait(uint32_t capturedGeneration,uint64_t queueSerial,uint64_t sequence,uint64_t queueFrame,
                     uint64_t finalFrameTarget,uint64_t initializerTarget)
   :queue(queueSerial),chunk(sequence),frame(queueFrame),final_target(finalFrameTarget),initializer_target(initializerTarget) {
  if(profile()==Profile::StallWaits && detailLimit() && capturedGeneration && generation()==capturedGeneration)
   gen=capturedGeneration;
 }
 bool active() const {return gen!=0;}
 void begin() {if(gen){cpu_before=threadCpuSample();start=now();}}
 bool finish() {
  if(!gen)return false;
  end=now();auto after=threadCpuSample();
  cpu_valid=cpu_before.valid && after.valid && after.ticks100ns>=cpu_before.ticks100ns;
  if(cpu_valid)cpu100ns=after.ticks100ns-cpu_before.ticks100ns;
  return end>=start && end-start>=StallWaitThresholdNs;
 }
 void publish(uint32_t before,uint32_t after,uint64_t gpuStart,uint64_t gpuEnd) {
  const bool gpuValid=after==4 && gpuStart && gpuEnd>=gpuStart;
  const uint32_t flags=before|(after<<3)|(uint32_t(cpu_valid)<<6)|(uint32_t(gpuValid)<<7);
  const DetailPayload rows[]={
   {111,flags,chunk,frame}, {112,0,final_target,initializer_target},
   {113,0,initializer_observed,cpu100ns}, {114,ongoing,ready_encode,ready_commit},
   {115,0,coherent,latency}, {116,0,gpuValid?gpuEnd-gpuStart:0,0}
  };
  detailRecords(gen,start,end,queue,rows,6);
 }
 StallCompletionWait(const StallCompletionWait&)=delete;
};
} // namespace dxmt::cs2trace
