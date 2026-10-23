#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>

#define WARN(...) ((void)0)
namespace dxmt {
using mutex = std::mutex;
template <typename T> constexpr T align(T size, size_t alignment) {
  return (size + alignment - 1) & ~(alignment - 1);
}
}

// Counts ownership without allocating real GPU-sized buffers. A payload is freed
// exactly once by the real template's move-only Block, erase, reuse, and teardown.
struct Counts {
  std::atomic<size_t> created = 0, live = 0;
  std::array<std::atomic<bool>, 4096> alive{};
};
struct Payload {
  Counts &counts;
  const size_t id;
  uint64_t value = 0;
  explicit Payload(Counts &counts) : counts(counts), id(++counts.created) {
    assert(id < counts.alive.size());
    counts.alive[id] = true;
    ++counts.live;
  }
  ~Payload() {
    assert(counts.alive[id].exchange(false));
    --counts.live;
  }
};
struct CountedAllocator {
  struct Block {
    std::unique_ptr<Payload> payload;
    Block() = default;
    Block(const Block &) = delete;
    Block &operator=(const Block &) = delete;
    Block(Block &&) noexcept = default;
    Block &operator=(Block &&) = delete;
  };
  Counts &counts;
  Block allocate(size_t) {
    Block block;
    block.payload = std::make_unique<Payload>(counts);
    return block;
  }
};
