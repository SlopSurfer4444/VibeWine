#include "cs2_trace.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace dxmt::cs2trace;
unsigned native_calls=0;
int native_operation(){native_calls++;return 17;}
static Event *events(){return reinterpret_cast<Event*>(reinterpret_cast<char*>(buffer())+4096);}
static void arm(uint32_t gen){auto h=buffer();assert(h && !controlWriters(h->control));h->control=controlWord(gen,CaptureState::Open);}
static unsigned metadata(uint32_t gen){unsigned n=0;for(uint64_t i=0;i<buffer()->next;i++)n+=events()[i].type==95 && events()[i].reserved==gen;return n;}
static unsigned count(uint32_t type,uint64_t from){unsigned n=0;for(uint64_t i=from;i<buffer()->next;i++)n+=events()[i].type==type;return n;}
// New booleans must occupy old padding, not increase these hot stack objects.
struct OldApiLayout {uint32_t gen,type;uint64_t object,detail0,detail1;uint32_t result;uint64_t start;};
struct OldScopeLayout {uint32_t gen,type;uint64_t object;uint32_t flags;uint64_t start;};
struct OldSyncLayout {uint32_t gen,type;uint64_t object;uint32_t flags;uint64_t queue,frame,start;};
static_assert(sizeof(ApiScope)==sizeof(OldApiLayout));
static_assert(sizeof(Scope)==sizeof(OldScopeLayout));
static_assert(sizeof(SyncScope)==sizeof(OldSyncLayout));
static_assert(offsetof(ApiScope,start)==offsetof(OldApiLayout,start));
static_assert(offsetof(Scope,start)==offsetof(OldScopeLayout,start));
static_assert(offsetof(SyncScope,queue)==offsetof(OldSyncLayout,queue));
static_assert(offsetof(SyncScope,start)==offsetof(OldSyncLayout,start));

