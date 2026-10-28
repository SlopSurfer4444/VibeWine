#include "trace_core.hpp"
#include "probe_semantics.hpp"
#include <iostream>
#include <limits>
#include <cmath>
#include <iomanip>
#include <set>
#include <tuple>
#include <map>
#include <array>
#include <QuartzCore/CABase.h>
#include <mach/mach_time.h>
#include <uuid/uuid.h>
static std::string json(const std::string &s);

// One public native-clock observation. No producer mapping, process inspection,
// capture mutation, timer, output file, or background observer is created.
static void clockSample() {
  uuid_t identity;uuid_generate_random(identity);char id[37];uuid_unparse_lower(identity,id);
  mach_timebase_info_data_t timebase{};
  if(mach_timebase_info(&timebase)!=KERN_SUCCESS || !timebase.numer || !timebase.denom)
    trace::fail("native Mach timebase unavailable");
  const uint64_t resolution=(uint64_t(timebase.numer)+timebase.denom-1)/timebase.denom;
  if(resolution>1000000)trace::fail("native raw clock resolution exceeds 1 ms");
  const double lower=CACurrentMediaTime();
  const auto ticks=mach_absolute_time();
  const double upper=CACurrentMediaTime();
  if(!std::isfinite(lower)||!std::isfinite(upper)||lower<=0||upper<lower||upper>=1e9||upper-lower>.1||!ticks)
    trace::fail("native clock sample invalid or bracket exceeds 100 ms");
  const auto converted=(__uint128_t(ticks)*timebase.numer)/timebase.denom;
  if(converted>UINT64_MAX)trace::fail("native clock conversion overflow");
  const uint64_t stamp=uint64_t(converted);
  const auto width=uint64_t(std::ceil((upper-lower)*1e9));
  std::cout<<"{\"kind\":\"clock_sample\",\"schema\":1,\"clock_domain\":\"ca_host_seconds\",\"clock_api\":\"CACurrentMediaTime\",\"monotonic_clock_domain\":\"native_mach_absolute_ns\",\"monotonic_clock_api\":\"mach_absolute_time\",\"sample_id\":"<<json(id)<<",\"pid\":"<<getpid()<<",\"ca_lower_seconds\":"<<std::setprecision(17)<<lower<<",\"ca_upper_seconds\":"<<upper<<",\"native_monotonic_ns\":"<<stamp<<",\"native_monotonic_resolution_ns\":"<<resolution<<",\"mach_ticks\":"<<ticks<<",\"timebase_numer\":"<<timebase.numer<<",\"timebase_denom\":"<<timebase.denom<<",\"bracket_ns\":"<<width<<"}\n";
}

