// Independent test of exact generated attribution helpers; no WSI/runtime calls.
#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
namespace dxmt::cs2trace {
inline std::atomic<uint32_t> capture=0;
inline std::atomic<uint64_t> clock_calls=0;
struct Recorded {uint32_t generation,type,flags; uint64_t object,detail0,detail1;};
inline std::mutex events_mutex;
inline std::vector<Recorded> events;
inline uint32_t generation(){return capture.load();}
inline uint64_t now(){return ++clock_calls;}
inline void record(uint32_t gen,uint32_t type,uint64_t,uint64_t,uint64_t object=0,uint32_t flags=0,uint64_t detail0=0,uint64_t detail1=0){
 if(!gen || capture.load()!=gen)return;
 std::lock_guard lock(events_mutex);events.push_back({gen,type,flags,object,detail0,detail1});
}
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

} // namespace dxmt::cs2trace
using namespace dxmt::cs2trace;
int main(){
 constexpr uint64_t A=11,B=22; int checks=0;
 auto reset=[](uint32_t gen){assert(!reason_state.active);capture=gen;events.clear();clock_calls=0;};
 reset(7);{ReasonScope scope(CommitReason::StagingMap,A);commitReason(7,A,3,4);assert(events.size()==1&&events[0].flags==2&&events[0].generation==7&&events[0].object==A&&events[0].detail0==3);}assert(!reason_state.active);++checks;
 reset(7);{ReasonScope scope(CommitReason::StagingMap,A);capture=8;assert(operationGeneration(A)==7);{ReasonScope fallback(CommitReason::ExplicitFlush,A,true);assert(reason_state.generation==7&&reason_state.reason==CommitReason::StagingMap);commitReason(8,A,3,4);}assert(events.empty()&&clock_calls==0);}++checks;
 reset(0);{ReasonScope scope(CommitReason::GPUIdle,A);assert(reason_state.active&&reason_state.generation==0);capture=8;{ReasonScope fallback(CommitReason::ExplicitFlush,A,true);assert(operationGeneration(A)==0);commitReason(8,A,3,4);}assert(events.empty()&&clock_calls==0);}++checks;
 reset(7);{ReasonScope outer(CommitReason::StagingMap,A);capture=8;{ReasonScope specific(CommitReason::FenceSignal,A);assert(reason_state.generation==7&&reason_state.reason==CommitReason::FenceSignal);}assert(reason_state.reason==CommitReason::StagingMap&&reason_state.generation==7);}++checks;
 reset(0);{ReasonScope outer(CommitReason::StagingMap,A);capture=8;{ReasonScope specific(CommitReason::FenceSignal,A);assert(operationGeneration(A)==0);}assert(operationGeneration(A)==0);}++checks;
 reset(7);{ReasonScope outer(CommitReason::GPUIdle,A);capture=8;{ReasonScope other(CommitReason::FenceSignal,B);assert(operationGeneration(B)==8);commitReason(8,B,4,5);}assert(operationGeneration(A)==7&&reason_state.reason==CommitReason::GPUIdle);assert(events.size()==1&&events[0].object==B&&events[0].flags==6);}++checks;
 reset(7);{ReasonScope scope(CommitReason::GPUIdle,A);{SyncScope wait{.gen=operationGeneration(A),.type=32,.queue=A};capture=8;}assert(events.empty());}++checks;
 reset(0);{ReasonScope scope(CommitReason::GPUIdle,A);capture=8;{SyncScope wait{.gen=operationGeneration(A),.type=32,.queue=A};}assert(events.empty()&&clock_calls==0);}++checks;
 reset(8);{ReasonScope scope(CommitReason::GetDataNotReady,A);{SyncScope flush{.gen=operationGeneration(A),.type=31,.queue=A};}assert(events.size()==1&&events[0].type==31&&events[0].generation==8);}++checks;
 reset(8);commitReason(8,A,2,3);assert(events.size()==1&&events[0].flags==0);++checks;
 reset(7);{ReasonScope scope(CommitReason::Present,A);capture=8;std::thread other([&]{assert(!reason_state.active);ReasonScope own(CommitReason::ExplicitFlush,A);commitReason(8,A,5,6);});other.join();assert(reason_state.generation==7&&reason_state.reason==CommitReason::Present);assert(events.size()==1&&events[0].generation==8&&events[0].flags==4);}++checks;
 reset(8);try{ReasonScope scope(CommitReason::FenceWait,A);throw std::runtime_error("unwind");}catch(const std::runtime_error&){}assert(!reason_state.active);++checks;
 std::cout<<checks<<" exact attribution helper checks passed\n";
}
