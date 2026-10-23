#pragma once
#include <atomic>
#include <cstdint>
#include <cstddef>
namespace dxmt::cs2trace {
// Changed magic prevents version-1 controllers mutating this control word.
constexpr uint64_t Magic=0x4353325452414332ULL, Version=2, Capacity=131072;
constexpr uint64_t WriterMask=(1ULL<<30)-1, StateMask=3ULL<<30;
enum class CaptureState : uint64_t { Closed=0, Open=1ULL<<30, Closing=2ULL<<30 };
constexpr uint64_t controlWord(uint32_t gen,CaptureState state,uint64_t writers=0) { return (uint64_t(gen)<<32)|uint64_t(state)|writers; }
constexpr uint32_t controlGeneration(uint64_t word) { return uint32_t(word>>32); }
constexpr uint64_t controlWriters(uint64_t word) { return word&WriterMask; }
constexpr CaptureState controlState(uint64_t word) { return CaptureState(word&StateMask); }
struct Header { uint64_t magic,version,capacity,next,control,dropped,boundaryRejected; };
struct Event { uint64_t start,end,object; uint32_t type,thread,flags,reserved; uint64_t padding[2]; uint64_t committed; };
static_assert(sizeof(Header)==56 && sizeof(Event)==64 && offsetof(Header,control)==32 && offsetof(Event,committed)==56);
static_assert(std::atomic_ref<uint64_t>::is_always_lock_free);
// Additive detailed-probe admission, independent of the v2 wire layout. At most
// limit budgeted records plus one cap-failure marker per generation (stale-work
// markers consume the ordinary budget). Caller holds
// capture writer admission while claiming (generations need not increase).
// No allocation, timer, or background
// producer is introduced by this gate.
class DetailBudget {
  std::atomic<uint64_t> state_{0};
public:
  enum class Result { disabled, accepted, exhausted_first, exhausted, stale };
  static constexpr uint32_t MaxRecords = 32768;
  Result claim(uint32_t gen, uint32_t limit) {
    if (!gen || !limit || limit > MaxRecords) return Result::disabled;
    auto old = state_.load(std::memory_order_relaxed);
    for (;;) {
      const auto prior = uint32_t(old >> 32), used = uint32_t(old);
      const auto count = prior == gen ? used : 0;
      if (count > limit) return Result::exhausted;
      const auto next = (uint64_t(gen) << 32) | (count + 1);
      if (state_.compare_exchange_weak(old, next, std::memory_order_relaxed))
        return count == limit ? Result::exhausted_first : Result::accepted;
    }
  }
};
inline bool admit(Header *h,uint32_t gen) {
 auto a=std::atomic_ref<uint64_t>(h->control);auto word=a.load(std::memory_order_relaxed);
 for(;;) {
  if(controlGeneration(word)!=gen || controlState(word)!=CaptureState::Open)return false;
  if(controlWriters(word)==WriterMask)return false;
  if(a.compare_exchange_weak(word,word+1,std::memory_order_acquire,std::memory_order_relaxed))return true;
 }
}
inline void releaseWriter(Header *h) {
 auto a=std::atomic_ref<uint64_t>(h->control);auto word=a.load(std::memory_order_relaxed);
 for(;;) {
  auto desired=word-1;
  if(controlState(word)==CaptureState::Closing && controlWriters(word)==1)desired=controlWord(controlGeneration(word),CaptureState::Closed);
  if(a.compare_exchange_weak(word,desired,std::memory_order_release,std::memory_order_relaxed))return;
 }
}
#ifndef DXMT_TRACE_WRITER_BARRIER
#define DXMT_TRACE_WRITER_BARRIER(point) ((void)0)
#endif
// Shared by the real Windows producer and native barrier fixture; no extracted imitation.
inline void writeAdmittedEvent(Header *h,uint32_t gen,uint32_t type,uint64_t start,uint64_t end,uint64_t object,uint32_t flags,uint64_t detail0,uint64_t detail1,uint32_t thread) {
 // Caller owns writer admission for this generation until publication returns.
 DXMT_TRACE_WRITER_BARRIER(1); // admitted, before reservation
 auto index=std::atomic_ref<uint64_t>(h->next).fetch_add(1,std::memory_order_relaxed);
 DXMT_TRACE_WRITER_BARRIER(2); // reserved
 if(index>=h->capacity){std::atomic_ref<uint64_t>(h->dropped).fetch_add(1,std::memory_order_relaxed);return;}
 auto e=reinterpret_cast<Event*>(reinterpret_cast<char*>(h)+4096)+index;
 e->start=start;e->end=end;e->object=object;e->type=type;e->thread=thread;e->flags=flags;e->reserved=gen;e->padding[0]=detail0;e->padding[1]=detail1;
 DXMT_TRACE_WRITER_BARRIER(3); // payload filled, before publication
 std::atomic_ref<uint64_t>(e->committed).store(index+1,std::memory_order_release);
}
inline void writeEvent(Header *h,uint32_t gen,uint32_t type,uint64_t start,uint64_t end,uint64_t object,uint32_t flags,uint64_t detail0,uint64_t detail1,uint32_t thread) {
 if(!h || !gen)return;
 if(!admit(h,gen)) {
  if(controlGeneration(std::atomic_ref<uint64_t>(h->control).load(std::memory_order_relaxed))==gen)
   std::atomic_ref<uint64_t>(h->boundaryRejected).fetch_add(1,std::memory_order_relaxed);
  return;
 }
 struct Exit {Header *h;~Exit(){releaseWriter(h);}} exit{h};
 writeAdmittedEvent(h,gen,type,start,end,object,flags,detail0,detail1,thread);
}
} // namespace dxmt::cs2trace
