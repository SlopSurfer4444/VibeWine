// Production policy and verbatim queue admission method; native APIs are doubles.
#include <cassert>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <vector>
#include "util_flags.hpp"
#include "rc/util_rc_ptr.hpp"
#include "dxmt_residency_set_policy.hpp"
static_assert(DXMT_RESIDENCY_SETS == TEST_EXPECT_ENABLED);

struct NativeState { uint64_t bytes; unsigned size_reads = 0; };
static std::map<uint64_t, std::shared_ptr<NativeState>> native;
static std::set<uint64_t> members;
static std::vector<char> operations;
static unsigned pending = 0;
static uint64_t os_major = 15;
static unsigned os_reads=0, creations=0, attachments=0;
static bool creation_succeeds=true;
static unsigned capture_stops=0;
[[maybe_unused]] static void WMTGetOSVersion(uint64_t *major, uint64_t *minor, uint64_t *patch) {
  ++os_reads; *major=os_major; *minor=0; *patch=0;
}
#define WARN(...) ((void)0)
enum { WMTCommandBufferStatusScheduled = 3, WMTCommandBufferStatusCompleted = 4 };
namespace WMT {
struct Allocation {
  uint64_t handle = 0;
  uint64_t allocatedSize() const { ++native.at(handle)->size_reads; return native.at(handle)->bytes; }
};
struct Buffer : Allocation {};
template<class T> struct Reference : T {
  std::shared_ptr<NativeState> ref;
  Reference() = default;
  Reference(T value) : T(value), ref(native.at(value.handle)) {}
};
struct ResidencySet {
  bool valid = true;
  explicit operator bool() const { return valid; }
  void addAllocations(const Allocation *a, uint64_t count) {
    assert(count == 1); assert(members.insert(a->handle).second); operations.push_back('a');
  }
  void removeAllocations(const Allocation *a, uint64_t count) {
    assert(pending == 0); assert(count == 1); assert(members.erase(a->handle) == 1); operations.push_back('r');
  }
  void commit() { operations.push_back('c'); }
};
struct Error {};
struct Device {
  ResidencySet newResidencySet(uint64_t count, Reference<Error>&) { assert(count==256); ++creations; return {creation_succeeds}; }
};
struct CommandQueue {
  void addResidencySet(ResidencySet set) { assert(set.valid); ++attachments; }
};
struct CaptureManager {
  static CaptureManager sharedCaptureManager() { return {}; }
  void stopCapture() { ++capture_stops; }
};
}
static WMT::Buffer buffer(uint64_t id, uint64_t bytes) {
  native[id] = std::make_shared<NativeState>(NativeState{bytes}); return {{id}};
}

namespace dxmt {
enum class BufferAllocationFlag : uint32_t {
  GpuReadonly = 0, NoTracking = 0, GpuPrivate = 1, CpuWriteCombined = 2,
  OwnedByCommandList = 3, GpuManaged = 4, SuballocateFromOnePage = 5,
  CpuPlaced = 6, AllocatedOnHeap = 7,
};
enum DXMT_RESOURCE_RESIDENCY : uint32_t { DXMT_RESOURCE_RESIDENCY_READ = 0x15,
                                        DXMT_RESOURCE_RESIDENCY_WRITE = 0x2a };
struct Allocation {};
struct BufferAllocation : Allocation {
  WMT::Buffer value;
  Flags<BufferAllocationFlag> bits;
  unsigned refs = 0;
  BufferAllocation(WMT::Buffer value, Flags<BufferAllocationFlag> bits) : value(value), bits(bits) {}
  void incRef() { ++refs; }
  void decRef() { assert(refs); --refs; }
  WMT::Buffer buffer() { return value; }
  Flags<BufferAllocationFlag> flags() { return bits; }
};
struct CommandQueue {
  struct CaptureState {
    unsigned ownership_checks=0;
    bool takeCaptureOwnership() { ++ownership_checks; return false; }
  } capture_state;
  WMT::Device device;
  WMT::CommandQueue commandQueue;
  WMT::ResidencySet residency_set_;
  // NATIVE_REFERENCE
  ResidencySetPolicy<ResidencyBufferReference> residency_policy_;
  // BACKEND
  struct CommandBuffer {
    bool valid = false;
    int value = WMTCommandBufferStatusCompleted;
    explicit operator bool() const { return valid; }
    int status() { return value; }
    void waitUntilCompleted() { assert(value == WMTCommandBufferStatusScheduled); value=WMTCommandBufferStatusCompleted; --pending; }
  };
  struct Chunk { CommandBuffer attached_cmdbuf; };
  std::vector<Chunk> chunks;
  void drain() {
    // CAPTURE_CLEANUP
    // SHUTDOWN_DRAIN
  }
  bool TryReadOnlyResidency(BufferAllocation*, DXMT_RESOURCE_RESIDENCY, uint64_t);
  void InitializeResidencySet();
};
// QUEUE_API
// INITIALIZE
}