static std::string json(const std::string &s) {
  std::string out="\"";
  for(unsigned char c:s) {
    if(c=='"'||c=='\\'){out+='\\';out+=c;}
    else if(c<32){const char*h="0123456789abcdef";out+="\\u00";out+=h[c>>4];out+=h[c&15];}
    else out+=c;
  }
  return out+'"';
}
static uint32_t generation(const char *arg) {
  std::string s=arg;if(s.empty())trace::fail("nonzero uint32 generation required");uint64_t n=0;
  for(char c:s){if(c<'0'||c>'9')trace::fail("nonzero uint32 generation required");n=n*10+(c-'0');if(n>UINT32_MAX)trace::fail("generation overflow");}
  if(n==0)trace::fail("generation zero forbidden");return uint32_t(n);
}
static uint64_t recordBudget(const char *arg) {
  std::string s=arg;if(s.empty())trace::fail("unsigned decimal record budget required");
  uint64_t n=0;
  for(char c:s){
    if(c<'0'||c>'9')trace::fail("unsigned decimal record budget required");
    const auto digit=uint64_t(c-'0');
    if(n>(UINT64_MAX-digit)/10)trace::fail("record budget overflow");
    n=n*10+digit;
  }
  return n;
}
static const char *clockDomain(uint32_t type) {
  if(type==24)return "native_gpu_ns";
  if((type>=1 && type<=17)||type==19||(type>=20 && type<=23)||type==25||(type>=30 && type<=49)||type==61||type==62||(type>=70 && type<=78)||(type>=80 && type<=120))return "cpu_steady_ns";
  return "unknown";
}
static const char *profileName(uint32_t flags) {
  return flags==1?"compile-waits":flags==2?"invalid-fail-closed":flags==3?"presentation-waits":flags==4?"stall-waits":flags==5?"combined-bounded":"unknown";
}
static bool profileMarkerValid(const trace::Decoded &e) {
  return e.object==1 && ((e.flags!=3 && e.flags!=4 && e.flags!=5) || e.thread) && ((e.flags!=4 && e.flags!=5) || e.start) && (e.flags>=1 && e.flags<=5) && e.start==e.end &&
    e.padding0<=dxmt::cs2trace::DetailBudget::MaxRecords && e.padding1==trace::Capacity;
}
static bool presentationProbeValid(const trace::Decoded &e) {
 if(e.type<96 || e.type>99)return true;
 if(!e.object || !e.thread || e.end<e.start)return false;
 if(e.type==96)return e.flags && e.padding0<UINT64_MAX && e.padding0+1>e.flags && e.padding1==e.padding0+1-e.flags;
 if(e.type==97)return e.flags && e.padding0>e.flags && e.padding1==e.padding0-e.flags;
 if(e.type==98)return !e.flags && e.padding0 && e.padding1;
 return !e.flags && e.padding0 && e.start==e.end;
}
static bool compilerProbeValid(const trace::Decoded &e) {
 if(e.type<104 || e.type>107)return true;
 if(!e.object || !e.thread || !e.start || e.end<e.start)return false;
 if(e.type==104)return e.padding1 && (e.flags==1 || e.flags==3);
 return e.padding0 && e.padding1<=1 && (e.type!=105 || !e.padding1) && e.flags>=1 && e.flags<=3;
}
static bool helperProbeValid(const trace::Decoded &e) {
 if(e.type!=120)return true;
 return e.object && e.thread && e.start && e.end>=e.start && e.flags>=1 && e.flags<=3 && e.padding0>=1 && e.padding0<=5;
}
static bool retirementProbeValid(const trace::Decoded &e) {
 if(!((e.type>=108 && e.type<=110) || (e.type>=117 && e.type<=119)))return true;
 return e.object && e.padding0 && e.thread && e.start && e.end>=e.start && !e.flags;
}

