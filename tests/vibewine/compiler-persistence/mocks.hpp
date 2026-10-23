#pragma once
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

using namespace std::chrono_literals;
inline std::atomic<bool> start_threads{true};
inline std::atomic<unsigned> created_threads{0};
inline thread_local bool destroying_scheduler = false;
inline std::atomic<bool> scheduler_close_observed{false};
inline int SetThreadPriority(int, int) { return 1; }
inline int GetCurrentThread() { return 0; }
inline constexpr int THREAD_PRIORITY_TIME_CRITICAL = 1;
namespace dxmt {
struct mutex {
  std::mutex native;
  void lock() { native.lock(); }
  void unlock() {
    native.unlock();
    // Test seam: destructor has stored the closed predicate before unlocking.
    if (destroying_scheduler)
      scheduler_close_observed = true;
  }
};
using condition_variable = std::condition_variable_any;
struct thread : std::thread {
  thread() = default;
  explicit thread(std::function<void()> fn)
      : std::thread([fn = std::move(fn)] {
          while (!start_threads.load())
            std::this_thread::yield();
          fn();
        }) { created_threads++; }
  thread(thread &&) = default;
  thread &operator=(thread &&) = default;
  static unsigned hardware_concurrency() { return 4; }
};
}

inline void until(std::function<bool()> predicate) {
  const auto end = std::chrono::steady_clock::now() + 4s;
  while (!predicate()) {
    assert(std::chrono::steady_clock::now() < end);
    std::this_thread::yield();
  }
}

struct FakeWriterState {
  std::atomic<unsigned> inside{0}, peak{0}, calls{0};
  std::atomic<bool> fail{false};
  std::atomic<bool> hold{false}, entered{false}, release{false};
};
inline std::shared_ptr<FakeWriterState> fake_writer;
using WMTMetalVersion = unsigned;
namespace WMT {
struct DispatchData {
  std::shared_ptr<int> payload;
  explicit operator bool() const { return bool(payload); }
  bool operator!=(std::nullptr_t) const { return bool(payload); }
};
struct CacheWriter {
  std::shared_ptr<FakeWriterState> state;
  explicit operator bool() const { return bool(state); }
  template <typename Key> void set(const Key &key, DispatchData data) {
    assert(key == std::make_pair(17, 23));
    assert(data.payload && *data.payload == 42);
    auto active = ++state->inside;
    auto peak = state->peak.load();
    while (active > peak && !state->peak.compare_exchange_weak(peak, active)) {}
    state->entered = true;
    if (state->hold)
      until([&] { return state->release.load(); });
    std::this_thread::sleep_for(1ms);
    state->calls++;
    state->inside--;
    if (state->fail)
      throw 7;
  }
  template <typename Key> uint32_t set_checked(const Key &key, DispatchData data) {
    set(key, data);
    return 101;
  }
};
struct CacheReader {
  explicit operator bool() const { return false; }
};
template <typename T> struct Reference : T {
  Reference() = default;
  Reference(T value) : T(std::move(value)) {}
  Reference &operator=(std::nullptr_t) { static_cast<T &>(*this) = T{}; return *this; }
};
struct Pool {};
inline Pool MakeAutoreleasePool() { return {}; }
}
