#include "trace_core.hpp"
#include "probe_semantics.hpp"
#include <iostream>
#include <functional>
#include <sys/wait.h>

using namespace trace;
static size_t checks=0;
static std::string cliFixture,cliBinding;
static std::string cliInvalidGPU,cliInvalidGPUBinding;
static std::string cliSync,cliSyncBinding;
static void check(bool value,const char *why){++checks;if(!value)fail(std::string("test failed: ")+why);}
static void rejects(const std::function<void()> &fn,const char *why){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}check(rejected,why);}
struct Fixture {
  std::string session,path,binding;FD fd;Header *h;Event *events;
  explicit Fixture(const std::string &root):session(newSession(root)),path(session+"/events-123.bin"),fd(open(path.c_str(),O_RDWR|O_CREAT|O_EXCL,0600)) {
    if(fd.fd<0)osfail("fixture create");if(ftruncate(fd.fd,Bytes)<0)osfail("fixture length");
    void *ptr=mmap(nullptr,Bytes,PROT_READ|PROT_WRITE,MAP_SHARED,fd.fd,0);if(ptr==MAP_FAILED)osfail("fixture map");
    h=static_cast<Header*>(ptr);events=reinterpret_cast<Event*>(static_cast<char*>(ptr)+4096);
    h->version=1;h->capacity=Capacity;Mapping::atomic(h->magic).store(Magic,std::memory_order_release);
    struct stat s{};if(fstat(fd.fd,&s)<0)osfail("fixture stat");binding=identity(s);
  }
  ~Fixture(){munmap(h,Bytes);}
  void event(uint64_t index,uint32_t gen,uint64_t token=1) {
    Event &e=events[index];e.start=token;e.end=token+100;e.object=token*3;e.type=24;e.thread=17;e.flags=4;e.reserved=gen;e.padding[0]=token*5;e.padding[1]=token*7;
    Mapping::atomic(e.committed).store(index+1,std::memory_order_release);
  }
};
// Exact record() extracted at build time from diagnostic/source/src/d3d11/cs2_trace.hpp.
namespace exact_producer {
using trace::Header;using trace::Event;constexpr uint64_t Capacity=trace::Capacity;
static Header *current;
inline Header *buffer(){return current;}
inline uint32_t GetCurrentThreadId(){return 17;}
#include "producer-record.inc"
}
static void schemaFailures(const std::string &root) {
  Fixture f(root);
  {Mapping m(f.path,false);check(m.binding==f.binding,"valid binding");}
  rejects([&]{Mapping m(f.path,false,"0:0");},"identity mismatch");
  rejects([&]{Mapping m(f.path,true);},"explicit mutation identity required");
  Mapping::atomic(f.h->magic).store(0);rejects([&]{Mapping m(f.path,false);},"unpublished magic");Mapping::atomic(f.h->magic).store(Magic);
  f.h->version=2;rejects([&]{Mapping m(f.path,false);},"version");f.h->version=1;
  f.h->capacity=1;rejects([&]{Mapping m(f.path,false);},"capacity");f.h->capacity=Capacity;
  check(ftruncate(f.fd.fd,Bytes-1)==0,"truncate fixture");rejects([&]{Mapping m(f.path,false);},"truncated");
  check(ftruncate(f.fd.fd,Bytes+1)==0,"oversize fixture");rejects([&]{Mapping m(f.path,false);},"oversized");check(ftruncate(f.fd.fd,Bytes)==0,"restore fixture");
  std::string link=f.session+"/symlink.bin";check(symlink(f.path.c_str(),link.c_str())==0,"fixture symlink");rejects([&]{Mapping m(link,false);},"symlink file");
  std::string fifo=f.session+"/fifo.bin";check(mkfifo(fifo.c_str(),0600)==0,"fixture fifo");rejects([&]{Mapping m(fifo,false);},"FIFO without blocking");
  rejects([&]{Mapping m(f.session+"/../events-123.bin",false);},"parent traversal");
  std::string rootLink=root+"/session-link";check(symlink(f.session.c_str(),rootLink.c_str())==0,"directory symlink");rejects([&]{Mapping m(rootLink+"/events-123.bin",false);},"directory symlink");
  check(unlink(rootLink.c_str())==0,"remove fixture link");
  {Mapping m(f.path,false);check(rename(f.path.c_str(),(f.path+".old").c_str())==0,"rename fixture");FD other(open(f.path.c_str(),O_RDWR|O_CREAT|O_EXCL,0600));check(other.fd>=0 && ftruncate(other.fd,Bytes)==0,"replacement fixture");rejects([&]{m.verifyBinding();},"inode replaced");}
}
static void generations(const std::string &root) {
  Fixture f(root);
  {Mapping m(f.path,true,f.binding);rejects([&]{m.arm(0);},"zero generation");m.arm(12);check(m.capture()==12,"arm");rejects([&]{m.arm(13);},"arm collision");rejects([&]{m.stop(13);},"foreign stop");check(m.capture()==12,"foreign stop preserves capture");m.stop(12);check(m.capture()==0,"same generation stop");rejects([&]{m.stop(12);},"inactive stop");rejects([&]{m.arm(12);},"zero-event generation reuse ledger");}
  f.event(0,77);Mapping::atomic(f.h->next).store(1);
  {Mapping m(f.path,true,f.binding);rejects([&]{m.arm(77);},"committed generation reuse scan");m.arm(78);m.stop(78);check(m.next()==1,"controller preserves next");check(Mapping::atomic(f.events[0].committed).load()==1,"controller preserves event");}
  {Mapping m(f.path,true,f.binding);m.arm(90);}
  {Mapping m(f.path,false,f.binding);rejects([&]{decode(m,90);},"armed decode rejected");}
  {Mapping m(f.path,true,f.binding);m.stop(90);}
}
static void decoderCases(const std::string &root) {
  Fixture f(root);Mapping::atomic(f.h->next).store(3);
  f.event(0,1,10);f.event(2,2,30);f.events[1].reserved=1;f.events[1].start=UINT64_MAX; // Deliberately unpublished poison.
  {Mapping m(f.path,false,f.binding);auto s=decode(m,1);check(s.events.size()==1 && s.events[0].start==10 && s.events[0].padding0==50 && s.events[0].padding1==70,"decoded payload/padding");check(s.committed==2 && s.wrongGeneration==1 && s.uncommitted==1 && s.inconclusive,"generation filter and publication hole");rejects([&]{decode(m,0);},"zero decode generation");}
  Mapping::atomic(f.events[1].committed).store(999,std::memory_order_release);
  {Mapping m(f.path,false);rejects([&]{decode(m,1);},"wrong-index committed marker");}
  Mapping::atomic(f.events[1].committed).store(0,std::memory_order_release);
  Mapping::atomic(f.h->next).store(UINT64_MAX);Mapping::atomic(f.h->dropped).store(8);
  {Mapping m(f.path,false);auto s=decode(m,1);check(s.droppedAfter==8 && s.events.size()==1 && s.inconclusive && s.uncommitted==Capacity-2,"huge next bounded with drops");}
}
static void exactProducer(const std::string &root) {
  Fixture f(root);exact_producer::current=f.h;
  Mapping::atomic(f.h->capture).store(7);exact_producer::record(7,24,100,400,55,8,66,77);
  check(Mapping::atomic(f.h->next).load()==1,"exact producer reserves slot");
  Mapping::atomic(f.h->capture).store(0);exact_producer::record(7,24,900,1000,55,8,99,101);
  check(Mapping::atomic(f.h->next).load()==1,"exact producer rejects changed capture");exact_producer::record(0,24,900,1000);
  check(Mapping::atomic(f.h->next).load()==1,"exact producer rejects zero generation");
  {Mapping m(f.path,false);auto s=decode(m,7);check(s.events.size()==1 && s.events[0].start==100 && s.events[0].end==400 && s.events[0].object==55 && s.events[0].type==24 && s.events[0].thread==17 && s.events[0].flags==8 && s.events[0].padding0==66 && s.events[0].padding1==77,"exact producer decoded fields");}
  Mapping::atomic(f.h->next).store(Capacity);Mapping::atomic(f.h->capture).store(8);exact_producer::record(8,20,1,1,2,0,3,4);Mapping::atomic(f.h->capture).store(0);
  check(Mapping::atomic(f.h->next).load()==Capacity+1 && Mapping::atomic(f.h->dropped).load()==1,"exact producer capacity drop");
  {Mapping m(f.path,false);auto s=decode(m,8);check(s.events.empty() && s.droppedAfter==1 && s.inconclusive,"exact producer dropped snapshot");}
  cliInvalidGPU=f.path;cliInvalidGPUBinding=f.binding;
}
static void competingControllers(const std::string &root) {
  Fixture f(root);int gate[2];check(pipe(gate)==0,"CAS test pipe");
  pid_t child=fork();if(child<0)osfail("fork arm test");
  if(child==0){close(gate[1]);char c;read(gate[0],&c,1);try{Mapping m(f.path,true,f.binding);m.arm(101);_exit(0);}catch(...){_exit(1);}}
  close(gate[0]);check(write(gate[1],"x",1)==1,"release CAS contender");close(gate[1]);
  bool parentWon=false;try{Mapping m(f.path,true,f.binding);m.arm(102);parentWon=true;}catch(...){ }
  int status=0;check(waitpid(child,&status,0)==child && WIFEXITED(status),"CAS child completion");
  check(int(parentWon)+int(WEXITSTATUS(status)==0)==1,"exactly one controller wins");
  uint32_t active=Mapping::atomic(f.h->capture).load();check(active==101 || active==102,"contender capture value");
  {Mapping m(f.path,true,f.binding);m.stop(active);}
  // Exercise the actual shared-memory CAS independently of advisory controller locking.
  int results[2];check(pipe(results)==0,"native CAS results pipe");child=fork();if(child<0)osfail("fork native CAS");
  if(child==0){close(results[0]);uint32_t expected=0;bool won=Mapping::atomic(f.h->capture).compare_exchange_strong(expected,201);write(results[1],&won,sizeof(won));_exit(0);}
  close(results[1]);uint32_t expected=0;bool won=Mapping::atomic(f.h->capture).compare_exchange_strong(expected,202);bool childWon=false;
  check(read(results[0],&childWon,sizeof(childWon))==sizeof(childWon),"native CAS child receipt");close(results[0]);check(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0,"native CAS child status");
  check(int(won)+int(childWon)==1,"native cross-process CAS single winner");Mapping::atomic(f.h->capture).store(0);
}
static void publicationConsistency(const std::string &root) {
  Fixture f(root);int ready[2],release[2];check(pipe(ready)==0 && pipe(release)==0,"publication pipes");
  pid_t child=fork();if(child<0)osfail("fork publication test");
  if(child==0){close(ready[0]);close(release[1]);Mapping::atomic(f.h->next).fetch_add(1,std::memory_order_relaxed);write(ready[1],"r",1);char c;read(release[0],&c,1);f.event(0,31,100);
    for(uint64_t i=1;i<5000;++i){Mapping::atomic(f.h->next).fetch_add(1,std::memory_order_relaxed);f.event(i,31,100+i);} _exit(0);}
  close(ready[1]);close(release[0]);char c;check(read(ready[0],&c,1)==1,"producer has reserved unpublished slot");close(ready[0]);
  Mapping m(f.path,false);auto pending=decode(m,31);check(pending.events.empty() && pending.uncommitted==1 && pending.inconclusive,"reserved is not committed");
  check(write(release[1],"r",1)==1,"release publication");close(release[1]);
  int status=0;bool finished=false;size_t scans=0;
  while(!finished) {
    auto s=decode(m,31);++scans;
    for(const auto &e:s.events)check(e.start==100+e.index && e.end==e.start+100 && e.object==e.start*3 && e.padding0==e.start*5 && e.padding1==e.start*7 && e.thread==17 && e.flags==4 && e.generation==31,"acquire publication payload consistency");
    pid_t result=waitpid(child,&status,WNOHANG);if(result<0)osfail("wait publication child");finished=result==child;
  }
  check(WIFEXITED(status) && WEXITSTATUS(status)==0 && scans>0,"publication child completed");auto s=decode(m,31);
  check(s.events.size()==5000 && s.uncommitted==0 && !s.inconclusive,"late post-stop commits admitted after settle");
  cliFixture=f.path;cliBinding=f.binding;
}
static void invisibleLateProducer(const std::string &root) {
  Fixture f(root);int ready[2],release[2];check(pipe(ready)==0 && pipe(release)==0,"invisible producer pipes");
  Mapping::atomic(f.h->capture).store(41);
  pid_t child=fork();if(child<0)osfail("fork invisible producer");
  if(child==0){close(ready[0]);close(release[1]);if(Mapping::atomic(f.h->capture).load()!=41)_exit(1);write(ready[1],"p",1);char c;read(release[0],&c,1); // Producer has passed generation check before reservation.
    Mapping::atomic(f.h->next).fetch_add(1,std::memory_order_relaxed);f.event(0,41,222);_exit(0);}
  close(ready[1]);close(release[0]);char c;check(read(ready[0],&c,1)==1,"producer passed generation check");close(ready[0]);
  {Mapping m(f.path,true,f.binding);m.stop(41);}
  {Mapping m(f.path,false);auto s=decode(m,41);check(s.events.empty() && s.nextAfter==0 && !s.inconclusive,"coherent empty snapshot does not prove quiescence");}
  check(write(release[1],"r",1)==1,"release invisible late producer");close(release[1]);int status=0;
  check(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0,"invisible producer completed");
  {Mapping m(f.path,false);auto s=decode(m,41);check(s.events.size()==1 && s.events[0].start==222,"new event can appear after coherent stopped snapshot");}
}
static void syncProducer(const std::string &root) {
  Fixture f(root);exact_producer::current=f.h;Mapping::atomic(f.h->capture).store(71);
  exact_producer::record(0,30,100,200,1,1,99,4);exact_producer::record(0,33,100,100,99,1,1,3);
  check(Mapping::atomic(f.h->next).load()==0,"new scopes/reasons skip zero generation");
  for(uint32_t t=20;t<=23;++t)exact_producer::record(71,t,1'000'000'000+(t-20)*2'000'000,1'000'000'000+(t-20)*2'000'000,99,t==23?4:0,1,3);
  exact_producer::record(71,24,9'000'000'000'000,9'000'000'008'000,99,4,1,3);
  exact_producer::record(71,25,1'000'000'000,1'000'000'000,11,0,99,3);
  for(uint32_t flags=1;flags<=3;++flags)exact_producer::record(71,30,1'200'000'000,1'200'001'000,1,flags,99,4);
  for(uint32_t flags:{0u,1u,2u,3u,5u})exact_producer::record(71,31,1'300'000'000,1'300'000'000,1,flags,99,4);
  exact_producer::record(71,32,1'400'000'000,1'400'002'000,1,0,99,4);
  for(uint32_t reason=0;reason<=8;++reason)exact_producer::record(71,33,1'000'000'001+reason,1'000'000'001+reason,99,reason,reason+1,3);
  exact_producer::record(71,30,100,200,1,4,99,4);
  exact_producer::record(71,31,100,200,1,4,99,4);
  exact_producer::record(71,32,100,200,1,1,99,4);
  exact_producer::record(71,33,100,100,99,9,50,4);
  exact_producer::record(71,33,100,200,99,1,51,4);
  exact_producer::record(71,30,200,100,1,1,99,4);
  Mapping::atomic(f.h->capture).store(72);exact_producer::record(72,33,100,100,99,1,99,4);Mapping::atomic(f.h->capture).store(0);
  Mapping m(f.path,false);auto s=decode(m,71);check(s.events.size()==30 && s.wrongGeneration==1,"new producer generation filter");
  size_t valid=0,invalid=0;
  for(const auto&e:s.events)if(e.type>=30){if(probe::valid(e))++valid;else ++invalid;}
  check(valid==18 && invalid==6,"new types/reasons semantic validation");
  check(std::string(probe::reason(0))=="Unknown" && std::string(probe::reason(8))=="PassPromotion" && std::string(probe::reason(9))=="invalid","reason enum bounds");
  cliSync=f.path;cliSyncBinding=f.binding;
}
int main(int argc,char **argv) {
  try {if(argc!=2)fail("usage trace_tests ABS_TEST_ROOT");
    schemaFailures(argv[1]);generations(argv[1]);decoderCases(argv[1]);exactProducer(argv[1]);competingControllers(argv[1]);publicationConsistency(argv[1]);invisibleLateProducer(argv[1]);syncProducer(argv[1]);
    std::cout<<"{\"synthetic_checks\":"<<checks<<",\"result\":\"pass\",\"cross_process_atomic_fixture\":true,\"live_runtime_test\":false,\"cli_fixture\":\""<<cliFixture<<"\",\"cli_identity\":\""<<cliBinding<<"\",\"cli_invalid_gpu_fixture\":\""<<cliInvalidGPU<<"\",\"cli_invalid_gpu_identity\":\""<<cliInvalidGPUBinding<<"\",\"cli_sync_fixture\":\""<<cliSync<<"\",\"cli_sync_identity\":\""<<cliSyncBinding<<"\"}\n";
  }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}
}