static bool completionCardsValid(const std::vector<trace::Decoded> &events,bool stallProfile) {
 using Group=std::array<const trace::Decoded*,6>;
 std::map<std::tuple<uint32_t,uint64_t,uint64_t,uint64_t>,Group> groups;
 bool valid=true;
 std::map<std::pair<uint64_t,uint64_t>,std::vector<const trace::Decoded*>> retirements;
 for(const auto &e:events)if((e.type>=108 && e.type<=110) || (e.type>=117 && e.type<=119))retirements[{e.object,e.padding0}].push_back(&e);
 for(const auto &e:events)if(e.type>=111 && e.type<=116) {
  if(!stallProfile || !e.object || !e.thread || !e.start || e.end<e.start || e.end-e.start<dxmt::cs2trace::StallWaitThresholdNs)valid=false;
  auto &slot=groups[{e.thread,e.object,e.start,e.end}][e.type-111];
  if(slot)valid=false;slot=&e;
 }
 std::set<std::pair<uint64_t,uint64_t>> identities;
 for(const auto &[key,g]:groups) {
  (void)key;
  if(std::any_of(g.begin(),g.end(),[](auto p){return !p;})){valid=false;continue;}
  const auto &a=*g[0],&b=*g[1],&c=*g[2],&d=*g[3],&e=*g[4],&f=*g[5];
  const auto before=a.flags&7u,after=(a.flags>>3)&7u;
  if(!a.padding0 || before>3 || after>5 || (a.flags&~255u) || b.flags || c.flags || e.flags || f.flags || d.flags>32)valid=false;
  if(b.padding0!=UINT64_MAX && (a.padding1==UINT64_MAX || b.padding0!=a.padding1+1))valid=false;
  if((!b.padding1 && c.padding0) || (!(a.flags&64u) && c.padding1) || f.padding1 ||
     ((a.flags&128u) && after!=4) || (!(a.flags&128u) && f.padding0))valid=false;
  if(d.padding0<=a.padding0 || d.padding1<=a.padding0 || e.padding0>=a.padding0)valid=false;
  for(const auto *phase:retirements[{a.object,a.padding0}])if(phase->thread!=a.thread || phase->padding1!=a.padding1 || phase->start<a.end)valid=false;
  if(!identities.emplace(a.object,a.padding0).second)valid=false;
 }
 return valid;
}
int main(int argc,char **argv) {
  try {
    if(argc==2 && std::string(argv[1])=="clock-sample") {clockSample();return 0;}
    if(argc==3 && std::string(argv[1])=="new-session") {
      std::cout<<"{\"session\":"<<json(trace::newSession(argv[2]))<<",\"runtime_started\":false}\n";return 0;
    }
    if(argc==3 && std::string(argv[1])=="inspect") {
      trace::Mapping m(argv[2],false);
      const auto reserved=m.next();
      std::cout<<"{\"identity\":"<<json(m.binding)<<",\"capture\":"<<m.capture()<<",\"next_reserved\":"<<reserved<<",\"dropped\":"<<m.dropped()<<",\"remaining_capacity\":"<<trace::Mapping::remainingAfter(reserved)<<",\"capacity\":"<<trace::Capacity<<",\"protocol_version\":2,\"live_pid_verified\":false}\n";return 0;
    }
    if((argc==7 && std::string(argv[1])=="arm") || (argc==5 && std::string(argv[1])=="stop")) {
      uint32_t gen=generation(argv[3]);trace::Mapping m(argv[2],true,argv[4]);
      bool quiescent=false;
      const bool arming=std::string(argv[1])=="arm";
      const auto planned=arming?recordBudget(argv[5]):0;
      const auto reserve=arming?recordBudget(argv[6]):0;
      uint64_t available=0;
      if(arming)available=m.arm(gen,planned,reserve);else quiescent=m.stop(gen);
      if(arming)std::cout<<"{\"planned_records\":"<<planned<<",\"tail_reserve\":"<<reserve<<",\"remaining_capacity_at_admission\":"<<available<<",\"budget_enforced_during_capture\":false,";
      else std::cout<<"{";
      std::cout<<"\"operation\":"<<json(argv[1])<<",\"identity\":"<<json(m.binding)<<",\"generation\":"<<gen<<",\"capture\":"<<m.capture()<<",\"producer_quiescence_verified\":"<<(quiescent?"true":"false")<<",\"structurally_complete\":"<<(quiescent?"true":"false")<<",\"admitted_writers\":"<<trace::controlWriters(m.control())<<"}\n";return (std::string(argv[1])=="stop" && !quiescent)?3:0;
    }
    if(argc==5 && std::string(argv[1])=="decode") {
      uint32_t gen=generation(argv[3]);trace::Mapping m(argv[2],false,argv[4]);auto s=trace::decode(m,gen);
      const trace::Decoded *profile=nullptr;unsigned profileMarkers=0;
      for(const auto &e:s.events)if(e.type==95){profile=&e;profileMarkers++;}
      bool profileValid=!profileMarkers || (profileMarkers==1 && profileMarkerValid(*profile) && (profile->flags==1 || profile->flags==3 || profile->flags==4 || profile->flags==5));
      if(profile && (profile->flags==1 || profile->flags==3 || profile->flags==4 || profile->flags==5))for(const auto &e:s.events)
        if(e.type!=95 && !dxmt::cs2trace::profileEventAllowed(dxmt::cs2trace::Profile(profile->flags),e.type,e.start,e.end))profileValid=false;
      if(profile && (profile->flags==4 || profile->flags==5)) {
        uint64_t context=0,capMarkers=0;
        for(const auto &e:s.events)if(profile->flags==5?dxmt::cs2trace::combinedDetailType(e.type):((e.type>=80 && e.type<=89) || e.type==93 || e.type==94 || (e.type>=104 && e.type<=107) || (e.type>=111 && e.type<=119))) {
          if(e.type==89 && e.flags==1) {
            capMarkers++;
            if(e.object || !e.thread || !e.start || e.start!=e.end || e.padding0!=profile->padding0 || e.padding1)profileValid=false;
          } else context++;
        }
        if(context>profile->padding0 || capMarkers>1 || (capMarkers && (!profile->padding0 || context!=profile->padding0)))profileValid=false;
      }
      if(profile && (profile->flags==3 || profile->flags==5)) {
        std::set<std::tuple<uint32_t,uint64_t,uint64_t>> identities;
        for(const auto &e:s.events)if(e.type>=96 && e.type<=99) {
          if(!presentationProbeValid(e) || !identities.emplace(e.type,e.object,e.padding0).second)profileValid=false;
        }
      }
      for(const auto &e:s.events)if(!compilerProbeValid(e))profileValid=false;
      if(!completionCardsValid(s.events,profile && (profile->flags==4 || profile->flags==5)))profileValid=false;
      std::set<uint64_t> helperSerials;
      for(const auto &e:s.events)if(e.type==120) {
        if(!profile || profile->flags!=5 || !helperProbeValid(e) || !helperSerials.emplace(e.object).second)profileValid=false;
      }
      std::set<std::tuple<uint64_t,uint64_t,uint32_t>> retirementTypes;
      std::map<std::pair<uint64_t,uint64_t>,std::pair<uint64_t,uint32_t>> retirementBindings;
      std::map<std::pair<uint64_t,uint64_t>,std::map<uint32_t,const trace::Decoded*>> retirementPhases;
      for(const auto &e:s.events)if((e.type>=108 && e.type<=110) || (e.type>=117 && e.type<=119)) {
        if(e.type>=117 && (!profile || (profile->flags!=4 && profile->flags!=5)))profileValid=false;
        if(!retirementProbeValid(e) || !retirementTypes.emplace(e.object,e.padding0,e.type).second)profileValid=false;
        auto [binding,inserted]=retirementBindings.emplace(std::make_pair(e.object,e.padding0),std::make_pair(e.padding1,e.thread));
        if(!inserted && binding->second!=std::make_pair(e.padding1,e.thread))profileValid=false;
        retirementPhases[{e.object,e.padding0}][e.type]=&e;
      }
      for(const auto &[key,phases]:retirementPhases) {
        (void)key;
        const trace::Decoded *prior=nullptr;
        for(uint32_t type:{110u,108u,109u}) {
          auto phase=phases.find(type);if(phase==phases.end())continue;
          if(prior && prior->end>phase->second->start)profileValid=false;
          prior=phase->second;
        }
      }
      for(const auto &[key,phases]:retirementPhases) {
        (void)key;const trace::Decoded *prior=nullptr;
        for(uint32_t type:{117u,118u,119u}) {
          auto phase=phases.find(type);if(phase==phases.end())continue;
          if(prior && prior->end>phase->second->start)profileValid=false;
          auto parent=phases.find(110);
          if(parent!=phases.end() && (phase->second->start<parent->second->start || phase->second->end>parent->second->end))profileValid=false;
          prior=phase->second;
        }
      }
      // Additive groups are optional for old producers. If present, partial or
      // malformed evidence is never silently accepted as valid visibility.
      std::map<std::tuple<uint32_t,uint64_t,uint64_t>,std::array<const trace::Decoded*,4>> visibility;
      for(const auto &e:s.events)if(e.type>=100 && e.type<=103) {
        auto &group=visibility[{e.thread,e.object,e.start}];auto &slot=group[e.type-100];
        if(slot || !e.thread || !e.object || e.start!=e.end)profileValid=false;
        slot=&e;
      }
      using VisibilityPayload=std::tuple<uint64_t,uint64_t,uint64_t,uint64_t,uint32_t>;
      std::map<std::pair<uint64_t,uint64_t>,VisibilityPayload> visibilitySnapshots;
      for(const auto &[key,g]:visibility) {
        if(!g[0] || !g[1] || !g[2] || !g[3]) {profileValid=false;continue;}
        const auto &a=*g[0],&b=*g[1],&c=*g[2],&d=*g[3];
        if(a.flags>4 || (b.flags&~255u) || c.flags || d.flags ||
           bool(b.flags&32u)!=bool(d.padding0&2u))profileValid=false;
        if(a.flags==0 && (!a.padding0 || !a.padding1 || !b.padding0 || b.padding1>=1000000000ULL ||
                         (!(b.flags&1u) && c.padding0)))profileValid=false;
        // Cached observations retain immutable identity even after expiry or
        // when another Presenter reads the same layer. Only age/status and the
        // caller's configuration revision may differ between receipts.
        if(a.padding0 && a.padding1) {
          VisibilityPayload payload{b.padding0,c.padding0,c.padding1,d.padding0,b.flags};
          auto [prior,inserted]=visibilitySnapshots.emplace(std::make_pair(a.padding0,a.padding1),payload);
          if(!inserted && prior->second!=payload)profileValid=false;
        }
      }
      const auto profileLabel=!profileMarkers?"unspecified-full-or-legacy":profileMarkers==1 && profileMarkerValid(*profile)?profileName(profile->flags):"malformed-profile-metadata";
      for(const auto &e:s.events) {
        std::cout<<"{\"kind\":\"event\",\"index\":"<<e.index<<",\"generation\":"<<e.generation<<",\"type\":"<<e.type<<",\"thread\":"<<e.thread<<",\"flags\":"<<e.flags<<",\"object\":"<<e.object<<",\"start\":"<<e.start<<",\"end\":"<<e.end<<",\"padding0\":"<<e.padding0<<",\"padding1\":"<<e.padding1<<",\"clock_domain\":"<<json(clockDomain(e.type))<<",\"duration_ns\":";
        if(e.type!=33 && e.type!=61 && e.type!=62 && !(e.type>=70 && e.type<=78) && !(e.type>=80 && e.type<=85) && e.type!=89 && e.type!=91 && e.type!=92 && e.type!=95 && e.type!=99 && !(e.type>=100 && e.type<=103) && probe::valid(e) && compilerProbeValid(e) && retirementProbeValid(e) && helperProbeValid(e) && e.end>=e.start && std::string(clockDomain(e.type))!="unknown" && (e.type!=24 || (e.flags==4 && e.start && e.end)))std::cout<<(e.end-e.start);else std::cout<<"null";
        if((e.type>=108 && e.type<=110) || (e.type>=117 && e.type<=119))std::cout<<",\"event_name\":"<<json(e.type==108?"Idle retirement sweep":e.type==109?"Ring allocator reclamation aggregate":e.type==110?"Command chunk reset":e.type==117?"Command list destruction":e.type==118?"Reference tracker clear":"Native command buffer release")<<",\"retirement_identity_valid\":"<<(retirementProbeValid(e)?"true":"false")<<",\"queue\":"<<e.object<<",\"chunk_sequence\":"<<e.padding0<<",\"queue_frame\":"<<e.padding1;
        if(e.type==120)std::cout<<",\"event_name\":\"Helper compilation CPU wall scope\",\"helper_identity_valid\":"<<(helperProbeValid(e)?"true":"false")<<",\"span_serial\":"<<e.object<<",\"helper_route_code\":"<<e.padding0<<",\"owner_birth_serial\":"<<e.padding1<<",\"helper_result\":"<<json(e.flags==1?"success":e.flags==2?"failure":"unknown");
        if(e.type>=111 && e.type<=116)std::cout<<",\"event_name\":\"Slow completion card part\",\"completion_card_part\":"<<(e.type-110);
        if(e.type>=104 && e.type<=107)std::cout<<",\"event_name\":"<<json(e.type==104?"Cache reader mutex acquisition wait":e.type==105?"AIR conversion":e.type==106?"Metal newLibrary":"Metal newFunction")<<",\"compiler_identity_valid\":"<<(compilerProbeValid(e)?"true":"false");
        if(e.type>=96 && e.type<=99)std::cout<<",\"event_name\":"<<json(e.type==96?"Queue frame-latency wait":e.type==97?"Swapchain admission wait":e.type==98?"Drawable acquire":"Queue/native frame binding")<<",\"presentation_identity_valid\":"<<(presentationProbeValid(e)?"true":"false");
        if(e.type>=100 && e.type<=103)std::cout<<",\"event_name\":"<<json(e.type==100?"Visibility sampled identity/status":e.type==101?"Visibility native freshness/flags":e.type==102?"Visibility window/display identity":"Visibility occlusion/configuration")<<",\"visibility_sample_only\":true";
        if(e.type==95)std::cout<<",\"event_name\":\"Producer trace profile\",\"producer_profile\":"<<json(profileName(e.flags))<<",\"profile_schema\":"<<e.object<<",\"profile_metadata_valid\":"<<(profileMarkerValid(e)?"true":"false")<<",\"detail_limit\":"<<e.padding0<<",\"lifetime_capacity\":"<<e.padding1;
        if(e.type==71){
          const double seconds=std::bit_cast<double>(e.padding1);
          const bool positive=std::isfinite(seconds)&&seconds>0;
          const bool valid=e.start==e.end && e.object && e.padding0 && !(e.flags&~0x1ffu) && (e.flags&0xffu)==1 && bool(e.flags&0x100u)==positive && positive;
          std::cout<<",\"native_time_bits\":"<<e.padding1<<",\"native_clock_domain\":"<<(e.flags&0xffu)<<",\"native_clock_domain_name\":"<<json((e.flags&0xffu)==1?"ca_host_seconds":"unknown")<<",\"native_presented_seconds\":";
          if(std::isfinite(seconds))std::cout<<std::setprecision(17)<<seconds;else std::cout<<"null";
          std::cout<<",\"native_presented_time_valid\":"<<(valid?"true":"false");
        }
        if(e.type>=30 && e.type<=33){std::cout<<",\"event_name\":"<<json(probe::name(e.type))<<",\"flags_name\":"<<json(probe::flagName(e.type,e.flags))<<",\"probe_semantics_valid\":"<<(probe::valid(e)?"true":"false");
          if(e.type==33)std::cout<<",\"queue\":"<<e.object<<",\"chunk_sequence\":"<<e.padding0<<",\"frame\":"<<e.padding1;
          else{std::cout<<",\"queue\":"<<e.padding0<<",\"call_frame\":"<<e.padding1;if(e.type==31)std::cout<<",\"async_pointer\":"<<e.object;else std::cout<<",\"awaited_sequence\":"<<e.object;}}
        std::cout<<"}\n";
      }
      std::cout<<"{\"kind\":\"summary\",\"identity\":"<<json(m.binding)<<",\"generation\":"<<gen<<",\"events\":"<<s.events.size()<<",\"committed_all_generations\":"<<s.committed<<",\"wrong_generation\":"<<s.wrongGeneration<<",\"next_before_reserved\":"<<s.nextBefore<<",\"next_after_reserved\":"<<s.nextAfter<<",\"dropped_before\":"<<s.droppedBefore<<",\"dropped_after\":"<<s.droppedAfter<<",\"uncommitted_reserved\":"<<s.uncommitted<<",\"counters_changed\":"<<(s.countersChanged?"true":"false")<<",\"inconclusive_window\":"<<((s.inconclusive || !profileValid)?"true":"false")<<",\"producer_profile\":"<<json(profileLabel)<<",\"producer_profile_marker_count\":"<<profileMarkers<<",\"producer_profile_policy_valid\":"<<(profileValid?"true":"false")<<",\"producer_quiescence_verified\":"<<(s.structurallyComplete?"true":"false")<<",\"structurally_complete\":"<<(s.structurallyComplete?"true":"false")<<",\"boundary_rejected_total\":"<<s.boundaryRejected<<",\"all_operations_complete\":false,\"protocol_version\":2,\"live_pid_verified\":false,\"bridge_verified\":false}\n";
      return (s.inconclusive || !profileValid)?3:0;
    }
    std::cerr<<"Usage:\n  cs2trace clock-sample\n  cs2trace new-session ABS_ROOT\n  cs2trace inspect ABS_FILE\n  cs2trace arm ABS_FILE GENERATION DEV:INODE PLANNED_RECORDS TAIL_RESERVE\n  cs2trace stop ABS_FILE GENERATION DEV:INODE\n  cs2trace decode ABS_FILE GENERATION DEV:INODE\n";return 2;
  } catch(const std::exception &e) {std::cerr<<"cs2trace: "<<e.what()<<"\n";return 1;}
}
