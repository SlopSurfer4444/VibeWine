#pragma once
#include "util_env.hpp"
#include "cs2_trace_protocol.hpp"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <string>
namespace dxmt::cs2trace {
inline uint64_t now() {return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
inline Header* buffer() {
 static Header *result=[]()->Header* {
  std::string path=env::getEnvVar("DXMT_CS2_TRACE_DIR"); if(path.empty())return nullptr;
  path+="\\events-"+std::to_string(GetCurrentProcessId())+".bin";
  HANDLE file=CreateFileA(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file==INVALID_HANDLE_VALUE)return nullptr;
  HANDLE mapping=CreateFileMappingA(file,nullptr,PAGE_READWRITE,0,4096+Capacity*sizeof(Event),nullptr);
  if(!mapping){CloseHandle(file);return nullptr;}
  auto ptr=(Header*)MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,0);
  CloseHandle(mapping);CloseHandle(file);if(!ptr)return nullptr;
  ptr->version=Version;ptr->capacity=Capacity;ptr->next=0;ptr->control=0;ptr->dropped=0;ptr->boundaryRejected=0;
  std::atomic_ref<uint64_t>(ptr->magic).store(Magic,std::memory_order_release);
  return ptr; // Process-lifetime mapping; no file writes in event hot paths.
 }();
 return result;
}
inline uint32_t generation() {auto h=buffer();if(!h)return 0;auto word=std::atomic_ref<uint64_t>(h->control).load(std::memory_order_relaxed);return controlState(word)==CaptureState::Open?controlGeneration(word):0;}
inline bool configured() {static bool value=!env::getEnvVar("DXMT_CS2_TRACE_DIR").empty();return value;}
inline void record(uint32_t gen,uint32_t type,uint64_t start,uint64_t end,uint64_t object=0,uint32_t flags=0,uint64_t detail0=0,uint64_t detail1=0) {
 if(!gen)return;
 writeEvent(buffer(),gen,type,start,end,object,flags,detail0,detail1,GetCurrentThreadId());
}
// Process-lifetime monotonic birth identities; assigned once per object.
inline uint64_t newSerial() {static std::atomic<uint64_t> next{1};return next.fetch_add(1,std::memory_order_relaxed);}
// Explicit opt-in, parsed once. Empty/invalid/out-of-range means off. The cap is
// per producer and generation across ALL detailed task/persistence/finish probes.
inline uint32_t detailLimit() {
 static const uint32_t value=[] {
  auto text=env::getEnvVar("DXMT_CS2_TRACE_DETAIL_LIMIT"); uint32_t n=0;
  if(text.empty())return 0u;
  for(char c:text) {if(c<'0'||c>'9'||n>DetailBudget::MaxRecords/10)return 0u;n=n*10+uint32_t(c-'0');}
  return n && n<=DetailBudget::MaxRecords ? n : 0u;
 }();return value;
}
struct DetailToken { uint32_t gen=0; uint64_t serial=0,owner=0; };
inline DetailToken beginDetail(uint64_t owner) {
 auto gen=detailLimit()?generation():0;
 return {gen,gen?newSerial():0,owner};
}
inline void detailRecord(uint32_t gen,uint32_t type,uint64_t start,uint64_t end,uint64_t object,uint32_t flags=0,uint64_t detail0=0,uint64_t detail1=0) {
 if(!gen || !detailLimit())return;
 static DetailBudget budget;
 auto active=generation();
 if(!active)return; // Ordinary capture-boundary incompleteness remains explicit.
 auto h=buffer();
 if(!h || !admit(h,active))return;
 struct Exit {Header *h;~Exit(){releaseWriter(h);}} exit{h};
 auto publish=[&](uint32_t event,uint64_t a,uint64_t b,uint64_t identity,uint32_t result,uint64_t p0,uint64_t p1) {
  writeAdmittedEvent(h,active,event,a,b,identity,result,p0,p1,GetCurrentThreadId());
 };
 // A stale asynchronous token must never become evidence in a later capture.
 // One budgeted marker fails that capture rather than silently joining it.
 auto admitted=budget.claim(active,detailLimit());
 if(admitted==DetailBudget::Result::exhausted_first) {
  auto t=now();publish(89,t,t,0,1,detailLimit(),0);return;
 }
 if(admitted!=DetailBudget::Result::accepted)return;
 if(active!=gen) {auto t=now();publish(89,t,t,0,2,gen,0);return;}
 publish(type,start,end,object,flags,detail0,detail1);
}
inline void detailInstant(DetailToken token,uint32_t type,uint32_t flags=0,uint64_t detail0=0,uint64_t detail1=0) {
 if(!token.gen)return;
 auto t=now();detailRecord(token.gen,type,t,t,token.serial,flags,detail0,detail1);
}
inline void detailInterval(DetailToken token,uint32_t type,uint64_t start,uint64_t end,uint32_t flags=0,uint64_t detail0=0,uint64_t detail1=0) {
 if(token.gen)detailRecord(token.gen,type,start,end,token.serial,flags,detail0,detail1);
}
struct ApiScope {
 uint32_t gen=generation(),type;uint64_t object,detail0=0,detail1=0;uint32_t result=0xffffffffu;uint64_t start=gen?now():0;
 ApiScope(uint32_t event,uint64_t identity,uint64_t arg0=0,uint64_t arg1=0):type(event),object(identity),detail0(arg0),detail1(arg1){}
 ApiScope(const ApiScope&)=delete;
 template<class F> auto invoke(F &&fn) {auto value=fn();result=uint32_t(value);return value;}
 ~ApiScope(){if(gen)record(gen,type,start,now(),object,result,detail0,detail1);}
};
// Diagnostic attribution only: queue scoped, generation scoped, thread local.
enum class CommitReason : uint32_t { Unknown=0, Present=1, StagingMap=2,
  GetDataNotReady=3, ExplicitFlush=4, GPUIdle=5, FenceSignal=6,
  FenceWait=7, PassPromotion=8 };
struct ReasonState { uint32_t generation=0; CommitReason reason=CommitReason::Unknown; uint64_t queue=0; bool active=false; };
inline thread_local ReasonState reason_state;
struct ReasonScope {
  ReasonState previous; bool changed=false;
  ReasonScope(CommitReason reason, uint64_t queue, bool fallback=false) {
    auto gen=generation();
    previous=reason_state;
    if (previous.active && previous.queue==queue) {
      gen=previous.generation;
      if (fallback) return;
    }
    reason_state={gen,reason,queue,true}; changed=true;
  }
  ~ReasonScope() { if(changed) reason_state=previous; }
  ReasonScope(const ReasonScope&)=delete;
  ReasonScope& operator=(const ReasonScope&)=delete;
};
inline void commitReason(uint32_t gen,uint64_t queue,uint64_t sequence,uint64_t frame) {
  if(!gen)return;
  if(reason_state.active && reason_state.queue==queue && reason_state.generation!=gen)return;
  auto reason=(reason_state.generation==gen && reason_state.queue==queue)?reason_state.reason:CommitReason::Unknown;
  auto t=now(); record(gen,33,t,t,queue,(uint32_t)reason,sequence,frame);
}
inline uint32_t operationGeneration(uint64_t queue) {
  return reason_state.active && reason_state.queue==queue ? reason_state.generation : generation();
}
// Uncensored diagnostic calls; no clock read or payload when unarmed.
struct SyncScope {
  uint32_t gen=generation(); uint32_t type; uint64_t object=0;
  uint32_t flags=0; uint64_t queue=0,frame=0; uint64_t start=gen?now():0;
  ~SyncScope() { if(gen) record(gen,type,start,now(),object,flags,queue,frame); }
};

struct Scope {
 uint32_t gen=generation();uint32_t type;uint64_t object=0;uint32_t flags=0;uint64_t start=gen?now():0;
 ~Scope(){if(gen){auto end=now();if(end-start>=100000)record(gen,type,start,end,object,flags);}}
};
struct Present {
 uint32_t gen=generation();uint64_t object;uint64_t start=gen?now():0;
 Present(uint64_t obj):object(obj){
  static thread_local uint64_t previous=0,previousObject=0;
  static thread_local uint32_t previousGeneration=0;
  if(gen && gen==previousGeneration && object==previousObject && previous)record(gen,1,previous,start,object);
  previous=start;previousObject=object;previousGeneration=gen;
 }
 ~Present(){if(gen)record(gen,2,start,now(),object);}
};
} // namespace dxmt::cs2trace
