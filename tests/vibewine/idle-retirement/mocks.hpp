// Only the Metal/Windows boundary is substituted. Production owners, registry,
// methods and intrusive Rc are compiled verbatim by run.py.
#pragma once
#include <algorithm>
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include "rc/util_rc_ptr.hpp"
namespace WMT { struct Device {}; }
namespace dxmt {
using mutex = std::mutex;
inline std::atomic<uint64_t> live_allocations = 0, created_allocations = 0;
inline std::atomic<uint64_t> native_size_queries = 0;
struct NativeAllocation {
  uint64_t size;
  uint64_t allocatedSize() const { ++native_size_queries; return size; }
};
enum class BufferAllocationFlag { CpuPlaced };
enum class TextureAllocationFlag {};
template<class T> struct Flags { void set(T) {} };
struct CountedAllocation {
  std::atomic<unsigned> refs = 0;
  std::atomic<uint64_t> inc_calls = 0, dec_calls = 0;
  uint64_t serial = ++created_allocations;
  std::vector<unsigned char> bytes;
  uint64_t native_size;
  explicit CountedAllocation(uint64_t length) : bytes(length), native_size(length) { ++live_allocations; }
  ~CountedAllocation() { --live_allocations; }
  void incRef() { ++inc_calls; ++refs; }
};
struct BufferAllocation final : CountedAllocation {
#include "buffer-charge-under-test.hpp"
  using CountedAllocation::CountedAllocation;
  void decRef() { ++dec_calls; if (--refs == 0) delete this; }
  NativeAllocation buffer() const { return {native_size}; }
  bool hasSuballocatoin(unsigned) { return false; }
  void *mappedMemory(unsigned) { return bytes.data(); }
};
struct TextureAllocation final : CountedAllocation {
  void *mappedMemory;
  explicit TextureAllocation(uint64_t length) : CountedAllocation(length), mappedMemory(bytes.data()) {}
  void decRef() { ++dec_calls; if (--refs == 0) delete this; }
  NativeAllocation texture() const { return {native_size}; }
};
template<class A, class F> struct MockResource {
  std::atomic<unsigned> refs = 0;
  uint64_t length_;
  Rc<A> name_;
  MockResource(uint64_t length = 4096, WMT::Device = {}) : length_(length) {}
  void incRef() { ++refs; }
  void decRef() { if (--refs == 0) delete this; }
  Rc<A> current() { return name_; }
  Rc<A> allocate(Flags<F>) { return new A(length_); }
  void rename(Rc<A> next) { name_ = std::move(next); }
};
using Buffer = MockResource<BufferAllocation, BufferAllocationFlag>;
using Texture = MockResource<TextureAllocation, TextureAllocationFlag>;
}
