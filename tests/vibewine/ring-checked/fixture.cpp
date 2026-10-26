#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <list>
#include <limits>
#include <mutex>
#include <new>
#include <span>
#include <utility>

// The native seam allocates only its tiny mock handle with malloc, so global
// new fault injection isolates the real production std::list node allocation.
static int fail_new = -1;
void *operator new(size_t size) {
  if (fail_new == 0) {
    fail_new = -1;
    throw std::bad_alloc{};
  }
  if (fail_new > 0)
    --fail_new;
  if (auto ptr = std::malloc(size))
    return ptr;
  throw std::bad_alloc{};
}
void operator delete(void *ptr) noexcept { std::free(ptr); }
void operator delete(void *ptr, size_t) noexcept { std::free(ptr); }

struct Counts {
  size_t calls = 0, created = 0, live = 0, destroyed = 0;
  size_t fail_call = 0, throw_call = 0;
  bool zero_gpu_address = false;
  ~Counts() { assert(live == 0 && created == destroyed); }
};
static int fail_malloc = -1;
static size_t host_live = 0;
static void *testMalloc(size_t) {
  if (fail_malloc == 0) {
    fail_malloc = -1;
    return nullptr;
  }
  if (fail_malloc > 0)
    --fail_malloc;
  auto ptr = std::malloc(1); // Metadata-only fixture; never mapped or written.
  if (ptr)
    ++host_live;
  return ptr;
}
static void testFree(void *ptr) {
  if (ptr) {
    assert(host_live);
    --host_live;
    std::free(ptr);
  }
}

using WMTResourceOptions = unsigned;
struct WMTBufferInfo {
  struct Memory {
    void *ptr = nullptr;
    void set(void *value) { ptr = value; }
  } memory;
  WMTResourceOptions options = 0;
  uint64_t length = 0, gpu_address = 0;
};
namespace WMT {
struct Buffer {
  Counts *counts;
  size_t id;
};
template <class T> class Reference {
public:
  T *handle = nullptr;
  Reference() = default;
  explicit Reference(T *ptr) : handle(ptr) {}
  Reference(const Reference &) = delete;
  Reference(Reference &&other) noexcept : handle(std::exchange(other.handle, nullptr)) {}
  Reference &operator=(Reference &&other) noexcept {
    reset();
    handle = std::exchange(other.handle, nullptr);
    return *this;
  }
  ~Reference() { reset(); }
  explicit operator bool() const { return handle != nullptr; }
private:
  void reset() {
    if (handle) {
      --handle->counts->live;
      ++handle->counts->destroyed;
      std::free(handle);
      handle = nullptr;
    }
  }
};
struct Device {
  Counts *counts = nullptr;
  Reference<Buffer> newBuffer(WMTBufferInfo &info) {
    ++counts->calls;
    info.gpu_address = 0;
    if (counts->calls == counts->throw_call)
      throw std::bad_alloc{};
    if (counts->calls == counts->fail_call)
      return {};
    auto buffer = static_cast<Buffer *>(std::malloc(sizeof(Buffer)));
    assert(buffer);
    *buffer = {counts, ++counts->created};
    ++counts->live;
    info.gpu_address = counts->zero_gpu_address ? 0 : buffer->id * 4096;
    return Reference<Buffer>{buffer};
  }
};
}
namespace dxmt {
using mutex = std::mutex;
template <typename T> constexpr T align(T size, size_t alignment) {
  return (size + alignment - 1) & ~(alignment - 1);
}
}
#define WARN(...) ((void)0)
#define malloc testMalloc
#define free testFree
#define private public
#include "ring-under-test.hpp"
#undef private
#undef free
#undef malloc

using Allocator = dxmt::StagingBufferBlockAllocator;
using Ring = dxmt::RingBumpState<Allocator>;
constexpr size_t MB = 1024 * 1024;
constexpr size_t standard = dxmt::kStagingBlockSize;

