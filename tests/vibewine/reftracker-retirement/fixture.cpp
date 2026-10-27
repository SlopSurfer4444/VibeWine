// SPDX-License-Identifier: MIT
// Complete queue finish/Retain/reset and tracker/ring bodies; native boundaries mocked.
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <list>
#include <memory>
#include <mutex>
#include <new>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

static thread_local bool fail_new = false;
void *operator new(std::size_t size) {
  if (fail_new) throw std::bad_alloc();
  if (void *ptr = std::malloc(size ? size : 1)) return ptr;
  throw std::bad_alloc();
}
void operator delete(void *ptr) noexcept { std::free(ptr); }
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void *ptr) noexcept { ::operator delete(ptr); }

#define unlikely(x) (x)
template<class... T> void ignored_log(T...) {}
#define ERR(...) ignored_log(__VA_ARGS__)
#define WARN(...) ((void)0)
#define TRACE(...) ((void)0)
#define DXMT_IDLE_RETIREMENT 0
#define DXMT_RESIDENCY_SETS 0
namespace dxmt {
using mutex = std::mutex;
template <typename T> constexpr T align(T value, size_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}
}
static std::unordered_map<void *, size_t> storage;
static size_t created = 0;
static void *tracked_malloc(size_t bytes) {
  void *ptr = std::malloc(bytes);
  assert(ptr);
  storage.emplace(ptr, bytes);
  ++created;
  return ptr;
}
static void tracked_free(void *ptr) {
  assert(storage.erase(ptr) == 1);
  std::free(ptr);
}
#include "owners.hpp"

