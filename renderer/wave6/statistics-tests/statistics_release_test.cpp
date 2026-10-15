#include "dxmt_statistics.hpp"
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>
#ifdef DXMT_DEBUG
#error This fixture must exercise the release API
#endif
static std::atomic<size_t> allocations{0};
void *operator new(std::size_t n) {
  allocations.fetch_add(1, std::memory_order_relaxed);
  if (auto ptr = std::malloc(n)) return ptr;
  throw std::bad_alloc();
}
void operator delete(void *ptr) noexcept { std::free(ptr); }
int main() {
  static_assert(sizeof(dxmt::FrameStatisticsContainer) == 1, "release statistics must have no producer storage");
  auto before = allocations.load();
  dxmt::FrameStatisticsContainer stats;
  bool callback_invoked = false;
  for (uint64_t frame = 0; frame < 100000; ++frame) {
    stats.update(frame, [&](auto &) { callback_invoked = true; });
    stats.cpuComplete(frame); stats.encodingComplete(frame);
    auto snapshot = stats.snapshot(frame);
    assert(snapshot.sample_count == 0 && snapshot.average.command_buffer_count == 0);
  }
  assert(!callback_invoked && allocations.load() == before);
  std::cout << "release statistics: empty storage, zero allocations, no callback: PASS\n";
}
