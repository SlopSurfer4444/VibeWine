// Exact production producer header; only Windows mapping/environment are mocked.
void fixture_barrier(int);
#define DXMT_TRACE_WRITER_BARRIER(point) fixture_barrier(point)
#include "cs2_trace.hpp"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
using namespace dxmt::cs2trace;
static bool close_at_publication=false;
void fixture_barrier(int point) {
 if(point==1 && close_at_publication) {
  close_at_publication=false;auto h=buffer();auto word=h->control;
  h->control=controlWord(controlGeneration(word),CaptureState::Closing,controlWriters(word));
 }
}
static Event *events() {return reinterpret_cast<Event*>(reinterpret_cast<char*>(buffer())+4096);}
static void arm(uint32_t gen) {
 auto h=buffer();assert(h && !controlWriters(h->control));
 h->control=controlWord(gen,CaptureState::Open);
}
static void check_marker(uint64_t index,uint32_t gen,Profile selected) {
 auto e=events()[index];assert(e.type==95 && e.reserved==gen && e.committed==index+1);
 assert(e.object==1 && e.flags==uint32_t(selected) && e.start==e.end);
 assert(e.padding[0]==detailLimit() && e.padding[1]==Capacity);
}
int main(int argc,char **argv) {
 assert(argc==2);auto h=buffer();assert(h);arm(10);
 if(!std::strcmp(argv[1],"stall-waits") || !std::strcmp(argv[1],"stall-waits-disabled")) {
  assert(profile()==Profile::StallWaits);assert(generation()==10);check_marker(0,10,Profile::StallWaits);
  if(!std::strcmp(argv[1],"stall-waits-disabled")) {
   assert(!detailLimit() && events()[0].padding[0]==0);
   for(uint32_t type=80;type<=94;type++){record(10,type,10,10);detailRecord(10,type,10,10,1);}
   assert(h->next==1);record(10,3,1,1+StallWaitThresholdNs);assert(h->next==2);return 0;
  }
  // Independent expected set, including newly instrumented finish-thread work.
  for(uint32_t type=0;type<120;type++) {
   const bool context=(type>=80 && type<=89) || type==93 || type==94;
   const bool duration=type==1 || type==3 || type==5 || (type>=6 && type<=16) || type==19 ||
     (type>=30 && type<=32) || (type>=40 && type<=49) || (type>=104 && type<=110);
   assert(profileAllows(type)==(context || duration));
   if(context || (type>=104 && type<=107))continue;
   auto before=h->next;record(10,type,100,100+StallWaitThresholdNs,123,0,8,9);
   assert(h->next==before+uint64_t(duration));
   if(duration){auto e=events()[before];assert(e.type==type && e.end-e.start==50000000 && e.object==123);}
  }
  auto before=h->next;
  // Filtering precedes reservation AND detailed quota; malformed subtraction,
  // including context, cannot evade the policy. Excluded long events also fail.
  for(unsigned i=0;i<100000;i++) {
   record(10,3,100,100+StallWaitThresholdNs-1);detailRecord(10,104,100,100+StallWaitThresholdNs-1,1);
   record(10,3,UINT64_MAX,0);detailRecord(10,104,UINT64_MAX,0,1);
   record(10,80,2,1);detailRecord(10,80,2,1,1);
   record(10,4,1,UINT64_MAX);detailRecord(10,17,1,UINT64_MAX,1);
  }
  assert(h->next==before && !h->dropped);
  record(0,3,1,UINT64_MAX);detailRecord(0,104,1,UINT64_MAX,1);assert(h->next==before);
  record(10,3,UINT64_MAX-StallWaitThresholdNs,UINT64_MAX);assert(h->next==before+1);
  before=h->next;
  // Exact threshold admitted via the actual detailed path after all filtered
  // attempts. The same shared cap covers slow compiler stages and context.
  record(10,104,100,100+StallWaitThresholdNs,1,1,0,9);
  record(10,80,10,10,500,2,100,1);detailRecord(10,81,10,10,500,2,100,0);
  detailRecord(10,84,10,10,500);record(10,84,10,10,500);
  assert(h->next==before+4 && events()[before].type==104 && events()[before+3].type==89 && events()[before+3].flags==1);
  auto token=beginDetail(100);assert(token.gen==10);
  // Explicitly unarmed finish work never upgrades itself after a later arm.
  h->control=controlWord(10,CaptureState::Closed);
  {StallScope unarmed(108,100,5,4);assert(!unarmed.gen && !unarmed.start);arm(9);}
  assert(generation()==9);before=h->next;
  detailInstant(token,84);assert(h->next==before+1 && events()[before].type==89 && events()[before].flags==2 && events()[before].padding[0]==10);
  // A scope born in generation9 cannot publish/relabel into generation8.
  before=h->next;{StallScope stale(108,100,5,4);assert(stale.gen==9);stale.start=1;arm(8);}
  assert(h->next==before);assert(generation()==8);before=h->next;
  for(unsigned i=0;i<3;i++)record(8,80,10,10,500,2,100,1);
  record(8,81,10,10,500);record(8,81,10,10,500);
  assert(h->next==before+4 && events()[before+3].type==89 && events()[before+3].flags==1);
  before=h->next;{StallScope slow(108,100,5,0);assert(slow.gen==8 && slow.start);slow.start=1;}
  assert(h->next==before+1 && events()[before].type==108 && events()[before].object==100 && events()[before].padding[0]==5 && events()[before].padding[1]==0);
  setenv("DXMT_CS2_TRACE_PROFILE","compile-waits",1);assert(profile()==Profile::StallWaits);
  arm(7);before=h->next;std::vector<std::thread> threads;
  for(unsigned t=0;t<8;t++)threads.emplace_back([]{for(unsigned i=0;i<100;i++){record(7,4,1,UINT64_MAX);record(7,3,1,1+StallWaitThresholdNs);}});
  for(auto& thread:threads)thread.join();assert(h->next==before+801);
  unsigned markers=0;for(auto i=before;i<h->next;i++){assert(events()[i].committed==i+1 && events()[i].reserved==7);markers+=events()[i].type==95;}
  assert(markers==1 && !controlWriters(h->control));
  arm(6);before=h->next;close_at_publication=true;record(6,108,1,1+StallWaitThresholdNs,100,0,5,4);
  assert(h->next==before+1);check_marker(before,6,Profile::StallWaits);
  assert(controlState(h->control)==CaptureState::Closed && !controlWriters(h->control) && h->boundaryRejected==1);
  arm(5);generation();before=h->next;h->capacity=before+2;
  for(unsigned i=0;i<3;i++)record(5,108,1,1+StallWaitThresholdNs,100,0,i+1,4);
  assert(h->next==before+3 && h->dropped==1);
 } else if(!std::strcmp(argv[1],"full")) {
  assert(profile()==Profile::Full);assert(generation()==10 && h->next==0);
  const uint32_t kinds[]={1,2,3,4,5,17,19,30,33,40,41,42,43,47,61,70,80,89,90,91,92,93,94,99,104,105,106,107};
  for(auto kind:kinds)record(10,kind,10,20,123,7,8,9);
  assert(h->next==sizeof(kinds)/sizeof(kinds[0]));
  for(unsigned i=0;i<h->next;i++)assert(events()[i].type==kinds[i] && events()[i].reserved==10);
  setenv("DXMT_CS2_TRACE_PROFILE","compile-waits",1);
  record(10,42,10,20);assert(events()[h->next-1].type==42 && profile()==Profile::Full);
 } else if(!std::strcmp(argv[1],"invalid")) {
  assert(profile()==Profile::Invalid);assert(generation()==10);check_marker(0,10,Profile::Invalid);
  for(uint32_t type=0;type<110;type++){record(10,type,10,20);detailRecord(10,type,10,20,1);}
  assert(h->next==1 && !h->dropped);
  setenv("DXMT_CS2_TRACE_PROFILE","compile-waits",1);
  record(10,3,10,20);assert(profile()==Profile::Invalid && h->next==1);
  arm(9);record(9,42,10,20);check_marker(1,9,Profile::Invalid);
  h->control=controlWord(9,CaptureState::Closed);record(9,3,10,20);assert(h->next==2);
 } else if(!std::strcmp(argv[1],"presentation-waits")) {
  assert(profile()==Profile::PresentationWaits);assert(generation()==10);check_marker(0,10,Profile::PresentationWaits);
  for(uint32_t type=0;type<120;type++) {
    bool accepted=type==1 || type==2 || (type>=6 && type<=16) || (type>=20 && type<=25) || type==33 || type==40 ||
      (type>=44 && type<=49) || type==61 || type==62 || (type>=70 && type<=78) || (type>=89 && type<=92) || (type>=96 && type<=103);
    assert(profileAllows(type)==accepted);auto before=h->next;
    record(10,type,10,20,123,7,8,9);assert(h->next==before+uint64_t(accepted));
    if(accepted){auto e=events()[before];assert(e.type==type && e.object==123 && e.flags==7 && e.padding[0]==8 && e.padding[1]==9);}
  }
  auto before=h->next;
  for(unsigned i=0;i<100000;i++){record(10,42,10,20);detailRecord(10,80,10,20,1);}
  assert(h->next==before && !h->dropped);
  {PresentationScope exact(96,100,9,7,3);}
  assert(h->next==before+1);auto e=events()[before];
  assert(e.type==96 && e.object==100 && e.padding[0]==9 && e.padding[1]==7 && e.flags==3 && e.end>=e.start);
  // Excluded compiler details did not spend the shared detailed budget.
  before=h->next;auto token=beginDetail(100);
  detailInterval(token,90,10,20,0,2,9);detailInstant(token,91,0,2,9);detailInstant(token,92,0,2,9);
  detailInstant(token,92,0,2,9);detailInstant(token,92,0,2,9);
  assert(h->next==before+4 && events()[before+3].type==89 && events()[before+3].flags==1);
  setenv("DXMT_CS2_TRACE_PROFILE","compile-waits",1);assert(profile()==Profile::PresentationWaits);
  arm(9);before=h->next;assert(generation()==9);check_marker(before,9,Profile::PresentationWaits);
  detailInstant(token,92,0,2,9);assert(events()[before+1].type==89 && events()[before+1].flags==2);
  before=h->next;{PresentationScope unarmed(98,100,5,4,0,0);}assert(h->next==before);
  {PresentationScope stale(98,100,5,4,0,10);}assert(h->next==before+1 && events()[before].type==89 && events()[before].flags==2 && events()[before].padding[0]==10);
  arm(8);before=h->next;std::vector<std::thread> threads;
  for(unsigned t=0;t<8;t++)threads.emplace_back([]{for(unsigned i=0;i<100;i++){record(8,42,10,20);record(8,99,10,10,100,0,i+1,i);}});
  for(auto& thread:threads)thread.join();assert(h->next==before+801);
  unsigned markers=0;for(auto i=before;i<h->next;i++){assert(events()[i].committed==i+1 && events()[i].reserved==8);markers+=events()[i].type==95;}
  assert(markers==1 && !controlWriters(h->control));
  arm(6);before=h->next;close_at_publication=true;record(6,99,10,10,100,0,5,4);
  assert(h->next==before+1);check_marker(before,6,Profile::PresentationWaits);
  assert(controlState(h->control)==CaptureState::Closed && !controlWriters(h->control) && h->boundaryRejected==1);
  arm(5);generation();before=h->next;h->capacity=before+2;
  record(5,98,10,20,100,0,5,4);record(5,98,20,30,100,0,6,4);record(5,98,30,40,100,0,7,4);
  assert(h->next==before+3 && h->dropped==1);
 } else {
  assert(!std::strcmp(argv[1],"compile-waits") && profile()==Profile::CompileWaits);
  // First attempt is excluded, but policy is discoverable in the capture.
  record(10,42,10,20);assert(h->next==1);check_marker(0,10,Profile::CompileWaits);
  const uint32_t keep[]={3,4,5,17,19,80,81,82,83,84,85,86,87,88,89,93,94,104,105,106,107};
  for(uint32_t type=0;type<110;type++) {
   bool accepted=false;for(auto k:keep)accepted|=type==k;
   assert(profileAllows(type)==accepted);
   auto before=h->next;record(10,type,10,20,123,7,8,9);
   assert(h->next==before+uint64_t(accepted));
   if(accepted){auto e=events()[before];assert(e.type==type && e.object==123 && e.flags==7 && e.padding[0]==8 && e.padding[1]==9);}
  }
  // Neither record() nor detailRecord() may let high-frequency API/finish
  // attempts consume capacity or the detailed task budget.
  auto before=h->next;
  for(unsigned i=0;i<100000;i++){record(10,42,10,20);detailRecord(10,90,10,20,1);}
  assert(h->next==before && !h->dropped);
  {ApiScope api(42,123);}{Present present(123);}{SyncScope scope{.type=30};}
  assert(h->next==before);
  {Scope wait{.type=3,.object=123};wait.start=1;}
  assert(h->next==before+1 && events()[before].type==3);before=h->next;
  auto token=beginDetail(500);assert(token.gen==10 && token.serial && token.owner==500);
  detailInstant(token,80,2,500,1);detailInstant(token,81,2,500,0);detailInstant(token,84,0,500,0);
  detailInstant(token,84);detailInstant(token,84);
  assert(h->next==before+4 && events()[before+3].type==89 && events()[before+3].flags==1);
  assert(events()[before+3].padding[0]==3);
  setenv("DXMT_CS2_TRACE_PROFILE","invalid",1);assert(profile()==Profile::CompileWaits);
  // Non-monotonic new generation, stale asynchronous work, no identity join.
  arm(9);assert(generation()==9);check_marker(before+4,9,Profile::CompileWaits);
  auto next=h->next;detailInstant(token,84);assert(h->next==next+1);
  assert(events()[next].type==89 && events()[next].flags==2 && events()[next].reserved==9 && events()[next].padding[0]==10);
  next=h->next;record(10,3,10,20);assert(h->next==next);
  // Concurrent first use: one marker, full publication, no admitted writer leak.
  arm(8);next=h->next;std::vector<std::thread> threads;
  for(unsigned t=0;t<8;t++)threads.emplace_back([]{for(unsigned i=0;i<100;i++){record(8,42,10,20);record(8,3,10,20);}});
  for(auto &thread:threads)thread.join();assert(h->next==next+801);
  unsigned markers=0;for(auto i=next;i<h->next;i++){auto e=events()[i];assert(e.committed==i+1 && e.reserved==8);markers+=e.type==95;assert(e.type==95 || e.type==3);}
  assert(markers==1 && controlWriters(h->control)==0);
  // The metadata producer participates in exactly the same close/quiescence
  // protocol. STOP at publication drains it and rejects the following scope.
  arm(6);next=h->next;close_at_publication=true;record(6,3,10,20);
  assert(h->next==next+1);check_marker(next,6,Profile::CompileWaits);
  assert(controlState(h->control)==CaptureState::Closed && !controlWriters(h->control) && h->boundaryRejected==1);
  // The lifetime buffer remains fixed, with honest overflow accounting.
  arm(5);generation();next=h->next;h->capacity=next+2;
  record(5,3,10,20);record(5,4,10,20);record(5,5,10,20);
  assert(h->next==next+3 && h->dropped==1 && !controlWriters(h->control));
 }
 if(profile()!=Profile::PresentationWaits){auto before=h->next;{PresentationScope exact(96,100,9,7,3);}assert(h->next==before);}
 if(profile()!=Profile::StallWaits){auto before=h->next;{StallScope idle(108,100,9,7);assert(!idle.gen && !idle.start);}assert(h->next==before);}
 assert(!controlWriters(h->control));
}
