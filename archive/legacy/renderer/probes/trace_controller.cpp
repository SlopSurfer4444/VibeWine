#include "trace_core.hpp"
#include <iostream>
#include <limits>

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
static const char *clockDomain(uint32_t type) {
  if(type==24)return "native_gpu_ns";
  if((type>=1 && type<=16)||(type>=20 && type<=23)||type==25)return "cpu_steady_ns";
  return "unknown";
}
int main(int argc,char **argv) {
  try {
    if(argc==3 && std::string(argv[1])=="new-session") {
      std::cout<<"{\"session\":"<<json(trace::newSession(argv[2]))<<",\"runtime_started\":false}\n";return 0;
    }
    if(argc==3 && std::string(argv[1])=="inspect") {
      trace::Mapping m(argv[2],false);
      std::cout<<"{\"identity\":"<<json(m.binding)<<",\"capture\":"<<m.capture()<<",\"next_reserved\":"<<m.next()<<",\"dropped\":"<<m.dropped()<<",\"capacity\":"<<trace::Capacity<<",\"live_pid_verified\":false}\n";return 0;
    }
    if(argc==5 && (std::string(argv[1])=="arm" || std::string(argv[1])=="stop")) {
      uint32_t gen=generation(argv[3]);trace::Mapping m(argv[2],true,argv[4]);
      if(std::string(argv[1])=="arm")m.arm(gen);else m.stop(gen);
      std::cout<<"{\"operation\":"<<json(argv[1])<<",\"identity\":"<<json(m.binding)<<",\"generation\":"<<gen<<",\"capture\":"<<m.capture()<<",\"producer_quiescence_verified\":false}\n";return 0;
    }
    if(argc==5 && std::string(argv[1])=="decode") {
      uint32_t gen=generation(argv[3]);trace::Mapping m(argv[2],false,argv[4]);auto s=trace::decode(m,gen);
      for(const auto &e:s.events) {
        std::cout<<"{\"kind\":\"event\",\"index\":"<<e.index<<",\"generation\":"<<e.generation<<",\"type\":"<<e.type<<",\"thread\":"<<e.thread<<",\"flags\":"<<e.flags<<",\"object\":"<<e.object<<",\"start\":"<<e.start<<",\"end\":"<<e.end<<",\"padding0\":"<<e.padding0<<",\"padding1\":"<<e.padding1<<",\"clock_domain\":"<<json(clockDomain(e.type))<<",\"duration_ns\":";
        if(e.end>=e.start && std::string(clockDomain(e.type))!="unknown" && (e.type!=24 || (e.flags==4 && e.start && e.end)))std::cout<<(e.end-e.start);else std::cout<<"null";
        std::cout<<"}\n";
      }
      std::cout<<"{\"kind\":\"summary\",\"identity\":"<<json(m.binding)<<",\"generation\":"<<gen<<",\"events\":"<<s.events.size()<<",\"committed_all_generations\":"<<s.committed<<",\"wrong_generation\":"<<s.wrongGeneration<<",\"next_before_reserved\":"<<s.nextBefore<<",\"next_after_reserved\":"<<s.nextAfter<<",\"dropped_before\":"<<s.droppedBefore<<",\"dropped_after\":"<<s.droppedAfter<<",\"uncommitted_reserved\":"<<s.uncommitted<<",\"counters_changed\":"<<(s.countersChanged?"true":"false")<<",\"inconclusive_window\":"<<(s.inconclusive?"true":"false")<<",\"producer_quiescence_verified\":false,\"live_pid_verified\":false,\"bridge_verified\":false}\n";
      return s.inconclusive?3:0;
    }
    std::cerr<<"Usage:\n  cs2trace new-session ABS_ROOT\n  cs2trace inspect ABS_FILE\n  cs2trace arm ABS_FILE GENERATION DEV:INODE\n  cs2trace stop ABS_FILE GENERATION DEV:INODE\n  cs2trace decode ABS_FILE GENERATION DEV:INODE\n";return 2;
  } catch(const std::exception &e) {std::cerr<<"cs2trace: "<<e.what()<<"\n";return 1;}
}
