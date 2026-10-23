// Counting Metal-free allocation seam; staging and Rc are compiled from production sources.
#pragma once
#include <algorithm>
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <new>
#include <queue>
#include <thread>
#include <vector>
#include "rc/util_rc_ptr.hpp"

namespace WMT { struct Device {}; }
namespace dxmt {
using mutex = std::mutex;
enum class BufferAllocationFlag { CpuPlaced };
template <class T> struct Flags { void set(T) {} };
inline std::atomic<size_t> live_allocations = 0, created_allocations = 0;

struct BufferAllocation {
  const size_t serial = ++created_allocations;
  std::vector<unsigned char> bytes;
  std::atomic<unsigned> refs = 0;
  explicit BufferAllocation(size_t length) : bytes(length) { ++live_allocations; }
  ~BufferAllocation() { --live_allocations; }
  void incRef() { ++refs; }
  void decRef() { if (--refs == 0) delete this; }
  void *mappedMemory(unsigned) { return bytes.data(); }
};
struct Buffer {
  const size_t length;
  Rc<BufferAllocation> current;
  std::atomic<unsigned> refs = 0;
  Buffer(size_t length, WMT::Device) : length(length) {}
  void incRef() { ++refs; }
  void decRef() { if (--refs == 0) delete this; }
  Rc<BufferAllocation> allocate(Flags<BufferAllocationFlag>) { return new BufferAllocation(length); }
  Rc<BufferAllocation> rename(Rc<BufferAllocation> &&next) {
    auto old = std::move(current);
    current = std::move(next);
    return old;
  }
};
}
