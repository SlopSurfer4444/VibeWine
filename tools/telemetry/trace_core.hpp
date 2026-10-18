#pragma once
#include <atomic>
#include <chrono>
#include <thread>
#include "../../components/dxmt/src/d3d11/cs2_trace_protocol.hpp"
#include <bit>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#if !defined(__APPLE__) || (!defined(__aarch64__) && !defined(__x86_64__))
#error This controller admits only native macOS arm64/x86_64 shared lock-free atomics.
#endif

namespace trace {
using namespace dxmt::cs2trace;
constexpr size_t Bytes = 4096 + Capacity * 64;
constexpr const char *SessionPrefix = "dxmt-trace-session-";
constexpr const char *Marker = "CS2TRACE_SESSION_V2\n";
static_assert(std::endian::native == std::endian::little);
inline void fail(const std::string &s) { throw std::runtime_error(s); }
inline void osfail(const std::string &s) { fail(s + ": " + std::strerror(errno)); }
struct FD {
  int fd = -1;
  FD() = default; explicit FD(int n): fd(n) {}
  ~FD() { if (fd >= 0) close(fd); }
  FD(const FD&) = delete; FD& operator=(const FD&) = delete;
  FD(FD&& x): fd(x.fd) { x.fd = -1; }
  FD& operator=(FD&& x) { if(fd >= 0)close(fd); fd=x.fd; x.fd=-1; return *this; }
};
inline FD openDirectory(const std::string &path) {
  if (path.empty() || path[0] != '/') fail("absolute path required");
  FD dir(open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  if(dir.fd<0)osfail("open root");
  size_t pos=1;
  while(pos<path.size()) {
    size_t end=path.find('/',pos); if(end==std::string::npos)end=path.size();
    std::string part=path.substr(pos,end-pos); pos=end+1;
    if(part.empty())continue;
    if(part=="." || part=="..")fail("dot path components forbidden");
    FD next(openat(dir.fd,part.c_str(),O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if(next.fd<0)osfail("open directory component");
    dir=std::move(next);
  }
  return dir;
}
inline std::string identity(const struct stat &s) { return std::to_string(uint64_t(s.st_dev))+":"+std::to_string(uint64_t(s.st_ino)); }
inline void exactRegular(const struct stat &s, size_t bytes) {
  if(!S_ISREG(s.st_mode))fail("not a regular file");
  if(s.st_size<0 || uint64_t(s.st_size)!=bytes)fail("wrong file length");
}
inline std::string newSession(const std::string &root) {
  auto dir=openDirectory(root);
  unsigned char random[16]; arc4random_buf(random,sizeof(random));
  const char *hex="0123456789abcdef"; std::string name=SessionPrefix;
  for(auto c:random){name+=hex[c>>4];name+=hex[c&15];}
  if(mkdirat(dir.fd,name.c_str(),0700)<0)osfail("create unique session");
  FD session(openat(dir.fd,name.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));
  if(session.fd<0)osfail("open new session");
  FD marker(openat(session.fd,".cs2trace-session",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600));
  if(marker.fd<0)osfail("create session marker");
  if(write(marker.fd,Marker,std::strlen(Marker))!=ssize_t(std::strlen(Marker)))osfail("write marker");
  if(fsync(marker.fd)<0)osfail("sync marker");
  return root+(root.back()=='/'?"":"/")+name;
}
struct Mapping {
  std::string path, parentPath, filename, binding, directoryBinding, markerBinding;
  FD directory, marker, file;
  Header *h=nullptr; Event *events=nullptr;
  struct stat original{};
  bool writable;
  Mapping(const std::string &p, bool writeAccess, const std::string &expected=""):path(p),writable(writeAccess) {
    size_t slash=p.rfind('/');
    if(slash==std::string::npos || slash==0 || slash+1==p.size())fail("absolute file path in unique session required");
    filename=p.substr(slash+1);
    if(filename=="." || filename=="..")fail("invalid filename");
    std::string parent=p.substr(0,slash), leaf=parent.substr(parent.rfind('/')+1);parentPath=parent;
    if(leaf.rfind(SessionPrefix,0)!=0 || leaf.size()!=std::strlen(SessionPrefix)+32)fail("new-session directory required");
    for(size_t i=std::strlen(SessionPrefix);i<leaf.size();++i)if(!((leaf[i]>='0'&&leaf[i]<='9')||(leaf[i]>='a'&&leaf[i]<='f')))fail("invalid session name");
    directory=openDirectory(parent);
    struct stat ds{}; if(fstat(directory.fd,&ds)<0)osfail("stat session");
    if(ds.st_uid!=geteuid() || (ds.st_mode & 077)!=0)fail("session must be owner-private");
    directoryBinding=identity(ds);
    marker=FD(openat(directory.fd,".cs2trace-session",O_RDWR|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK));
    if(marker.fd<0)osfail("open session marker");
    struct stat ms{};if(fstat(marker.fd,&ms)<0)osfail("stat marker");exactRegular(ms,std::strlen(Marker));
    if(ms.st_uid!=geteuid() || (ms.st_mode&077)!=0 || ms.st_nlink!=1)fail("unsafe session marker");
    markerBinding=identity(ms);
    if(flock(marker.fd,(writeAccess?LOCK_EX:LOCK_SH)|LOCK_NB)<0)osfail("another controller owns session");
    char buf[64]{};if(pread(marker.fd,buf,std::strlen(Marker),0)!=ssize_t(std::strlen(Marker)) || std::string(buf,std::strlen(Marker))!=Marker)fail("invalid session marker");
    file=FD(openat(directory.fd,filename.c_str(),(writeAccess?O_RDWR:O_RDONLY)|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK));
    if(file.fd<0)osfail("open trace");
    if(fstat(file.fd,&original)<0)osfail("stat trace");exactRegular(original,Bytes);
    if(original.st_uid!=geteuid() || original.st_nlink!=1)fail("trace must be owned by current user without hard links");
    binding=identity(original);
    if(!expected.empty() && binding!=expected)fail("inode identity mismatch");
    if(writeAccess && expected.empty())fail("arm/stop require explicit identity from inspect");
    void *mapped=mmap(nullptr,Bytes,PROT_READ|(writeAccess?PROT_WRITE:0),MAP_SHARED,file.fd,0);
    if(mapped==MAP_FAILED)osfail("map trace");
    h=static_cast<Header*>(mapped);events=reinterpret_cast<Event*>(static_cast<char*>(mapped)+4096);
    try { validate(); verifyBinding(); } catch (...) { munmap(h,Bytes);h=nullptr;throw; }
  }
  ~Mapping(){if(h)munmap(h,Bytes);}
  Mapping(const Mapping&)=delete;
  void verifyBinding() const {
    auto pathDirectory=openDirectory(parentPath);struct stat ds{},ms{};
    if(fstat(pathDirectory.fd,&ds)<0 || fstatat(directory.fd,".cs2trace-session",&ms,AT_SYMLINK_NOFOLLOW)<0)osfail("revalidate session");
    if(identity(ds)!=directoryBinding || identity(ms)!=markerBinding)fail("session or lock marker replaced during operation");
    exactRegular(ms,std::strlen(Marker));
    struct stat current{}, entry{};
    if(fstat(file.fd,&current)<0 || fstatat(directory.fd,filename.c_str(),&entry,AT_SYMLINK_NOFOLLOW)<0)osfail("revalidate trace");
    exactRegular(current,Bytes);exactRegular(entry,Bytes);
    if(identity(current)!=binding || identity(entry)!=binding)fail("trace replaced during operation");
  }
  template<class T> static std::atomic_ref<T> atomic(T &v) {
    if(reinterpret_cast<uintptr_t>(&v)%std::atomic_ref<T>::required_alignment)fail("atomic field alignment unsupported");
    std::atomic_ref<T> a(v);if(!a.is_lock_free())fail("shared atomics not lock-free");return a;
  }
  void validate() {
    if(atomic(h->magic).load(std::memory_order_acquire)!=Magic)fail("wrong/unpublished magic");
    if(h->version!=Version || h->capacity!=Capacity)fail("unsupported header version/capacity");
    (void)atomic(h->control);(void)atomic(h->next);(void)atomic(h->dropped);(void)atomic(events[0].committed);
  }
  uint64_t control() {return atomic(h->control).load(std::memory_order_acquire);}
  uint32_t capture() {auto word=control();return controlState(word)==CaptureState::Open?controlGeneration(word):0;}
  uint64_t next() {return atomic(h->next).load(std::memory_order_relaxed);}
  static uint64_t remainingAfter(uint64_t reserved) {return reserved>=Capacity?0:Capacity-reserved;}
  uint64_t remaining() {return remainingAfter(next());}
  uint64_t dropped() {return atomic(h->dropped).load(std::memory_order_relaxed);}
  bool reusedCommitted(uint32_t generation) {
    for(uint64_t i=0;i<Capacity;++i) {
      auto commit=atomic(events[i].committed).load(std::memory_order_acquire);
      if(commit && commit!=i+1)fail("malformed committed marker");
      if(commit==i+1 && events[i].reserved==generation)return true;
    }
    return false;
  }
  void reserveGeneration(uint32_t generation) {
    std::string name=".generations-"+binding;
    FD ledger(openat(directory.fd,name.c_str(),O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK,0600));
    if(ledger.fd<0)osfail("open generation ledger");
    struct stat st{};if(fstat(ledger.fd,&st)<0)osfail("stat ledger");
    if(!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||st.st_nlink!=1||(st.st_mode&077)!=0||st.st_size<0||st.st_size>1048576)fail("invalid generation ledger");
    if(st.st_size>1048576-11)fail("generation ledger full");
    std::string contents(size_t(st.st_size),'\0');
    if(pread(ledger.fd,contents.data(),contents.size(),0)!=ssize_t(contents.size()))osfail("read ledger");
    size_t pos=0;
    while(pos<contents.size()) {
      size_t end=contents.find('\n',pos);if(end==std::string::npos||end==pos)fail("corrupt generation ledger");
      uint64_t value=0;for(size_t n=pos;n<end;++n){char c=contents[n];if(c<'0'||c>'9')fail("corrupt generation ledger");value=value*10+(c-'0');if(value>UINT32_MAX)fail("corrupt generation ledger");}
      if(!value)fail("corrupt generation ledger");
      if(value==generation)fail("generation already reserved by this controller");pos=end+1;
    }
    std::string line=std::to_string(generation)+"\n";
    if(pwrite(ledger.fd,line.data(),line.size(),st.st_size)!=ssize_t(line.size()))osfail("reserve generation");
    if(fsync(ledger.fd)<0)osfail("sync generation ledger");
  }
  uint64_t arm(uint32_t generation,uint64_t plannedRecords,uint64_t tailReserve) {
    if(!writable||!generation)fail("arm requires writable mapping and nonzero generation");
    verifyBinding();validate();
    if(controlState(control())!=CaptureState::Closed || controlWriters(control()))fail("capture not closed/drained");
    const auto available=remaining();
    // The ring is process-lifetime: admission never resets published records.
    // Subtraction also rejects overflowing planned + reserve without wrapping.
    if(!plannedRecords)fail("planned record budget must be nonzero");
    if(plannedRecords>available || tailReserve>available-plannedRecords)
      fail("insufficient remaining capacity for planned records and tail reserve");
    if(reusedCommitted(generation))fail("generation already present in committed events");
    reserveGeneration(generation); // A failed CAS deliberately consumes this generation too.
    verifyBinding();auto expected=control();
    if(controlState(expected)!=CaptureState::Closed || controlWriters(expected))fail("arm not drained");
    if(!atomic(h->control).compare_exchange_strong(expected,controlWord(generation,CaptureState::Open),std::memory_order_acq_rel))fail("arm CAS collision");
    verifyBinding();
    return available;
  }
  bool stop(uint32_t generation,std::chrono::milliseconds budget=std::chrono::milliseconds(250)) {
    if(!writable||!generation)fail("stop requires writable mapping and nonzero generation");
    verifyBinding();validate();auto word=control();
    for(;;) {
      if(controlGeneration(word)!=generation || controlState(word)!=CaptureState::Open)fail("stop CAS generation mismatch");
      auto desired=controlWord(generation,controlWriters(word)?CaptureState::Closing:CaptureState::Closed,controlWriters(word));
      if(atomic(h->control).compare_exchange_weak(word,desired,std::memory_order_acq_rel))break;
    }
    const auto deadline=std::chrono::steady_clock::now()+budget;
    while(controlState(control())==CaptureState::Closing && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    word=control();verifyBinding();
    return controlGeneration(word)==generation && controlState(word)==CaptureState::Closed && controlWriters(word)==0;
  }

};
struct Decoded { uint64_t index,start,end,object,padding0,padding1;uint32_t type,thread,flags,generation; };
struct Snapshot {
  uint64_t nextBefore=0,nextAfter=0,droppedBefore=0,droppedAfter=0,committed=0,wrongGeneration=0,uncommitted=0;
  uint64_t controlBefore=0,controlAfter=0,boundaryRejected=0;
  bool countersChanged=false,inconclusive=false,structurallyComplete=false;
  std::vector<Decoded> events;
};
inline Snapshot decode(Mapping &m,uint32_t generation) {
  if(!generation)fail("decode requires nonzero generation");
  m.verifyBinding();m.validate();if(controlState(m.control())==CaptureState::Open)fail("stop capture before decode");
  if(controlGeneration(m.control())!=generation)fail("decode requires current stopped generation");
  Snapshot s;s.controlBefore=m.control();s.nextBefore=m.next();s.droppedBefore=m.dropped();
  for(uint64_t i=0;i<Capacity;++i) {
    Event &e=m.events[i];uint64_t commit=Mapping::atomic(e.committed).load(std::memory_order_acquire);
    if(commit==0)continue;
    if(commit!=i+1)fail("malformed committed marker");
    ++s.committed;
    if(e.reserved!=generation){++s.wrongGeneration;continue;}
    Decoded d{i,e.start,e.end,e.object,e.padding[0],e.padding[1],e.type,e.thread,e.flags,e.reserved};
    if(Mapping::atomic(e.committed).load(std::memory_order_acquire)!=commit)fail("published event changed; producer reset is unsupported");
    s.events.push_back(d);
  }
  s.nextAfter=m.next();s.droppedAfter=m.dropped();s.controlAfter=m.control();
  s.boundaryRejected=Mapping::atomic(m.h->boundaryRejected).load(std::memory_order_relaxed);
  uint64_t reserved=std::min(Capacity,std::max(s.nextBefore,s.nextAfter));
  s.uncommitted=reserved>s.committed?reserved-s.committed:0;
  s.countersChanged=s.nextBefore!=s.nextAfter||s.droppedBefore!=s.droppedAfter;
  s.structurallyComplete=controlState(s.controlBefore)==CaptureState::Closed && controlState(s.controlAfter)==CaptureState::Closed && !controlWriters(s.controlBefore) && !controlWriters(s.controlAfter);
  s.inconclusive=!s.structurallyComplete || s.uncommitted || s.countersChanged || s.droppedBefore || s.droppedAfter || s.nextBefore>Capacity || s.nextAfter>Capacity || s.committed>reserved;
  if(controlGeneration(s.controlAfter)!=generation || controlState(s.controlAfter)==CaptureState::Open)fail("capture rearmed during decode");m.verifyBinding();return s;
}
} // namespace trace
