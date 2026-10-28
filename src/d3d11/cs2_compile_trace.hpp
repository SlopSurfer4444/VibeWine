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
// Combined-only CPU steady elapsed scopes, including eager helper requests.
// No clock, identity allocation, mapping or generation lookup in old profiles or
// with detail disabled. Owner is an immutable birth serial, never an address.
// Route is schema-defined, not a native object or a cross-clock calibration.
enum class HelperRoute : uint64_t {
  TessellationLock=1, TessellationNativePSO=2, ClearUAVRequest=3,
  TileNativePSO=4, RTVNativePSO=5
};
inline bool helperTraceEnabled() {
  return profile()==Profile::CombinedBounded && detailLimit() && configured();
}
struct HelperCompileScope {
  DetailToken token; uint64_t route,start=0; uint32_t flags=3;
  HelperCompileScope(HelperRoute path,uint64_t owner=0):route(uint64_t(path)) {
    if(!helperTraceEnabled())return;
    token=beginDetail(owner);if(token.gen)start=now();
  }
  void result(bool success) {flags=success?1u:2u;}
  ~HelperCompileScope(){
    if(token.gen)detailRecord(token.gen,120,start,now(),token.serial,flags,route,token.owner);
  }
  HelperCompileScope(const HelperCompileScope&)=delete;
};
template<class F,class Check> auto helperCompile(HelperRoute route,uint64_t owner,F &&fn,Check &&success) {
  HelperCompileScope scope(route,owner);
  auto value=std::forward<F>(fn)();
  if(scope.token.gen)scope.result(std::forward<Check>(success)(value));
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
