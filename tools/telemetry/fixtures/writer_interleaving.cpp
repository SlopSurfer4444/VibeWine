// Native offline fixture. The hook interrupts the same writeEvent called by
// cs2_trace.hpp; no replacement reservation/commit implementation is used.
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

static int barrierStage=0, barrierNotify=-1, barrierRelease=-1;
static void writerBarrier(int point) {
  if(point!=barrierStage)return;
  char c=char(point);
  ssize_t n;do {n=write(barrierNotify,&c,1);}while(n<0&&errno==EINTR);
  if(n!=1)_exit(90);
  do {n=read(barrierRelease,&c,1);}while(n<0&&errno==EINTR);
  if(n!=1)_exit(91);
}
#define DXMT_TRACE_WRITER_BARRIER(point) writerBarrier(point)
#include "trace_core.hpp"
#undef DXMT_TRACE_WRITER_BARRIER
// Exercise the retained actual old decoder, not a guessed old-header check.
#define trace legacy_trace
#include "legacy/trace_core.hpp"
#undef trace

using namespace trace;
static void require(bool value,const std::string &message) {
  if(!value)throw std::runtime_error(message);
}
template<class F> static void rejects(F &&f,const std::string &why) {
  bool rejected=false;try {f();}catch(const std::exception &){rejected=true;}
  require(rejected,"accepted forbidden operation: "+why);
}
static void must(int result,const std::string &what) {
  if(result<0)throw std::runtime_error(what+": "+std::strerror(errno));
}
struct Fixture {
  std::string root,session,path,binding;
  Fixture() {
    char name[]="/private/tmp/cs2trace-protocol-XXXXXX";
    char *made=mkdtemp(name);if(!made)osfail("fixture root");root=made;
    try {session=newSession(root);path=session+"/trace.bin";makeFile(path);binding=fileIdentity(path);}
    catch(...) {std::filesystem::remove_all(root);throw;}
  }
  ~Fixture(){std::error_code ignored;std::filesystem::remove_all(root,ignored);}
  static std::string fileIdentity(const std::string &p) {
    struct stat s{};if(stat(p.c_str(),&s)<0)osfail("fixture stat");return identity(s);
  }
  static void makeFile(const std::string &p) {
    FD fd(open(p.c_str(),O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC,0600));
    if(fd.fd<0)osfail("fixture file");if(ftruncate(fd.fd,Bytes)<0)osfail("fixture truncate");
    Header h{Magic,Version,Capacity,0,controlWord(0,CaptureState::Closed),0,0};
    if(pwrite(fd.fd,&h,sizeof(h),0)!=ssize_t(sizeof(h)))osfail("fixture header");
  }
  void header(uint64_t magic,uint64_t version) {
    FD fd(open(path.c_str(),O_RDWR|O_CLOEXEC));if(fd.fd<0)osfail("fixture header open");
    uint64_t values[]{magic,version};
    if(pwrite(fd.fd,values,sizeof(values),0)!=ssize_t(sizeof(values)))osfail("fixture header replace");
  }
  void marker(const std::string &contents) {
    FD fd(open((session+"/.cs2trace-session").c_str(),O_WRONLY|O_TRUNC|O_CLOEXEC));
    if(fd.fd<0)osfail("fixture marker open");
    if(write(fd.fd,contents.data(),contents.size())!=ssize_t(contents.size()))osfail("fixture marker replace");
  }
  std::unique_ptr<Mapping> writable() {return std::make_unique<Mapping>(path,true,binding);}
};
struct Writer {
  pid_t pid=-1;FD ready,release;
  Writer(Mapping &m,uint32_t gen,int stage,uint64_t object=0x1234) {
    int notify[2],resume[2];must(pipe(notify),"notify pipe");
    if(pipe(resume)<0){close(notify[0]);close(notify[1]);osfail("resume pipe");}
    pid=fork();if(pid<0){for(int fd:notify)close(fd);for(int fd:resume)close(fd);osfail("fork writer");}
    if(pid==0) {
      close(notify[0]);close(resume[1]);
      barrierStage=stage;barrierNotify=notify[1];barrierRelease=resume[0];
      writeEvent(m.h,gen,30,100,150,object,1,200,300,77);
      _exit(0);
    }
    close(notify[1]);close(resume[0]);ready=FD(notify[0]);release=FD(resume[1]);
    try {
      pollfd p{ready.fd,POLLIN,0};int r;do {r=poll(&p,1,3000);}while(r<0&&errno==EINTR);
      require(r==1&&(p.revents&POLLIN),"writer failed to reach barrier");
      char point=0;require(read(ready.fd,&point,1)==1&&point==stage,"wrong writer barrier");
    }catch(...) {kill(pid,SIGKILL);int status;waitpid(pid,&status,0);pid=-1;throw;}
  }
  ~Writer(){if(pid>0){kill(pid,SIGKILL);int status;while(waitpid(pid,&status,0)<0&&errno==EINTR){}}}
  void resume() {
    char c='r';ssize_t n;do {n=write(release.fd,&c,1);}while(n<0&&errno==EINTR);
    require(n==1,"failed to release writer");
  }
  void reap(bool killed=false) {
    int status=0;pid_t result;do {result=waitpid(pid,&status,0);}while(result<0&&errno==EINTR);
    require(result==pid,"waitpid writer");pid=-1;
    require(killed?(WIFSIGNALED(status)&&WTERMSIG(status)==SIGKILL):(WIFEXITED(status)&&WEXITSTATUS(status)==0),"unexpected writer exit");
  }
  void killWriter(){must(kill(pid,SIGKILL),"kill writer");reap(true);}
};
static void state(Mapping &m,uint32_t gen,CaptureState s,uint64_t count) {
  auto word=m.control();require(controlGeneration(word)==gen,"wrong control generation");
  require(controlState(word)==s,"wrong capture state");require(controlWriters(word)==count,"wrong admitted writer count");
}
static void event(const Snapshot &s,uint32_t gen) {
  require(s.events.size()==1,"producer event missing/duplicated");const auto &e=s.events[0];
  require(e.index==0&&e.generation==gen&&e.start==100&&e.end==150&&e.object==0x1234&&e.type==30&&e.thread==77&&e.flags==1&&e.padding0==200&&e.padding1==300,"producer payload corrupted");
}
static void timeoutBoundary(int stage) {
  Fixture f;auto m=f.writable();m->arm(17);Writer w(*m,17,stage);
  state(*m,17,CaptureState::Open,1);
  require(m->next()==uint64_t(stage>1),"barrier reservation mismatch");
  require(Mapping::atomic(m->events[0].committed).load()==0,"writer published before barrier release");
  rejects([&]{m->stop(18,std::chrono::milliseconds(0));},"wrong stop generation");
  state(*m,17,CaptureState::Open,1);
  require(!m->stop(17,std::chrono::milliseconds(0)),"timeout falsely claimed drain");
  state(*m,17,CaptureState::Closing,1);
  auto pending=decode(*m,17);
  require(!pending.structurallyComplete&&pending.inconclusive,"parked writer falsely complete");
  require(!pending.countersChanged&&pending.events.empty(),"parked snapshot unexpectedly changed");
  require(pending.uncommitted==uint64_t(stage>1),"parked reservation accounting");
  rejects([&]{m->arm(18);},"rearm with admitted writer");
  rejects([&]{m->stop(17,std::chrono::milliseconds(0));},"second stop of Closing capture");
  auto next=m->next();writeEvent(m->h,17,1,0,1,0,0,0,0,0);
  require(m->next()==next&&m->h->boundaryRejected==1,"Closing did not reject new producer admission");
  w.resume();w.reap();state(*m,17,CaptureState::Closed,0);
  auto finished=decode(*m,17);require(finished.structurallyComplete&&!finished.inconclusive,"released writer did not complete");event(finished,17);
  rejects([&]{m->arm(17);},"reuse committed generation");
  m->arm(18);auto before=m->control();next=m->next();auto rejected=m->h->boundaryRejected;
  writeEvent(m->h,17,1,0,1,0,0,0,0,0);
  require(m->control()==before&&m->next()==next&&m->h->boundaryRejected==rejected,"stale generation modified new capture");
  require(m->stop(18,std::chrono::milliseconds(0)),"empty subsequent capture failed stop");
  auto subsequent=decode(*m,18);require(subsequent.structurallyComplete&&!subsequent.inconclusive&&subsequent.events.empty()&&subsequent.wrongGeneration==1,"old event incorrectly attributed to new capture");
}
static void drainBoundary(int stage) {
  Fixture f;auto m=f.writable();m->arm(29);Writer w(*m,29,stage);
  std::atomic<bool> sawClosing=false,releaseFailed=false;
  std::thread releaser([&] {
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(std::chrono::steady_clock::now()<deadline) {
      if(controlState(m->control())==CaptureState::Closing) {
        sawClosing=true;try {w.resume();}catch(...) {releaseFailed=true;}return;
      }
      std::this_thread::yield();
    }
  });
  bool drained=false;try {drained=m->stop(29,std::chrono::milliseconds(2000));}
  catch(...){releaser.join();throw;}releaser.join();
  require(sawClosing&&!releaseFailed&&drained,"stop did not wait for admitted producer drain");
  w.reap();state(*m,29,CaptureState::Closed,0);auto s=decode(*m,29);
  require(s.structurallyComplete&&!s.inconclusive,"drained snapshot incomplete");event(s,29);
}
static void deadBoundary(int stage) {
  Fixture f;auto m=f.writable();m->arm(31);Writer w(*m,31,stage);w.killWriter();
  require(!m->stop(31,std::chrono::milliseconds(5)),"dead admitted writer falsely drained");
  state(*m,31,CaptureState::Closing,1);auto s=decode(*m,31);
  require(!s.structurallyComplete&&s.inconclusive&&!s.countersChanged,"dead admitted writer falsely complete");
  require(s.uncommitted==uint64_t(stage>1),"dead writer reservation accounting");
  rejects([&]{m->arm(32);},"rearm after dead admitted writer");
}
static void multipleWriters() {
  Fixture f;auto m=f.writable();m->arm(41);
  Writer a(*m,41,1,11),b(*m,41,1,22),c(*m,41,1,33);
  state(*m,41,CaptureState::Open,3);require(!m->stop(41,std::chrono::milliseconds(0)),"multiple writers falsely drained");
  a.resume();a.reap();state(*m,41,CaptureState::Closing,2);
  b.resume();b.reap();state(*m,41,CaptureState::Closing,1);
  require(!decode(*m,41).structurallyComplete,"penultimate release falsely closed capture");
  c.resume();c.reap();state(*m,41,CaptureState::Closed,0);auto s=decode(*m,41);
  require(s.structurallyComplete&&!s.inconclusive&&s.events.size()==3,"final release did not close multiwriter capture");
  require(s.events[0].object==11&&s.events[1].object==22&&s.events[2].object==33,"reservation order/payload mismatch");
}
static void overflow() {
  Fixture f;auto m=f.writable();m->arm(43);Mapping::atomic(m->h->next).store(Capacity);
  Writer w(*m,43,2);require(m->next()==Capacity+1,"overflow reservation absent");
  require(!m->stop(43,std::chrono::milliseconds(0)),"overflow writer prematurely drained");
  w.resume();w.reap();state(*m,43,CaptureState::Closed,0);
  require(m->dropped()==1,"overflow not counted");auto s=decode(*m,43);
  require(s.structurallyComplete&&s.inconclusive&&s.events.empty(),"overflow did not release writer or retain loss qualifier");
}
static void generationAndDecode() {
  Fixture f;auto m=f.writable();
  rejects([&]{m->arm(0);},"zero generation arm");rejects([&]{m->stop(0);},"zero generation stop");rejects([&]{decode(*m,0);},"zero generation decode");
  m->arm(UINT32_MAX);rejects([&]{decode(*m,UINT32_MAX);},"decode while Open");
  writeEvent(m->h,0,1,0,1,0,0,0,0,0);require(m->next()==0,"zero-generation writer admitted");
  require(m->stop(UINT32_MAX,std::chrono::milliseconds(0)),"max generation stop");
  require(decode(*m,UINT32_MAX).structurallyComplete,"empty capture not complete");
  rejects([&]{decode(*m,1);},"decode wrong generation");rejects([&]{m->stop(UINT32_MAX);},"stop already Closed");
  rejects([&]{m->arm(UINT32_MAX);},"reuse empty generation ledger");
  m->arm(1);require(m->stop(1,std::chrono::milliseconds(0)),"fresh generation stop");
  Mapping::atomic(m->h->next).store(1);auto s=decode(*m,1);
  require(s.structurallyComplete&&s.inconclusive&&s.uncommitted==1,"Closed reservation hole did not retain loss qualifier");
  Mapping::atomic(m->events[0].committed).store(2);rejects([&]{decode(*m,1);},"malformed publication marker");
}
static void writerSaturation() {
  Fixture f;auto m=f.writable();m->arm(51);
  Mapping::atomic(m->h->control).store(controlWord(51,CaptureState::Open,WriterMask));
  auto before=m->control();writeEvent(m->h,51,1,0,1,0,0,0,0,0);
  require(m->control()==before&&m->next()==0&&m->h->boundaryRejected==1,"writer count overflow changed control");
  require(!m->stop(51,std::chrono::milliseconds(0)),"saturated count falsely drained");state(*m,51,CaptureState::Closing,WriterMask);
}
static void versions() {
  {Fixture f;f.marker(legacy_trace::Marker);f.header(legacy_trace::Magic,1);legacy_trace::Mapping old(f.path,false);}
  {Fixture f;f.marker(legacy_trace::Marker);f.header(legacy_trace::Magic,1);rejects([&]{Mapping m(f.path,false);},"new decoder accepted v1 session/header");}
  {Fixture f;f.header(legacy_trace::Magic,1);rejects([&]{Mapping m(f.path,false);},"new decoder accepted v1 magic under v2 session");}
  {Fixture f;f.header(Magic,1);rejects([&]{Mapping m(f.path,false);},"new decoder accepted old version with new magic");}
  {Fixture f;rejects([&]{legacy_trace::Mapping m(f.path,false);},"old decoder accepted v2 marker/header");}
  {Fixture f;f.marker(legacy_trace::Marker);rejects([&]{legacy_trace::Mapping m(f.path,false);},"old decoder accepted v2 magic under v1 session");}
}
static void pathControls() {
  {Fixture f;rejects([&]{Mapping m("trace.bin",false);},"relative trace path");rejects([&]{Mapping m(f.root+"/trace.bin",false);},"non-session directory");}
  {Fixture f;rejects([&]{Mapping m(f.session+"/../"+std::filesystem::path(f.session).filename().string()+"/trace.bin",false);},"dot path component");}
  {Fixture f;must(symlink(f.session.c_str(),(f.root+"/link").c_str()),"ancestor symlink");rejects([&]{openDirectory(f.root+"/link");},"symlink directory ancestor");}
  {Fixture f;must(rename(f.path.c_str(),(f.path+".real").c_str()),"rename trace");must(symlink((f.path+".real").c_str(),f.path.c_str()),"trace symlink");rejects([&]{Mapping m(f.path,false);},"symlink trace");}
  {Fixture f;must(link(f.path.c_str(),(f.path+".link").c_str()),"hardlink trace");rejects([&]{Mapping m(f.path,false);},"hardlinked trace");}
  {Fixture f;must(truncate(f.path.c_str(),Bytes-1),"truncate trace");rejects([&]{Mapping m(f.path,false);},"wrong length trace");}
  {Fixture f;must(unlink(f.path.c_str()),"unlink trace");must(mkfifo(f.path.c_str(),0600),"trace fifo");rejects([&]{Mapping m(f.path,false);},"nonregular trace");}
  {Fixture f;must(chmod(f.session.c_str(),0755),"chmod session");rejects([&]{Mapping m(f.path,false);},"nonprivate session");}
  {Fixture f;f.marker("INVALID_SESSION_V2\n");rejects([&]{Mapping m(f.path,false);},"wrong marker contents");}
  {Fixture f;auto marker=f.session+"/.cs2trace-session";must(link(marker.c_str(),(marker+".link").c_str()),"hardlink marker");rejects([&]{Mapping m(f.path,false);},"hardlinked marker");}
  {Fixture f;auto marker=f.session+"/.cs2trace-session";must(rename(marker.c_str(),(marker+".real").c_str()),"rename marker");must(symlink((marker+".real").c_str(),marker.c_str()),"marker symlink");rejects([&]{Mapping m(f.path,false);},"symlink marker");}
  {Fixture f;rejects([&]{Mapping m(f.path,true);},"writable mapping without identity");rejects([&]{Mapping m(f.path,true,"0:0");},"wrong identity");}
  {Fixture f;auto m=f.writable();rejects([&]{Mapping other(f.path,true,f.binding);},"second session controller");rejects([&]{Mapping other(f.path,false);},"reader during controller ownership");}
  {Fixture f;Mapping m(f.path,false);rejects([&]{m.arm(1);},"arm read-only mapping");rejects([&]{m.stop(1);},"stop read-only mapping");}
}
static void bindingReplacement() {
  {Fixture f;auto m=f.writable();must(rename(f.path.c_str(),(f.path+".old").c_str()),"rename bound trace");Fixture::makeFile(f.path);rejects([&]{m->arm(61);},"replaced bound trace");require(m->capture()==0,"replaced file mutated old control");}
  {Fixture f;auto m=f.writable();auto marker=f.session+"/.cs2trace-session";must(rename(marker.c_str(),(marker+".old").c_str()),"rename bound marker");FD fd(open(marker.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600));require(fd.fd>=0,"new marker");require(write(fd.fd,Marker,std::strlen(Marker))==ssize_t(std::strlen(Marker)),"new marker contents");rejects([&]{m->arm(61);},"replaced bound lock marker");}
  {Fixture f;auto m=f.writable();must(rename(f.session.c_str(),(f.session+".old").c_str()),"rename bound session");must(mkdir(f.session.c_str(),0700),"replace session");rejects([&]{m->arm(61);},"replaced bound session directory");}
  {Fixture f;auto m=f.writable();m->arm(61);require(m->stop(61,std::chrono::milliseconds(0)),"close fixture");must(truncate(f.path.c_str(),Bytes-1),"truncate mapped trace");rejects([&]{decode(*m,61);},"truncated mapped trace");}
  {Fixture f;auto m=f.writable();m->arm(61);require(m->stop(61,std::chrono::milliseconds(0)),"close fixture");Mapping::atomic(m->h->magic).store(legacy_trace::Magic);rejects([&]{decode(*m,61);},"header version changed after mapping");}
}
int main() {
  signal(SIGPIPE,SIG_IGN);
  int passed=0,failed=0;
  auto run=[&](const std::string &name,const std::function<void()> &test) {
    try {test();++passed;std::cout<<"PASS "<<name<<'\n';}
    catch(const std::exception &e){++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}
  };
  for(int point=1;point<=3;++point) {
    run("timeout_boundary_"+std::to_string(point),[=]{timeoutBoundary(point);});
    run("drain_boundary_"+std::to_string(point),[=]{drainBoundary(point);});
    run("dead_writer_boundary_"+std::to_string(point),[=]{deadBoundary(point);});
  }
  run("multiple_admitted_writers",multipleWriters);
  run("overflow_releases_writer",overflow);
  run("generation_and_decode_edges",generationAndDecode);
  run("writer_count_saturation",writerSaturation);
  run("v1_v2_mutual_decoder_rejection",versions);
  run("path_identity_and_lock_controls",pathControls);
  run("binding_replacement_controls",bindingReplacement);
  std::cout<<"RESULT passed="<<passed<<" failed="<<failed<<" live_wine=false\n";
  return failed?1:0;
}
