// Exercise exact production headers with only Windows mapping/environment mocked.
void fixture_barrier(int);
#define DXMT_TRACE_WRITER_BARRIER(point) fixture_barrier(point)
#include "cs2_compile_trace.hpp"
#include <cassert>
#include <cstring>
#include <stdexcept>
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
static Event *events(){return reinterpret_cast<Event*>(reinterpret_cast<char*>(buffer())+4096);}
static void arm(uint32_t gen){auto h=buffer();assert(h && !controlWriters(h->control));h->control=controlWord(gen,CaptureState::Open);}
int main(int argc,char **argv) {
 assert(argc==2);auto mode=argv[1];
 if(!std::strcmp(mode,"no-env")) {
  assert(!buffer() && !generation());
  HelperCompileScope span(HelperRoute::TileNativePSO,9);assert(!span.token.gen && !span.start);return 0;
 }
 auto h=buffer();assert(h);arm(10);generation();
 if(!std::strcmp(mode,"old")) {
  auto before=h->next;HelperCompileScope span(HelperRoute::TileNativePSO,9);
  assert(!span.token.gen && !span.start && !span.token.serial);
  assert(helperCompile(HelperRoute::TileNativePSO,9,[]{return 4;},[](auto v){return v==4;})==4);
  assert(h->next==before);return 0;
 }
 assert(profile()==Profile::CombinedBounded && events()[0].type==95 && events()[0].flags==5);
 if(!std::strcmp(mode,"disabled")) {
  assert(!detailLimit());auto before=h->next;
  for(unsigned type=80;type<=120;type++)if(combinedDetailType(type))record(10,type,1,100000001,77);
  {HelperCompileScope span(HelperRoute::TileNativePSO,9);assert(!span.token.gen && !span.start);}
  unsigned calls=0;assert(helperCompile(HelperRoute::TileNativePSO,9,[&]{++calls;return 4;},[](auto v){return v==4;})==4 && calls==1);
  assert(h->next==before);{PresentationScope span(96,9,8,7);}assert(h->next==before+1);return 0;
 }
 if(!std::strcmp(mode,"cap")) {
  assert(detailLimit()==3);auto before=h->next;
  record(10,80,1,1,5);record(10,104,1,2,5);{HelperCompileScope span(HelperRoute::TileNativePSO,9);span.result(true);}
  {HelperCompileScope span(HelperRoute::TileNativePSO,9);span.result(false);}
  record(10,111,1,1+StallWaitThresholdNs,5);
  assert(h->next==before+4 && events()[before+2].type==120 && events()[before+3].type==89 && events()[before+3].flags==1);
  assert(events()[before+3].padding[0]==3);return 0;
 }
 assert(!std::strcmp(mode,"enabled"));
 // All existing streams remain available, with a finite shared detail budget.
 const uint32_t ordinary[]={1,2,3,4,5,13,17,19,20,21,22,23,24,25,33,40,61,70,71,96,97,98,99,100,101,102,103};
 for(auto t:ordinary){auto before=h->next;record(10,t,1,2,91,4,8,7);assert(h->next==before+1);}
 for(unsigned type=108;type<=119;type++) {
  auto before=h->next;record(10,type,2,1,91);record(10,type,1,StallWaitThresholdNs,91);assert(h->next==before);
  record(10,type,1,1+StallWaitThresholdNs,91,4,8,7);assert(h->next==before+1);
 }
 auto before=h->next;record(10,120,2,1,91);assert(h->next==before);
 {PresentationScope p(98,91,8,7);assert(p.gen==10);}
 {StallScope p(108,91,8,7);assert(p.gen==10);p.start=1;}
 const auto owner=newSerial();before=h->next;
 assert(helperCompile(HelperRoute::TessellationNativePSO,owner,[]{return 4;},[](auto v){return v==4;})==4);
 assert(helperCompile(HelperRoute::TileNativePSO,owner,[]{return 0;},[](auto v){return v!=0;})==0);
 try {HelperCompileScope span(HelperRoute::ClearUAVRequest,owner);throw std::runtime_error("fixture");}catch(const std::runtime_error&){}
 for(unsigned i=0;i<3;i++) {
  auto e=events()[before+i];assert(e.type==120 && e.flags==i+1 && e.object && e.padding[1]==owner && e.end>=e.start);
  if(i)assert(e.object!=events()[before+i-1].object);
 }
 assert(events()[before].padding[0]==2 && events()[before+1].padding[0]==4 && events()[before+2].padding[0]==3);
 // Stopped operations and zero-generation asynchronous scopes never rearm.
 before=h->next;{HelperCompileScope span(HelperRoute::TileNativePSO,owner);h->control=controlWord(10,CaptureState::Closed);}assert(h->next==before);
 {HelperCompileScope span(HelperRoute::TileNativePSO,owner);assert(!span.token.gen && !span.start);arm(9);}assert(generation()==9);
 {PresentationScope span(98,owner,8,7,0,0);assert(!span.gen);}
 before=h->next;{HelperCompileScope span(HelperRoute::TileNativePSO,owner);arm(8);}assert(generation()==8);
 assert(h->next==before+2 && events()[before].type==95 && events()[before+1].type==89 && events()[before+1].flags==2 && events()[before+1].padding[0]==9);
 // Concurrent writers publish one metadata marker, no mixed generations.
 arm(7);before=h->next;std::vector<std::thread> threads;
 for(unsigned n=0;n<8;n++)threads.emplace_back([=]{for(unsigned i=0;i<10;i++){HelperCompileScope s(HelperRoute::RTVNativePSO,owner);s.result(true);}});
 for(auto &t:threads)t.join();assert(h->next==before+81);unsigned markers=0;
 for(auto i=before;i<h->next;i++){auto e=events()[i];assert(e.reserved==7 && e.committed==i+1);markers+=e.type==95;}
 assert(markers==1 && !h->dropped && !controlWriters(h->control));
 // STOP during metadata drains admitted publication and refuses subsequent work.
 arm(6);before=h->next;close_at_publication=true;record(6,99,1,1,owner,0,8,7);
 assert(h->next==before+1 && events()[before].type==95 && h->boundaryRejected==1 && controlState(h->control)==CaptureState::Closed);
 // Honest fixed lifetime cap and loss accounting.
 arm(5);generation();before=h->next;h->capacity=before+2;
 for(unsigned i=0;i<3;i++)record(5,99,1,1,owner,0,i+1,7);
 assert(h->next==before+3 && h->dropped==1 && !controlWriters(h->control));
}