template <class R> struct Snapshot {
  struct Entry {
    const void *address = nullptr;
    size_t used = 0, total = 0;
    uint64_t seq = 0, ttl = 0;
    bool operator==(const Entry &) const = default;
  };
  std::array<Entry, 16> entries{};
  size_t count = 0;
  explicit Snapshot(const R &ring) {
    for (const auto &entry : ring.fifo) {
      assert(count < entries.size());
      entries[count++] = {&entry.block, entry.allocated_size, entry.total_size,
                          entry.last_used_seq_id, entry.inc_time_to_live};
    }
  }
  bool operator==(const Snapshot &) const = default;
};
static std::array<Ring::Reservation, 2> sentinels() {
  return {{{nullptr, 1234}, {nullptr, 5678}}};
}
static void unchanged(const std::array<Ring::Reservation, 2> &out) {
  assert(out[0].block == nullptr && out[0].offset == 1234);
  assert(out[1].block == nullptr && out[1].offset == 5678);
}
static void idle(Ring &ring, uint64_t coherent, size_t n = 17) {
  for (size_t i = 0; i < n; ++i)
    ring.free_blocks(coherent);
}

static void invalid_inputs() {
  Counts counts;
  Ring ring{Allocator{{&counts}, 0}};
  ring.allocate(1, 0, 8, 1);
  idle(ring, 1);
  const Snapshot before{ring};
  auto out = sentinels();
  std::array<size_t, 3> three{1, 1, 1};
  assert(!ring.allocateChecked(2, 1, {}, 1, {}));
  assert(!ring.allocateChecked(2, 1, three, 1, out));
  for (const auto sizes : {std::array<size_t, 2>{0, 1}, {1, 0}, {1, SIZE_MAX}})
    assert(!ring.allocateChecked(2, 1, sizes, 256, out));
  std::array<size_t, 2> sizes{1, 1};
  for (size_t alignment : {size_t(0), size_t(3)})
    assert(!ring.allocateChecked(2, 1, sizes, alignment, out));
  assert(!ring.allocateChecked(2, 1, sizes, 1, std::span(out).first(1)));
  assert(!ring.allocateChecked(2, 1, std::span(sizes).first(1), 1, out));
  std::array<Ring::Reservation, 3> too_many{};
  assert(!ring.allocateChecked(2, 1, sizes, 1, too_many));
  assert(Snapshot{ring} == before && counts.calls == 1);
  unchanged(out);
}

static void failure_A_tail() {
  Counts counts;
  Ring ring{Allocator{{&counts}, 0}};
  auto *tail = &ring.allocate(1, 0, 8 * MB, 1).first;
  idle(ring, 1);
  const Snapshot before{ring};
  auto out = sentinels();
  counts.fail_call = counts.calls + 1;
  assert(!ring.allocateChecked(2, 1, std::array<size_t, 2>{8 * MB, standard}, 256, out));
  assert(Snapshot{ring} == before && counts.live == 1);
  unchanged(out);
  assert(ring.allocateChecked(2, 1, std::array<size_t, 1>{16 * MB}, 256, std::span(out).first(1)));
  assert(out[0].block == tail && out[0].offset == 8 * MB);
}

static void failure_B_reuse_and_order() {
  Counts counts;
  Ring ring{Allocator{{&counts}, 0}};
  auto *a = &ring.allocate(1, 0, standard, 1).first;
  auto *b = &ring.allocate(1, 0, standard, 1).first;
  idle(ring, 1);
  const Snapshot before{ring};
  auto out = sentinels();
  counts.fail_call = counts.calls + 1;
  assert(!ring.allocateChecked(3, 2, std::array<size_t, 2>{standard, standard + 1}, 256, out));
  assert(Snapshot{ring} == before && counts.live == 2);
  unchanged(out);
  // No new buffer is permitted. Two standard blocks must remain separate;
  // combining the request into one 40MiB allocation would fail this case.
  counts.fail_call = counts.calls + 1;
  const auto calls = counts.calls;
  fail_new = 0;
  assert(ring.allocateChecked(3, 2, std::array<size_t, 2>{standard, 8 * MB}, 256, out));
  assert(fail_new == 0); // Reuse must splice, with no host node allocation.
  fail_new = -1;
  assert(counts.calls == calls && out[0].block == a && out[1].block == b);
  assert(out[0].offset == 0 && out[1].offset == 0);
  assert(&ring.fifo.front().block == a && &ring.fifo.back().block == b);
  assert(ring.fifo.front().allocated_size == standard && ring.fifo.back().allocated_size == 8 * MB);
  assert(ring.fifo.front().inc_time_to_live == 0 && ring.fifo.back().inc_time_to_live == 0);
  // The exact same Block objects survive reuse and later cleanup of neighbors.
  assert(a->buffer && b->buffer);
  idle(ring, 3, dxmt::kStagingBlockLifetime + 2);
  assert(counts.live == 1 && b->buffer && &ring.fifo.back().block == b);
}

