#pragma once
#include "cs2_trace.hpp"
#include <utility>

namespace dxmt::cs2trace {
// Lexical context only: values are immutable task birth identity + the exact
// submission generation, never a task pointer. Nested/reentrant calls restore it.
struct CompilerWork { uint32_t gen=0; uint64_t serial=0; };
inline thread_local CompilerWork compiler_work;
struct CompilerWorkScope {
  CompilerWork previous=compiler_work;
  CompilerWorkScope(uint32_t gen,uint64_t serial) {
    compiler_work=detailLimit() && profileAllows(105) ? CompilerWork{gen,serial} : CompilerWork{};
  }
  ~CompilerWorkScope(){compiler_work=previous;}
  CompilerWorkScope(const CompilerWorkScope&)=delete;
};
// Uncensored elapsed CPU-steady scopes, not thread CPU/busy time. All records
// share the existing bounded detailed-probe budget and generation fail-closed gate.
struct CompilerStageScope {
  uint32_t gen=0,type,flags=3; uint64_t work=0,operation=0,route,start=0;
  CompilerStageScope(uint32_t event,uint64_t path):type(event),route(path) {
    auto context=compiler_work;
    if(!context.gen || !context.serial || !detailLimit() || !profileAllows(type))return;
    // generation() can publish profile metadata (and read its clock). Peek at
    // the same control word first so stale work creates no scope/metadata work.
    auto header=buffer();if(!header)return;
    auto control=std::atomic_ref<uint64_t>(header->control).load(std::memory_order_relaxed);
    if(controlState(control)!=CaptureState::Open || controlGeneration(control)!=context.gen)return;
    gen=context.gen;work=context.serial;operation=newSerial();start=now();
  }
  ~CompilerStageScope(){if(gen)detailRecord(gen,type,start,now(),work,flags,operation,route);}
  CompilerStageScope(const CompilerStageScope&)=delete;
};
template<class F,class Check> auto compilerStage(uint32_t type,uint64_t route,F &&fn,Check &&success) {
  CompilerStageScope scope(type,route);
  auto value=std::forward<F>(fn)();
  if(scope.gen)scope.flags=std::forward<Check>(success)(value)?1u:2u;
  return value;
}
// Construct before locking, stop immediately after acquisition, publish only
// after the caller's lock guard is destroyed (including exception unwinding).
struct CacheReaderLockScope {
  DetailToken token; uint64_t cache,parent=0,start=0,end=0; uint32_t flags=3;
  CacheReaderLockScope(DetailToken trace,uint64_t identity):token(trace),cache(identity) {
    if(!profileAllows(104) || !token.gen){token.gen=0;return;}
    parent=compiler_work.gen==token.gen?compiler_work.serial:0;
    start=now();
  }
  void acquired(){if(token.gen){end=now();flags=1;}}
  ~CacheReaderLockScope(){if(token.gen)detailRecord(token.gen,104,start,end?end:now(),cache,flags,parent,token.serial);}
  CacheReaderLockScope(const CacheReaderLockScope&)=delete;
};
} // namespace dxmt::cs2trace
