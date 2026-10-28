/* SPDX-License-Identifier: MIT */
#include "airconv_worker_bridge.h"
#include "airconv_worker_protocol.hpp"
#include "airconv_worker_recipe.hpp"
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>
using namespace dxmt::worker;
namespace {
constexpr size_t MaxOwnedDXBC=64*1024*1024, MaxOwnedAIR=64*1024*1024;
constexpr uint32_t RequestDeadlineMs=10000;
struct Source {WireBytes dxbc,reflection;};
struct RemoteBitcode {WireBytes air;};
struct RemoteError {std::string text;};
const char errorLimitMarker=0;
constexpr const char* ErrorLimitText="native worker retained error cap";
constexpr size_t MaxErrors=1024;
struct Registry {
  std::mutex lock;
  std::unordered_map<void*,std::shared_ptr<Source>> sources;
  std::unordered_map<void*,std::unique_ptr<RemoteBitcode>> bitcodes;
  std::unordered_map<void*,std::unique_ptr<RemoteError>> errors;
  size_t sourceBytes=0,airBytes=0;
};
Registry& registry(){static Registry r;return r;}
struct Transport {
  std::mutex lock;
  std::atomic<uint64_t> cancel{0};
  std::atomic<bool> stopped{false};
  pid_t child=-1;int input=-1,output=-1;uint64_t generation=0,serial=0;uint32_t requests=0;
  std::atomic<uint64_t> successes{0};
  std::chrono::steady_clock::time_point spawned;
  void stop() {
    if(input>=0)close(input);if(output>=0)close(output);input=output=-1;
    if(child>0){kill(child,SIGKILL);while(waitpid(child,nullptr,0)<0 && errno==EINTR){}child=-1;}
    requests=0;
  }
  ~Transport(){stopped.store(true);cancel.fetch_add(1);std::lock_guard guard(lock);stop();}
};
Transport& transport(){static Transport t;return t;}
const std::string& workerPath() {
  static const std::string path=[] {const char* p=getenv("DXMT_AIRCONV_WORKER");return p&&p[0]=='/'&&strlen(p)<4096?std::string(p):std::string();}();return path;
}
bool enabled(){return configured()&&!workerPath().empty()&&!transport().stopped.load();}
int fail(sm50_bitcode_t* bitcode,sm50_error_t* error,const std::string& message) {
  if(bitcode)*bitcode=nullptr;
  if(error) {
    auto& r=registry();std::lock_guard guard(r.lock);
    if(r.errors.size()>=MaxErrors)*error=const_cast<char*>(&errorLimitMarker);
    else {auto p=std::make_unique<RemoteError>();p->text=message.substr(0,MaxDiagnostic);auto key=p.get();r.errors.emplace(key,std::move(p));*error=key;}
  }
  return 1;
}
bool spawn(Transport& t) {
#ifdef POSIX_SPAWN_CLOEXEC_DEFAULT
  int in[2]{-1,-1},out[2]{-1,-1};
  if(pipe(in)||pipe(out)){for(auto fd:{in[0],in[1],out[0],out[1]})if(fd>=0)close(fd);return false;}
  bool ok=true;
  for(auto fd:{in[0],in[1],out[0],out[1]})if(fcntl(fd,F_SETFD,FD_CLOEXEC)<0)ok=false;
  if(fcntl(in[1],F_SETFL,O_NONBLOCK)<0||fcntl(out[0],F_SETFL,O_NONBLOCK)<0||fcntl(in[1],F_SETNOSIGPIPE,1)<0)ok=false;
  posix_spawn_file_actions_t actions;posix_spawnattr_t attr;
  bool haveActions=posix_spawn_file_actions_init(&actions)==0,haveAttr=posix_spawnattr_init(&attr)==0;
  ok=ok&&haveActions&&haveAttr;
  if(ok)ok=posix_spawn_file_actions_adddup2(&actions,in[0],STDIN_FILENO)==0 &&
      posix_spawn_file_actions_adddup2(&actions,out[1],STDOUT_FILENO)==0 &&
      posix_spawn_file_actions_addopen(&actions,STDERR_FILENO,"/dev/null",O_WRONLY,0)==0;
  sigset_t empty,defaults;sigemptyset(&empty);sigemptyset(&defaults);
  for(int signal:{SIGPIPE,SIGTERM,SIGINT,SIGQUIT,SIGHUP})sigaddset(&defaults,signal);
  if(ok)ok=posix_spawnattr_setflags(&attr,POSIX_SPAWN_CLOEXEC_DEFAULT|POSIX_SPAWN_SETSIGMASK|POSIX_SPAWN_SETSIGDEF)==0 &&
      posix_spawnattr_setsigmask(&attr,&empty)==0 && posix_spawnattr_setsigdefault(&attr,&defaults)==0;
  char pathEnv[]="PATH=/usr/bin:/bin",langEnv[]="LANG=C";
  char* env[]={pathEnv,langEnv,nullptr};char* argv[]={const_cast<char*>(workerPath().c_str()),nullptr};pid_t pid=-1;
  if(ok)ok=posix_spawn(&pid,workerPath().c_str(),&actions,&attr,argv,env)==0;
  if(haveActions)posix_spawn_file_actions_destroy(&actions);if(haveAttr)posix_spawnattr_destroy(&attr);
  close(in[0]);close(out[1]);
  if(!ok){close(in[1]);close(out[0]);return false;}
  t.child=pid;t.input=in[1];t.output=out[0];t.requests=0;++t.generation;t.spawned=std::chrono::steady_clock::now();return true;
#else
  return false; // Never fall back to fork in the multithreaded host.
#endif
}
// An admitted exchange owns the only child. Failures after admission never replay
// potentially hostile/crashing requests in the host. A future request may respawn.
bool exchange(Transport& t,const WireBytes& request,WireBytes& response,uint64_t cancellation,std::string& why) {
  auto start=std::chrono::steady_clock::now();size_t sent=0,expected=HeaderSize;
  while(true) {
    if(t.cancel.load()!=cancellation||t.stopped.load()){why="native worker cancelled";return false;}
    auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
    if(elapsed>=RequestDeadlineMs){why="native worker deadline";return false;}
    pollfd fd[2]={{sent<request.size()?t.input:-1,POLLOUT,0},{t.output,POLLIN,0}};
    auto result=poll(fd,2,std::min<int64_t>(20,RequestDeadlineMs-elapsed));
    if(result<0){if(errno==EINTR)continue;why="native worker poll";return false;}
    if(sent<request.size() && (fd[0].revents&(POLLOUT|POLLERR|POLLHUP))) {
      auto n=write(t.input,request.data()+sent,request.size()-sent);
      if(n>0)sent+=n;else if(n<0 && errno!=EINTR && errno!=EAGAIN){why="native worker request pipe";return false;}
    }
    if(fd[1].revents&(POLLIN|POLLERR|POLLHUP)) {
      uint8_t buffer[8192];auto n=read(t.output,buffer,std::min(sizeof buffer,expected-response.size()));
      if(n==0){why="native worker EOF";return false;}
      if(n<0){if(errno==EINTR||errno==EAGAIN)continue;why="native worker response pipe";return false;}
      response.insert(response.end(),buffer,buffer+n);
      if(response.size()==HeaderSize) {
        if(!validHeader(response.data(),true)){why="native worker response protocol";return false;}
        auto a=get32(response.data()+32),r=get32(response.data()+36),d=get32(response.data()+40);
        if(a>MaxAIR||r>MaxReflection||d>MaxDiagnostic){why="native worker response cap";return false;}
        expected=HeaderSize+size_t(a)+r+d;
      }
      if(response.size()==expected) {
        if(sent!=request.size()){why="native worker premature response";return false;}
        // Unsolicited output cannot become the next request's result.
        uint8_t extra;auto n=read(t.output,&extra,1);
        if(n>0){why="native worker trailing response";return false;}
        return true;
      }
    }
  }
}
}
extern "C" int WMTWorkerInitialize(const void* data,size_t size,sm50_shader_t* shader,MTL_SHADER_REFLECTION* reflection,sm50_error_t* error) {
  if(!enabled())return SM50Initialize(data,size,shader,reflection,error);
  MTL_SHADER_REFLECTION clean{};
  int result=SM50Initialize(data,size,shader,reflection?&clean:nullptr,error);
  if(reflection)*reflection=clean;
  if(result||!shader||!*shader||!reflection||!enabled()||size>MaxDXBC||clean.NumConstantBuffers>2048||clean.NumArguments>2048)return result;
  auto source=std::make_shared<Source>();auto* bytes=static_cast<const uint8_t*>(data);source->dxbc.assign(bytes,bytes+size);
  if(!ordinaryDXBC(source->dxbc))return result;
  std::vector<MTL_SM50_SHADER_ARGUMENT> cb(clean.NumConstantBuffers),args(clean.NumArguments);
  MTL_SM50_SHADER_ARGUMENT dummy{};SM50GetArgumentsInfo(*shader,cb.empty()?&dummy:cb.data(),args.empty()?&dummy:args.data());
  source->reflection=serializeReflection(clean,cb,args);
  if(source->reflection.size()>MaxReflection)return result;
  size_t retained=source->dxbc.size()+source->reflection.size();
  auto& r=registry();std::lock_guard guard(r.lock);
  if(r.sourceBytes+retained<=MaxOwnedDXBC && r.sources.size()<65536){r.sourceBytes+=retained;r.sources.emplace(*shader,std::move(source));}
  return result;
}
extern "C" void WMTWorkerDestroy(sm50_shader_t shader) {
  auto& r=registry();{std::lock_guard guard(r.lock);auto i=r.sources.find(shader);if(i!=r.sources.end()){r.sourceBytes-=i->second->dxbc.size()+i->second->reflection.size();r.sources.erase(i);}}
  SM50Destroy(shader);
}
extern "C" int WMTWorkerCompile(sm50_shader_t shader,SM50_SHADER_COMPILATION_ARGUMENT_DATA* args,const char* name,sm50_bitcode_t* bitcode,sm50_error_t* error) {
  if(error)*error=nullptr;
  if(transport().stopped.load())return fail(bitcode,error,"native worker shutdown");
  auto local=[&]{return SM50Compile(shader,args,name,bitcode,error);};
  if(!bitcode||!enabled())return local();
  std::shared_ptr<Source> source;
  {auto& r=registry();std::lock_guard guard(r.lock);auto it=r.sources.find(shader);if(it!=r.sources.end())source=it->second;}
  if(!source)return local();
  WireBytes recipe;std::string why;
  if(!encodeRecipe(args,name,recipe,why)||recipe.size()>MaxRecipe)
    return fail(bitcode,error,"native worker recipe rejected: "+why);
  auto& t=transport();std::unique_lock guard(t.lock,std::try_to_lock);
  if(!guard.owns_lock())return t.stopped.load()?fail(bitcode,error,"native worker shutdown"):local(); // Reuse existing compile-owner concurrency; no hidden queue.
  if(t.stopped.load())return fail(bitcode,error,"native worker shutdown");
  if(t.child>0) {
    auto age=std::chrono::steady_clock::now()-t.spawned;
    auto state=waitpid(t.child,nullptr,WNOHANG);
    if(state==t.child){t.child=-1;t.stop();}
    else if(state<0 && errno==ECHILD){t.child=-1;t.stop();}
    else if(t.requests>=MaxRequests||age>std::chrono::minutes(5))t.stop();
  }
  if(t.child<0&&!spawn(t))return local(); // Definite pre-admission failure only.
  auto id=++t.serial,gen=t.generation,cancellation=t.cancel.load();
  auto request=header(false,1,gen,id,source->dxbc.size(),recipe.size(),IdentitySize);
  const char* identity=DXMT_WORKER_BUILD_ID;
  request.insert(request.end(),identity,identity+IdentitySize);
  request.insert(request.end(),source->dxbc.begin(),source->dxbc.end());request.insert(request.end(),recipe.begin(),recipe.end());
  WireBytes response;++t.requests;
  if(!exchange(t,request,response,cancellation,why)){t.stop();return fail(bitcode,error,why);}
  auto* p=response.data();auto status=get32(p+12),a=get32(p+32),r=get32(p+36),d=get32(p+40);
  if(get64(p+16)!=gen||get64(p+24)!=id||status>3||(status&&(a||r))||(!status&&(!a||!r))||t.cancel.load()!=cancellation||t.stopped.load()) {
    t.stop();return fail(bitcode,error,"native worker response identity/status");
  }
  if(status)return fail(bitcode,error,std::string(reinterpret_cast<char*>(p+HeaderSize+a+r),d));
  if(a<16||memcmp(p+HeaderSize,"MTLB",4)||r!=source->reflection.size()||memcmp(p+HeaderSize+a,source->reflection.data(),r)) {
    t.stop();return fail(bitcode,error,"native worker AIR/reflection mismatch");
  }
  auto value=std::make_unique<RemoteBitcode>();value->air.assign(p+HeaderSize,p+HeaderSize+a);auto key=value.get();
  auto& registryRef=registry();std::unique_lock registryGuard(registryRef.lock);
  if(registryRef.airBytes+a>MaxOwnedAIR){registryGuard.unlock();return fail(bitcode,error,"native worker retained AIR cap");}
  registryRef.airBytes+=a;registryRef.bitcodes.emplace(key,std::move(value));*bitcode=key;t.successes.fetch_add(1);return 0;
}
extern "C" void WMTWorkerGetBitcode(sm50_bitcode_t bitcode,SM50_COMPILED_BITCODE* output) {
  auto& r=registry();{std::lock_guard guard(r.lock);auto i=r.bitcodes.find(bitcode);if(i!=r.bitcodes.end()){output->Data=i->second->air.data();output->Size=i->second->air.size();return;}}
  SM50GetCompiledBitcode(bitcode,output);
}
extern "C" void WMTWorkerDestroyBitcode(sm50_bitcode_t bitcode) {
  auto& r=registry();{std::lock_guard guard(r.lock);auto i=r.bitcodes.find(bitcode);if(i!=r.bitcodes.end()){r.airBytes-=i->second->air.size();r.bitcodes.erase(i);return;}}
  SM50DestroyBitcode(bitcode);
}
extern "C" size_t WMTWorkerGetError(sm50_error_t error,char* buffer,size_t size) {
  if(!buffer||!size)return 0;
  if(error==&errorLimitMarker){auto n=std::min(size-1,strlen(ErrorLimitText));memcpy(buffer,ErrorLimitText,n);buffer[n]=0;return n;}
  auto& r=registry();{std::lock_guard guard(r.lock);auto i=r.errors.find(error);if(i!=r.errors.end()){auto n=std::min(size-1,i->second->text.size());memcpy(buffer,i->second->text.data(),n);buffer[n]=0;return n;}}
  return SM50GetErrorMessage(error,buffer,size);
}
extern "C" void WMTWorkerFreeError(sm50_error_t error) {
  if(error==&errorLimitMarker)return;
  auto& r=registry();{std::lock_guard guard(r.lock);if(r.errors.erase(error))return;}
  SM50FreeError(error);
}
extern "C" void WMTWorkerShutdown() {
  auto& t=transport();t.stopped.store(true);t.cancel.fetch_add(1);std::lock_guard guard(t.lock);t.stop();
}

#ifdef DXMT_WORKER_TESTING
extern "C" uint64_t WMTWorkerTestSuccesses(){return transport().successes.load();}
extern "C" uint64_t WMTWorkerTestGeneration(){auto& t=transport();std::lock_guard guard(t.lock);return t.generation;}
extern "C" int WMTWorkerTestPID(){auto& t=transport();std::lock_guard guard(t.lock);return t.child;}
#endif