static void failure_C_pending(bool throw_native, int metadata_failure) {
  Counts counts;
  Ring ring{Allocator{{&counts}, 0}};
  ring.allocate(1, 0, 1, 1);
  idle(ring, 1);
  const Snapshot before{ring};
  auto out = sentinels();
  if (metadata_failure >= 0)
    fail_new = metadata_failure;
  else if (throw_native)
    counts.throw_call = counts.calls + 2;
  else
    counts.fail_call = counts.calls + 2;
  assert(!ring.allocateChecked(4, 1, std::array<size_t, 2>{standard, standard + 1}, 1, out));
  assert(fail_new == -1 && Snapshot{ring} == before && counts.live == 1);
  assert(counts.destroyed == (metadata_failure == 0 ? 1 : metadata_failure == 1 ? 2 : 1));
  unchanged(out);
  // Recovery proves failed candidates never became the current tail.
  counts.fail_call = counts.throw_call = 0;
  assert(ring.allocateChecked(4, 1, std::array<size_t, 2>{standard, standard + 1}, 1, out));
  assert(out[0].block != out[1].block && out[0].offset == 0 && out[1].offset == 0);
  assert(counts.live == 3);
}

static void alignment_and_shared_block() {
  Counts counts;
  Ring ring{Allocator{{&counts}, 0}};
  auto *tail = &ring.allocate(1, 0, 3, 1).first;
  auto out = sentinels();
  assert(ring.allocateChecked(2, 0, std::array<size_t, 2>{17, 19}, 256, out));
  assert(out[0].block == tail && out[1].block == tail);
  assert(out[0].offset == 256 && out[1].offset == 512 && ring.fifo.back().allocated_size == 531);
  // Overflowing tail alignment cannot wrap into an apparently usable offset.
  ring.fifo.back().allocated_size = SIZE_MAX - 2;
  ring.fifo.back().total_size = SIZE_MAX;
  const Snapshot before{ring};
  out = sentinels();
  counts.fail_call = counts.calls + 1;
  assert(!ring.allocateChecked(3, 0, std::array<size_t, 2>{1, 1}, 256, out));
  assert(Snapshot{ring} == before);
  unchanged(out);
}

static void inflight_and_strict_completion() {
  Counts counts;
  Ring ring{Allocator{{&counts}, 0}};
  auto *pending = &ring.allocate(5, 0, standard, 1).first;
  auto out = sentinels();
  const Snapshot before{ring};
  counts.fail_call = counts.calls + 1;
  assert(!ring.allocateChecked(6, 5, std::array<size_t, 1>{1}, 1, std::span(out).first(1)));
  assert(Snapshot{ring} == before && pending->buffer);
  counts.fail_call = counts.calls + 1;
  assert(ring.allocateChecked(7, 6, std::array<size_t, 2>{1, 1}, 256, out));
  assert(out[0].block == pending && out[1].block == pending && out[1].offset == 256);
  idle(ring, 6, 1000);
  assert(counts.live == 1 && pending->buffer && ring.fifo.back().last_used_seq_id == 7);
  ring.free_blocks(UINT64_MAX);
  assert(counts.live == 0);
}

static void adhoc_is_not_recycled() {
  Counts counts;
  Ring ring{Allocator{{&counts}, 0}};
  ring.allocate(1, 0, standard * 2, 1);
  ring.allocate(1, 0, standard, 1);
  const Snapshot before{ring};
  auto out = sentinels();
  counts.fail_call = counts.calls + 1;
  assert(!ring.allocateChecked(3, 2, std::array<size_t, 1>{standard + 1}, 1, std::span(out).first(1)));
  assert(Snapshot{ring} == before);
  unchanged(out);
}

