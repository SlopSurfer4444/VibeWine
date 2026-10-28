// SPDX-License-Identifier: MIT
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
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
#include "rc/util_rc_ptr.hpp"

static bool count_new = false, fail_host = false;
static size_t node_allocations = 0;
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
  RingBumpState<HostBufferBlockAllocator, 64, null_mutex> cpu_command_allocator{{}};
#include "allocate-command-under-test.hpp"
};
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
int main(int argc, char **argv) {
  const std::string_view mode = argc > 1 ? argv[1] : "candidate";
  if (mode == "allocation-consumer") {
    dxmt::CommandQueue queue;
    fail_host = true;
    // Actual AllocateCommandData and actual immediate placement-construction
    // consumer. Only the host allocation seam is forced to fail.
    dxmt::moveonly_list<uint64_t> values(static_cast<uint64_t *>(queue.AllocateCommandData(8, 8)), 1);
    values[0] = 1;
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
  assert(node_allocations == (baseline ? 100 : 0));
  if (!baseline) {
    assert(first == recycled && first->id == 1);
    first->id = 99; // A borrowed Block reference survives repeated recycle.
    assert(recycled->id == 99);
  }
  std::cout << "ring_recycle_count=100 list_node_allocations=" << node_allocations
            << " first_identity_preserved=" << (first == recycled) << '\n';

  dxmt::Rc<dxmt::TimestampQuery> normal = new dxmt::TimestampQuery;
  {
    dxmt::TimestampReadbackCBuf readback(WMT::Device{}, WMT::CommandBuffer{}, 1,
                                      dxmt::TimestampQueryList{{normal, 0}});
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
  assert(query->getValue(&value) && value == 123);
  std::cout << "HELD_CPU18: actual readback destructor publishes timestamp=123 during allocation unwind before GPU commit\n";
}