static bool api42_allowed(){return profile()==Profile::Full;}
static bool scope6_allowed(){return profile()==Profile::Full || profile()==Profile::PresentationWaits;}
static bool sync30_allowed(){return profile()==Profile::Full;}
static bool scope3_allowed(){return profile()==Profile::Full || profile()==Profile::CompileWaits;}
static void three_native_operations(){
  {ApiScope scope(42,100,1,2);assert(scope.invoke(native_operation)==17);assert(scope.result==17);}
  {Scope scope{.type=6,.object=200};native_operation();}
  {SyncScope scope{.type=30,.object=300,.queue=50,.frame=1};native_operation();}
}
static void unarmed(bool disabled){
  auto clocks=fixture_clock_calls.load();auto calls=native_calls;
  three_native_operations();assert(native_calls==calls+3 && fixture_clock_calls==clocks);
  if(disabled)assert(!buffer() && !fixture_create_calls);else assert(buffer()->next==0);
}
static void exercise(){
  arm(7);auto from=buffer()->next;auto clocks=fixture_clock_calls.load();auto calls=native_calls;
  three_native_operations();
  const unsigned expected=2*(api42_allowed()+scope6_allowed()+sync30_allowed());
  const unsigned marker=profile()==Profile::Full?0:1;
  assert(fixture_clock_calls==clocks+expected+marker && native_calls==calls+3);
  assert(metadata(7)==marker && buffer()->next==from+expected/2+marker);
  for(uint64_t i=from;i<buffer()->next;i++){
    const auto &e=events()[i];assert(e.reserved==7 && e.committed==i+1 && e.thread==456);
    if(e.type==95)continue;
    assert(e.end-e.start==100000);
    if(e.type==42)assert(e.object==100 && e.flags==17 && e.padding[0]==1 && e.padding[1]==2);
    else if(e.type==6)assert(e.object==200 && !e.flags && !e.padding[0] && !e.padding[1]);
    else{assert(e.type==30 && e.object==300 && !e.flags && e.padding[0]==50 && e.padding[1]==1);}
  }
  // This is measured call avoidance, not a wall-time/FPS estimate.
  from=buffer()->next;clocks=fixture_clock_calls.load();calls=native_calls;
  for(unsigned i=0;i<1000;i++)three_native_operations();
  assert(native_calls==calls+3000);
  assert(fixture_clock_calls==clocks+1000*expected);
  assert(buffer()->next==from+1000*expected/2 && metadata(7)==marker);
  assert(count(42,from)==1000*unsigned(api42_allowed()));
  assert(count(6,from)==1000*unsigned(scope6_allowed()));
  assert(count(30,from)==1000*unsigned(sync30_allowed()));
  // Allowed compiler scopes retain old 100us censoring and result payloads.
  from=buffer()->next;clocks=fixture_clock_calls.load();calls=native_calls;
  {Scope s{.type=3,.object=77,.flags=4};native_operation();}
  {Scope s{.type=5,.object=78};native_operation();}
  {Scope s{.type=17};native_operation();}
  {Scope s{.type=19};native_operation();}
  assert(native_calls==calls+4 && fixture_clock_calls==clocks+8*unsigned(scope3_allowed()));
  for(auto type:{3u,5u,17u,19u})assert(count(type,from)==unsigned(scope3_allowed()));
  from=buffer()->next;clocks=fixture_clock_calls.load();fixture_clock_step=1;
  {Scope s{.type=3,.object=77};native_operation();}
  assert(buffer()->next==from && fixture_clock_calls==clocks+2*unsigned(scope3_allowed()));fixture_clock_step=100000;
  // Explicit zero generations are never upgraded to a currently active arm.
  from=buffer()->next;clocks=fixture_clock_calls.load();
  {Scope s{.gen=0,.type=3};native_operation();}{SyncScope s{.gen=0,.type=30};native_operation();}
  assert(buffer()->next==from && fixture_clock_calls==clocks);
  // Preserve existing fail-closed behavior for explicit old tokens, including
  // their allowed in-flight timing overhead; this patch adds no token upgrade.
  from=buffer()->next;clocks=fixture_clock_calls.load();auto rejected=buffer()->boundaryRejected;
  {Scope s{.gen=6,.type=3};native_operation();}{SyncScope s{.gen=6,.type=30};native_operation();}
  assert(buffer()->next==from && buffer()->boundaryRejected==rejected);
  assert(fixture_clock_calls==clocks+2*(scope3_allowed()+sync30_allowed()));
}
static void boundaries(){
  // Allowed scopes keep their original same-generation STOP rejection receipt.
  auto from=buffer()->next;auto rejected=buffer()->boundaryRejected;auto calls=native_calls;
  {ApiScope api(40,101);Scope scope{.type=3};SyncScope sync{.type=30};
    assert(api.invoke(native_operation)==17);native_operation();native_operation();
    buffer()->control=controlWord(7,CaptureState::Closed);
  }
  const unsigned api40=scope6_allowed(),scopes=api40+scope3_allowed()+sync30_allowed();
  assert(native_calls==calls+3 && buffer()->next==from && buffer()->boundaryRejected==rejected+scopes);
  // Rearm while older scopes live: their events cannot become generation 9.
  arm(8);generation();from=buffer()->next;rejected=buffer()->boundaryRejected;
  {ApiScope api(40,101);Scope scope{.type=3};SyncScope sync{.type=30};
    assert(api.invoke(native_operation)==17);native_operation();native_operation();arm(9);
  }
  assert(buffer()->next==from && buffer()->boundaryRejected==rejected);
  generation();assert(metadata(9)==unsigned(profile()!=Profile::Full));
  // An unarmed scope stays unarmed even when an arm occurs before destruction.
  buffer()->control=controlWord(9,CaptureState::Closed);from=buffer()->next;auto clocks=fixture_clock_calls.load();
  {ApiScope api(40,101);Scope scope{.type=3};SyncScope sync{.type=30};
    assert(api.invoke(native_operation)==17);native_operation();native_operation();arm(10);
  }
  assert(buffer()->next==from && fixture_clock_calls==clocks);
  generation();assert(metadata(10)==unsigned(profile()!=Profile::Full));
}
int main(int argc,char **argv){
  assert(argc==2);const bool disabled=std::strcmp(argv[1],"disabled")==0;
  unarmed(disabled);if(disabled){std::cout<<"PASS disabled: zero trace clocks/mapping, native operations retained\n";return 0;}
  const auto selected=profile();setenv("DXMT_CS2_TRACE_PROFILE",selected==Profile::Full?"compile-waits":"",1);assert(profile()==selected);
  exercise();boundaries();
  std::cout<<"PASS actual producer scopes: precise clock/native counts, metadata/profile, censoring, zero/old tokens, stop/rearm\n";
}