constexpr int WMTCommandBufferStatusScheduled = 2;
constexpr int WMTCommandBufferStatusCompleted = 3;
constexpr int WMTCommandBufferStatusError = 4;
constexpr int THREAD_PRIORITY_TIME_CRITICAL = 0;
static int GetCurrentThread() { return 0; }
static void SetThreadPriority(int, int) {}
namespace env { static void setThreadName(const char *) {} }
namespace cs2trace {
struct Scope { int type; uint64_t object; ~Scope() {} };
struct StallScope { template<class... T> StallScope(T...) {} };
struct ApiScope {};
struct StallCompletionWait {
  uint64_t initializer_observed = 0, ready_encode = 0, ready_commit = 0, ongoing = 0, coherent = 0, latency = 0;
  template<class... T> StallCompletionWait(T...) {}
  bool active() const { return false; }
  void begin() {}
  bool finish() { return false; }
  template<class... T> void publish(T...) {}
};
static uint32_t generation() { return 0; }
static bool detailLimit() { return false; }
static uint64_t now() { return 0; }
static bool profileAllows(int) { return false; }
template<class... T> void record(T...) {}
template<class... T> void detailRecord(T...) {}
}
struct Fence {
  uint64_t value = 0;
  void signal(uint64_t seq) { value = seq; }
  uint64_t signaledValue() const { return value; }
};
namespace WMT { struct SharedEvent : Fence { void *handle = nullptr; }; }
struct Text { const char *getUTF8String() const { return "mock error"; } };
struct Error { Text description() const { return {}; } };
struct Logs {
  explicit operator bool() const { return false; }
  std::array<Error, 0> elements() const { return {}; }
};
struct CommandBuffer {
  int state = WMTCommandBufferStatusCompleted;
  int status() const { return state; }
  void waitUntilCompleted() { state = WMTCommandBufferStatusCompleted; }
  Error error() const { return {}; }
  Logs logs() const { return {}; }
  uint64_t gpuStartTime() const { return 0; }
  uint64_t gpuEndTime() const { return 0; }
  CommandBuffer &operator=(std::nullptr_t) { return *this; }
};
struct List { void reset() {} };
namespace dxmt {
#include "queue-constant.hpp"
struct CommandChunk {
  uint64_t signal_frame_latency_fence_ = ~0ull, chunk_id = 0, frame_ = 0, resource_initializer_event_id = 0;
  uint32_t trace_generation = 0;
  CommandBuffer attached_cmdbuf;
  List list_enc;
  AllocationRefTracking ref_tracker;
  struct Readback {} readback;
#include "chunk-reset.hpp"
};
struct IdleAllocator { void free_blocks(uint64_t) {} };
struct CommandQueue {
  struct Ready {
    CommandQueue *queue;
    void wait(uint64_t seq, std::memory_order) {
      if (seq > queue->finish_until) queue->stopped = true;
      else if (queue->before_finish) queue->before_finish(seq);
    }
    uint64_t load(std::memory_order) const { return 0; }
  } ready_for_commit{this}, ready_for_encode{this};
  std::atomic<bool> stopped = false;
  std::atomic<unsigned> chunk_ongoing = 32;
  Fence cpu_coherent, frame_latency_fence_;
  struct Initializer { WMT::SharedEvent event() const { return {}; } } initializer;
  std::array<CommandChunk, kCommandChunkCount> chunks;
  IdleAllocator staging_allocator, copy_temp_allocator, argbuf_allocator;
  RingBumpState<HostBufferBlockAllocator, 0x1000> reftracker_storage_allocator{{}};
  uint64_t finish_until = 1;
  std::function<void(uint64_t)> before_finish;
  uint64_t traceSerial() const { return 0; }
  uint32_t WaitForFinishThread();
  void Retain(uint64_t, Allocation *);
};
#include "queue-methods.hpp"
}
struct Resource : dxmt::Allocation {
  unsigned freed = 0;
  void free() override { ++freed; }
};
static void retain_blocks(dxmt::CommandQueue &queue, uint64_t seq, unsigned blocks, Resource &resource) {
  // Real tracker has 29 inline slots, then 509 pointers per full 4KiB block.
  for (unsigned i = 0; i < 29 + blocks * 509; ++i) queue.Retain(seq, &resource);
}
static void finish(dxmt::CommandQueue &queue, uint64_t until) {
  queue.finish_until = until;
  queue.WaitForFinishThread();
  assert(queue.cpu_coherent.signaledValue() == until);
}
static unsigned failed = 0;
static void check(bool condition, const char *name) {
  if (!condition) { ++failed; std::cerr << "FAIL: " << name << '\n'; }
}
static void burst(unsigned until, size_t expected) {
  Resource resource;
  {
    dxmt::CommandQueue queue;
    retain_blocks(queue, 1, 10, resource);
    assert(storage.size() == 10);
    fail_new = true; // Finish cleanup must still work under host allocation failure.
    finish(queue, until);
    fail_new = false;
    assert(resource.freed == 1);
    check(storage.size() == expected, "full finish method completed storage bound");
  }
  assert(storage.empty());
}
static void pending_and_external_owner() {
  Resource old, pending;
  pending.incRef(); // Deferred/independent owner survives queue tracker clear.
  {
    dxmt::CommandQueue queue;
    retain_blocks(queue, 1, 10, old);
    const auto before = created;
    queue.before_finish = [&](uint64_t seq) {
      if (seq == 302) retain_blocks(queue, 303, 4, pending);
    };
    finish(queue, 302);
    assert(old.freed == 1 && pending.freed == 0);
    check(storage.size() == 5, "expired burst preserves four pending tracking blocks and warm spare");
    assert(created == before); // Older completed storage is reused before creating new blocks.
    // Actual clear still reads every pending storage node after maintenance.
    queue.chunks[303 % 32].reset();
    assert(pending.freed == 0);
  }
  assert(storage.empty());
  pending.decRef();
  assert(pending.freed == 1);
}
static void all_slots_and_error() {
  std::array<Resource, 32> resources;
  {
    dxmt::CommandQueue queue;
    for (unsigned seq = 1; seq <= 32; ++seq) {
      retain_blocks(queue, seq, 1, resources[seq - 1]);
      queue.chunks[seq % 32].attached_cmdbuf.state = seq == 17 ? WMTCommandBufferStatusError : WMTCommandBufferStatusCompleted;
    }
    finish(queue, 333);
    for (auto &resource : resources) assert(resource.freed == 1);
    check(storage.size() == 1, "all 32 completed tracker slots, including terminal error, retire");
  }
  assert(storage.empty());
}
static void strict_frontier() {
  Resource first, second;
  dxmt::CommandQueue queue;
  retain_blocks(queue, 1, 1, first);
  queue.chunks[1].reset();
  queue.cpu_coherent.signal(1);
  retain_blocks(queue, 2, 1, second);
  assert(storage.size() == 2); // Reuse is strict <, never equal to frontier.
  queue.chunks[2].reset();
}
static void inline_and_near_limit() {
  Resource resource;
  {
    dxmt::CommandQueue queue;
    for (unsigned i = 0; i < 29; ++i) queue.Retain(1, &resource);
    assert(storage.empty());
    finish(queue, 2);
    assert(resource.freed == 1 && storage.empty());
  }
  {
    // No UINT64_MAX sentinel: exercise ordinary subtraction near the frontier
    // limit without turning a pending allocation into unconditional teardown.
    dxmt::RingBumpState<dxmt::HostBufferBlockAllocator, 4096> ring{{}};
    constexpr uint64_t last = UINT64_MAX - 400;
    for (unsigned i = 0; i < 10; ++i) ring.allocate(last, 0, 4096, 1);
    auto &pending = ring.allocate(UINT64_MAX - 1, 0, 4096, 1).first;
    auto *ptr = pending.ptr;
    ring.free_blocks(last + 300);
    assert(storage.size() == 11 && storage.contains(ptr));
    ring.free_blocks(last + 301);
    assert(storage.size() == 2 && storage.contains(ptr));
    ring.free_blocks(UINT64_MAX - 2);
    assert(storage.size() == 2 && storage.contains(ptr));
  }
  assert(storage.empty());
}
int main() {
  burst(301, 10);
  burst(302, 1);
  pending_and_external_owner();
  all_slots_and_error();
  strict_frontier();
  inline_and_near_limit();
  assert(storage.empty());
  if (failed) return 2;
  std::cout << "PASS: actual finish/Retain/reset + tracker + host ring reclamation and ownership controls\n";
}
