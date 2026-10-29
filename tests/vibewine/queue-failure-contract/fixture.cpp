// SPDX-License-Identifier: MIT
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <list>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <span>
#include <string_view>
#include <utility>
#include <vector>
#include <future>
#include <thread>
#include <chrono>
#include "fence-under-test.hpp"
#include "rc/util_rc_ptr.hpp"

static bool count_new = false, fail_host = false;
static size_t node_allocations = 0;
static unsigned listener_cancels = 0;
static void MTLSharedEvent_setWin32EventAtValue(uint64_t, uint64_t, void *, uint64_t) { ++listener_cancels; }
void *operator new(size_t size) {
  if (count_new) ++node_allocations;
  if (auto p = std::malloc(size)) return p;
  throw std::bad_alloc{};
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }
static void *host_malloc(size_t n) { return fail_host ? nullptr : std::malloc(n); }
#define WARN(...) ((void)0)
namespace dxmt {
using mutex = std::mutex;
struct null_mutex { void lock() {} void unlock() {} };
template <typename T> constexpr T align(T n, size_t a) { return (n + a - 1) & ~(a - 1); }
#include "pointer-under-test.hpp"
}
#define malloc host_malloc
#include "ring-under-test.hpp"
#undef malloc
struct CountedAllocator {
  struct Block { unsigned id; Block(unsigned n) : id(n) {} Block(Block &&) = default; };
  unsigned created = 0;
  Block allocate(size_t) { return Block{++created}; }
};
namespace dxmt {
struct CommandQueue {
  std::atomic<uint64_t> ready_for_encode = 1;
  struct Fence { uint64_t signaledValue() const { return 0; } } cpu_coherent;
  RingBumpState<HostBufferBlockAllocator, 4096, null_mutex> cpu_command_allocator{{}};
  enum class Failure : int32_t { None = 0, OutOfMemory = int32_t(0x8007000e), DeviceRemoved = int32_t(0x887a0005) };
  std::atomic<int32_t> failure_reason_{0};
  struct Wake {Wake*next=nullptr;void(*notify)(void*)noexcept=nullptr;void*context=nullptr;};
  Wake*failure_wakes_=nullptr;
  mutex failure_mutex_;
  std::shared_ptr<CpuFence::WaitState> wait_state_ = std::make_shared<CpuFence::WaitState>();
  uint64_t shared_event_listener = 1;
  int32_t FailureReason() const { return failure_reason_.load(); }
  void Fail(Failure) noexcept;
#include "reserve-under-test.hpp"
#include "allocate-command-under-test.hpp"
};
#include "failure-under-test.hpp"
#include "moveonly-list-under-test.hpp"
}
namespace WMT {
struct Device {};
struct CommandBuffer { uint64_t gpuEndTime() const { return 123; } };
struct CounterSampleBuffer {};
}
namespace dxmt {
#include "timestamp-under-test.hpp"
using TimestampQueryList = std::vector<std::pair<Rc<TimestampQuery>, uint64_t>>;
#include "readback-under-test.hpp"
}
struct CounterSeam {
  dxmt::CommandQueue &queue;
  uint64_t *allocated_encoder_argbuf_size_ = nullptr;
  struct Encoder { uint64_t seen=0; void startComputePass(uint64_t value){seen=value;} } encoder;
  using ArgumentEncodingContext=Encoder;
  explicit CounterSeam(dxmt::CommandQueue&q):queue(q){}
  alignas(std::max_align_t) char command[256];
  void (*invoke)(void *, Encoder &) = nullptr;
  template<class Fn> void EmitST(Fn &&fn) {
    using F=std::remove_reference_t<Fn>;
    static_assert(sizeof(F)<=sizeof(command));
    new(command) F(std::forward<Fn>(fn));
    invoke=[](void*p,Encoder&e){auto&fn=*static_cast<F*>(p);fn(e);fn.~F();};
  }
  template<class T> dxmt::moveonly_list<T> AllocateCommandData() {
    return dxmt::moveonly_list<T>((T*)queue.AllocateCommandData(sizeof(T),alignof(T)),1);
  }
  void begin() {
#include "counter-under-test.hpp"
  }
};
template <class Readback> void publish_completed(Readback &readback) {
  if constexpr (requires { readback.publish(); }) readback.publish();
}
int main(int argc, char **argv) {
  const std::string_view mode = argc > 1 ? argv[1] : "candidate";
  if (mode == "allocation-consumer") {
    dxmt::CommandQueue queue;
    fail_host = true;
    // Actual AllocateCommandData and actual immediate placement-construction
    // consumer. Only the host allocation seam is forced to fail.
    dxmt::moveonly_list<uint64_t> values(static_cast<uint64_t *>(queue.AllocateCommandData(8, 8)), 1);
    assert(!values.data());
    assert(queue.FailureReason() == int32_t(dxmt::CommandQueue::Failure::OutOfMemory));
    assert(queue.cpu_coherent.signaledValue() == 0);
    queue.Fail(dxmt::CommandQueue::Failure::DeviceRemoved);
    assert(queue.FailureReason() == int32_t(dxmt::CommandQueue::Failure::OutOfMemory));
    assert(listener_cancels == 1);
    assert(!queue.AllocateCommandData(8, 8)); // no retry after terminal failure
    std::cout << "PASS actual null allocation consumer, terminal first OOM, no coherence/retry, one listener cancel\n";
    return 0;
  }
  dxmt::RingBumpState<CountedAllocator, 64> ring{CountedAllocator{}};
  auto *first = &ring.allocate(1, 0, 64, 1).first;
  ring.allocate(2, 0, 64, 1);
  count_new = true;
  auto *recycled = &ring.allocate(3, 3, 64, 1).first;
  for (unsigned seq = 4; seq <= 102; ++seq) ring.allocate(seq, seq, 64, 1);
  count_new = false;
  const bool baseline = mode == "baseline";
  assert(node_allocations == EXPECT_RECYCLE_NODES);
  if (!baseline) {
    assert(first == recycled && first->id == 1);
    first->id = 99; // A borrowed Block reference survives repeated recycle.
    assert(recycled->id == 99);
  }
  std::cout << "ring_recycle_count=100 list_node_allocations=" << node_allocations
            << " first_identity_preserved=" << (first == recycled) << '\n';

  {
    dxmt::CommandQueue queue;
    assert(queue.AllocateCommandData(8,8)); // one warm production-shaped ring block
    CounterSeam counter{queue};
    node_allocations=0;count_new=true;
    for(unsigned i=0;i<100;++i){counter.begin();assert(counter.allocated_encoder_argbuf_size_);*counter.allocated_encoder_argbuf_size_=37;counter.invoke(counter.command,counter.encoder);assert(counter.encoder.seen==37);}
    count_new=false;
    assert(node_allocations==(baseline?100:0));
    std::cout<<"actual_compute_counter_setups=100 standalone_heap_allocations="<<node_allocations<<" encoded_counter=37\n";
  }
  dxmt::Rc<dxmt::TimestampQuery> normal = new dxmt::TimestampQuery;
  {
    dxmt::TimestampReadbackCBuf readback(WMT::Device{}, WMT::CommandBuffer{}, 1,
                                      dxmt::TimestampQueryList{{normal, 0}});
    if (mode != "baseline") publish_completed(readback);
  }
  uint64_t completed = 0;
  assert(normal->getValue(&completed) && completed == 123);
  std::cout << "normal_completed_readback=123\n";
  dxmt::Rc<dxmt::TimestampQuery> query = new dxmt::TimestampQuery;
  uint64_t value = 999;
  assert(!query->getValue(&value) && value == 999);
  try {
    auto readback = std::make_unique<dxmt::TimestampReadbackCBuf>(
        WMT::Device{}, WMT::CommandBuffer{}, 1, dxmt::TimestampQueryList{{query, 0}});
    // This is the unwind reached if a later production argument allocation
    // fails after flushCommands has prepared readbacks, before any GPU commit.
    throw std::bad_alloc{};
  } catch (const std::bad_alloc &) {}
  if (mode == "baseline") {
    assert(query->getValue(&value) && value == 123);
    std::cout << "OLDFAIL actual readback destructor published canceled timestamp=123\n";
    return 0;
  }
  assert(!query->getValue(&value) && value == 999);
  auto wake = std::make_shared<dxmt::CpuFence::WaitState>();
  dxmt::CpuFence coherent(wake), frame(wake), swapchain(wake);
  coherent.signal(2);
  assert(coherent.wait(2));
  std::vector<std::future<bool>> waits;
  for (unsigned i = 0; i < 16; ++i)
    waits.push_back(std::async(std::launch::async, [&, i] {
      return (i % 3 == 0 ? coherent : i % 3 == 1 ? frame : swapchain).wait(3);
    }));
  wake->cancel();
  for (auto &wait : waits) {
    assert(wait.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    assert(!wait.get());
  }
  assert(coherent.signaledValue() == 2 && frame.signaledValue() == 0 && swapchain.signaledValue() == 0);
  std::cout << "PASS actual readback explicit completion/discard unwind and 16 canceled shared-epoch CPU/frame/swapchain waits, frozen completion values\n";
}