static void allocator_validity() {
  const std::array<size_t, 1> size{1};
  // Native nil is rejected; no failed block enters the FIFO.
  for (bool zero_address : {false, true}) {
    Counts counts;
    counts.fail_call = zero_address ? 0 : 1;
    counts.zero_gpu_address = zero_address;
    dxmt::RingBumpState<dxmt::GpuPrivateBufferBlockAllocator> ring{
        dxmt::GpuPrivateBufferBlockAllocator{{&counts}, 0}};
    decltype(ring)::Reservation out{nullptr, 42};
    const bool success = ring.allocateChecked(1, 0, size, 1, std::span(&out, 1));
    if (zero_address) {
      // Native handle is the allocation result. A zero optional GPU address
      // must not reject a handle the initializer can use for native copies.
      assert(success && out.block && out.block->buffer && out.block->gpu_address == 0);
    } else {
      assert(!success && out.block == nullptr && out.offset == 42 && ring.fifo.empty() && counts.live == 0);
    }
  }
  {
    Counts counts;
    Ring ring{Allocator{{&counts}, 0, true}};
    auto out = sentinels();
    fail_malloc = 0;
    assert(!ring.allocateChecked(1, 0, size, 1, std::span(out).first(1)));
    assert(ring.fifo.empty() && counts.calls == 1 && counts.live == 0);
    unchanged(out); // A valid native buffer is insufficient without placed CPU memory.
  }
  {
    Counts counts;
    counts.zero_gpu_address = true;
    Ring ring{Allocator{{&counts}, 0, false}};
    auto out = sentinels();
    assert(ring.allocateChecked(1, 0, size, 1, std::span(out).first(1)));
    assert(out[0].block->buffer && !out[0].block->mapped_address);
  }
  {
    dxmt::RingBumpState<dxmt::HostBufferBlockAllocator> ring{dxmt::HostBufferBlockAllocator{}};
    decltype(ring)::Reservation out{nullptr, 42};
    fail_malloc = 0;
    assert(!ring.allocateChecked(1, 0, size, 1, std::span(&out, 1)));
    assert(out.block == nullptr && out.offset == 42 && ring.fifo.empty());
    assert(ring.allocateChecked(1, 0, size, 1, std::span(&out, 1)) && out.block->ptr);
  }
  {
    dxmt::RingBumpState<dxmt::HostBufferBlockAllocator> ring{dxmt::HostBufferBlockAllocator{}};
    std::array<decltype(ring)::Reservation, 2> out{{{nullptr, 42}, {nullptr, 43}}};
    const auto live = host_live;
    fail_malloc = 1;
    assert(!ring.allocateChecked(1, 0, std::array<size_t, 2>{standard, standard + 1}, 1, out));
    assert(ring.fifo.empty() && host_live == live);
    assert(out[0].block == nullptr && out[1].block == nullptr && out[0].offset == 42 && out[1].offset == 43);
  }
  {
    // Checked calls must not republish an invalid legacy unchecked block.
    Counts counts;
    counts.fail_call = 1;
    Ring ring{Allocator{{&counts}, 0}};
    ring.allocate(1, 0, 1, 1);
    const Snapshot before{ring};
    auto out = sentinels();
    assert(!ring.allocateChecked(2, 0, size, 1, std::span(out).first(1)));
    assert(Snapshot{ring} == before);
    unchanged(out);
  }
}

int main() {
  invalid_inputs();
  failure_A_tail();
  failure_B_reuse_and_order();
  failure_C_pending(false, -1);
  failure_C_pending(true, -1);
  failure_C_pending(false, 0);
  failure_C_pending(false, 1);
  alignment_and_shared_block();
  inflight_and_strict_completion();
  adhoc_is_not_recycled();
  allocator_validity();
  assert(host_live == 0 && fail_new == -1 && fail_malloc == -1);
  std::cout << "checked ring: atomic failures A/B/C, native+metadata faults, reuse, ownership and boundaries passed\n";
}
