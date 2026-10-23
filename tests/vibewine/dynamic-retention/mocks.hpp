// Counted non-Metal allocation seam; Dynamic classes and Rc come from production.
#pragma once
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <queue>
#include <vector>
#include "rc/util_rc_ptr.hpp"
namespace dxmt {
using mutex = std::mutex;
enum class BufferAllocationFlag {};
enum class TextureAllocationFlag {};
template<class T> struct Flags {};
inline unsigned live_allocations = 0, created_allocations = 0;
struct CountedAllocation {
  std::atomic<unsigned> refs = 0;
  unsigned serial = ++created_allocations;
  unsigned char bytes[64] = {};
  CountedAllocation() { ++live_allocations; }
  ~CountedAllocation() { --live_allocations; }
  void incRef() { ++refs; }
};
struct BufferAllocation : CountedAllocation {
  void decRef() { if (--refs == 0) delete this; }
  bool hasSuballocatoin(unsigned) { return false; }
  void *mappedMemory(unsigned) { return bytes; }
};
struct TextureAllocation : CountedAllocation {
  void *mappedMemory = bytes;
  void decRef() { if (--refs == 0) delete this; }
};
template<class A, class F> struct MockResource {
  Rc<A> name = new A;
  Rc<A> current() { return name; }
  Rc<A> allocate(Flags<F>) { return new A; }
  void rename(Rc<A> next) { name = std::move(next); }
};
using Buffer = MockResource<BufferAllocation, BufferAllocationFlag>;
using Texture = MockResource<TextureAllocation, TextureAllocationFlag>;
}