struct Backend {
  bool fail = false;
  std::set<uint64_t> active;
  unsigned adds = 0, removes = 0;
  bool add(WMT::Allocation a) { ++adds; if(fail) return false; assert(active.insert(a.handle).second); return true; }
  void remove(WMT::Allocation a) {
    assert(native.at(a.handle).use_count() >= 2); // retained through remove
    ++removes; assert(active.erase(a.handle) == 1);
  }
};
using Policy = dxmt::ResidencySetPolicy<WMT::Reference<WMT::Buffer>>;

static void stability_and_completion() {
  Policy p({4, 64, 32, 3}); Backend b;
  auto a = buffer(1, 16), renamed = buffer(2, 16);
  assert(!p.cover(a,16,1,true,b));
  assert(!p.cover(a,16,1,true,b)); // same chunk cannot establish stability
  assert(p.cover(a,16,2,true,b)); assert(b.adds == 1);
  assert(p.cover(a,16,2,true,b)); assert(b.adds == 1);
  assert(!p.cover(renamed,16,2,true,b)); // logical rename is new backing
  assert(p.cover(renamed,16,3,true,b));
  p.retire(1,b); assert(b.removes == 0); // not even first use complete
  p.retire(4,b); assert(b.removes == 0); // last use=2, idle span not met
  assert(p.cover(a,16,5,true,b)); // reuse postpones retirement
  p.retire(6,b); assert(b.removes == 1); assert(b.active.count(1));
  p.retire(5,b); assert(b.removes == 1); // completion cannot move backwards
  p.retire(8,b); assert(b.removes == 2); assert(p.heldCount() == 0);
  assert(!p.cover(a,16,8,true,b)); // completed sequences are never new work
  assert(p.statistics().admitted == 2); assert(p.statistics().retired == 2);
}

static void caps_and_hostile_inputs() {
  Policy p({2, 32, 24, 3}); Backend b;
  auto a=buffer(10,24), c=buffer(11,16), d=buffer(12,8);
  assert(!p.cover(a,24,1,true,b)); assert(p.heldBytes()==24);
  assert(!p.cover(c,16,1,true,b)); assert(p.heldCount()==1); // byte cap
  assert(!p.cover(d,8,1,true,b)); assert(p.heldBytes()==32);
  assert(!p.cover(buffer(13,1),1,1,true,b)); assert(p.heldCount()==2); // count cap
  assert(!p.cover(a,23,2,true,b)); assert(b.adds==0); // size mismatch
  assert(!p.cover(a,24,0,true,b));
  assert(!p.cover(a,0,2,true,b));
  assert(!p.cover(a,UINT64_MAX,2,true,b));
  assert(!p.cover(a,24,2,false,b)); // writable/imported/tracked eligibility rejection
  assert(p.cover(a,24,2,true,b));
  assert(!p.cover(a,24,1,true,b)); // stale sequence
  assert(p.statistics().overflow==2);
  p.clearAfterDrain(b); assert(b.active.empty()); assert(p.heldBytes()==0);
  Policy disabled({0,32,32,3});
  assert(!disabled.cover(a,24,3,true,b)); assert(disabled.heldCount()==0);
  Policy count_only({1,1024,1024,3});
  assert(!count_only.cover(a,24,3,true,b));
  assert(!count_only.cover(c,16,3,true,b)); assert(count_only.heldCount()==1);
  Policy huge({4,UINT64_MAX,UINT64_MAX,3});
  assert(!huge.cover(buffer(14,UINT64_MAX-2),UINT64_MAX-2,1,true,b));
  assert(!huge.cover(buffer(15,4),4,1,true,b)); assert(huge.heldCount()==1);
}

static void failure_and_lifetime() {
  Policy p({2,32,32,2}); Backend b;
  auto a=buffer(20,16);
  std::weak_ptr<NativeState> weak=native.at(20);
  assert(!p.cover(a,16,1,true,b));
  b.fail=true; assert(!p.cover(a,16,2,true,b)); assert(b.active.empty());
  assert(!p.cover(a,16,2,true,b)); // no retry in same chunk
  b.fail=false; assert(p.cover(a,16,3,true,b));
  assert(p.statistics().failed==1);
  auto owner=native.at(20); native.erase(20); owner.reset();
  assert(!weak.expired()); // cache keeps exact native backing alive
  native[20]=weak.lock(); // observable backend needs its state again
  p.retire(4,b); assert(!b.active.empty());
  p.retire(5,b); assert(b.active.empty());
  native.erase(20); assert(weak.expired());
}

static void alias_retention_updates_last_use() {
  Policy p({2,64,32,3}); Backend b;
  int owner=0, unrelated=0;
  auto a=buffer(25,16);
  assert(!p.cover(a,16,1,true,b,&owner));
  assert(p.cachedCharge(a.handle,&owner)==16);
  assert(p.cachedCharge(a.handle,&unrelated)==0);
  assert(p.cachedCharge(a.handle+1,&owner)==0);
  assert(p.cachedCharge(a.handle,nullptr)==0);
  p.touch(&owner,2); // existing access/Retain runs before residency coverage
  assert(p.cover(a,16,2,true,b,&owner)); // a touch cannot defeat stability
  p.touch(&owner,6); // later view/copy access still uses this backing
  p.touch(&owner,4); // stale touch cannot move retirement backwards
  p.touch(&unrelated,100); // different resource cannot prolong this backing
  p.retire(5,b); assert(b.removes==0); // alias use not completed
  p.retire(8,b); assert(b.removes==0); // alias completed but idle span not met
  assert(!p.cover(a,16,9,true,b,&unrelated)); // mismatched source owner is rejected
  p.retire(9,b); assert(b.removes==1);
  assert(p.cachedCharge(a.handle,&owner)==0); // retirement invalidates the charge
}

static void exact_queue_admission() {
  using namespace dxmt;
  CommandQueue q;
  auto a=buffer(30,16);
  BufferAllocation value{a, Flags<BufferAllocationFlag>(BufferAllocationFlag::GpuReadonly,
                                                       BufferAllocationFlag::GpuManaged)};
  auto read=DXMT_RESOURCE_RESIDENCY(1), write=DXMT_RESOURCE_RESIDENCY(2);
  assert(!q.TryReadOnlyResidency(&value,read,1));
#if DXMT_RESIDENCY_SETS
  assert(q.TryReadOnlyResidency(&value,read,2));
  assert(q.TryReadOnlyResidency(&value,read,3));
  assert(native.at(30)->size_reads==1); // candidates and members reuse exact-owner charge
  assert((operations == std::vector<char>{'a','c'})); // commit before coverage
  assert(!q.TryReadOnlyResidency(&value,write,3));
  assert(!q.TryReadOnlyResidency(&value,DXMT_RESOURCE_RESIDENCY(3),3));
  assert(!q.TryReadOnlyResidency(&value,DXMT_RESOURCE_RESIDENCY(0),3));
  for(auto excluded : {BufferAllocationFlag::AllocatedOnHeap, BufferAllocationFlag::CpuPlaced,
                       BufferAllocationFlag::OwnedByCommandList}) {
    value.bits.set(excluded); assert(!q.TryReadOnlyResidency(&value,read,3)); value.bits.clr(excluded);
  }
  value.bits.clr(BufferAllocationFlag::GpuReadonly);
  assert(!q.TryReadOnlyResidency(&value,read,3));
  value.bits.set(BufferAllocationFlag::GpuReadonly);
  q.residency_set_.valid=false; assert(!q.TryReadOnlyResidency(&value,read,3));
  assert(!q.TryReadOnlyResidency(nullptr,read,3));
  assert(native.at(30)->size_reads==1); // rejected cases never query native size
  assert(value.refs==1); // exact source allocation identity stays alive
  CommandQueue::ResidencyBackend backend{WMT::ResidencySet{}};
  (void)backend;
  q.residency_set_.valid=true;
  BufferAllocation different_owner{a,value.bits};
  assert(!q.TryReadOnlyResidency(&different_owner,read,4));
  assert(native.at(30)->size_reads==2); // same handle alone cannot reuse charge
  assert(different_owner.refs==0); assert(q.residency_policy_.heldCount()==1);
  q.chunks = {{{true,WMTCommandBufferStatusScheduled}},{{true,WMTCommandBufferStatusCompleted}},
              {{true,WMTCommandBufferStatusScheduled}},{{false,WMTCommandBufferStatusCompleted}}};
  pending=2;
  q.drain(); assert(pending==0); assert(q.residency_policy_.heldCount()==0);
  assert(q.capture_state.ownership_checks==1); assert(capture_stops==0);
  assert(value.refs==0);
  assert((operations == std::vector<char>{'a','c','r','c'}));
  // A dead identity cannot leave a small cached charge for a newly observed
  // backing, even if the native allocator reuses its handle and source address.
  native.at(30)->bytes=17ull*1024*1024;
  assert(!q.TryReadOnlyResidency(&value,read,40));
  assert(native.at(30)->size_reads==3); assert(q.residency_policy_.heldCount()==0);
#else
  assert(!q.TryReadOnlyResidency(&value,read,2));
  assert(!q.TryReadOnlyResidency(&value,write,2));
  assert(native.at(30)->size_reads==0); assert(operations.empty());
#endif
}

static void exact_os_and_creation_fallback() {
  dxmt::CommandQueue q;
  q.residency_set_.valid=false;
  os_major=14; q.InitializeResidencySet();
  assert(!q.residency_set_); assert(creations==0); assert(attachments==0);
  os_major=15; creation_succeeds=false; q.InitializeResidencySet();
  assert(!q.residency_set_); assert(attachments==0);
  creation_succeeds=true; q.InitializeResidencySet();
#if DXMT_RESIDENCY_SETS
  assert(q.residency_set_); assert(os_reads==3); assert(creations==2); assert(attachments==1);
#else
  assert(!q.residency_set_); assert(os_reads==0); assert(creations==0); assert(attachments==0);
#endif
}

int main() {
  stability_and_completion(); caps_and_hostile_inputs(); failure_and_lifetime(); alias_retention_updates_last_use(); exact_queue_admission(); exact_os_and_creation_fallback();
  std::cout << "PASS residency policy, completion, rename, caps, failure, native lifetime and queue admission; flag="
            << DXMT_RESIDENCY_SETS << '\n';
}
