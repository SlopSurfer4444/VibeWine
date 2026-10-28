// SPDX-License-Identifier: MIT
// Compile the actual production header; no copied cache implementation.
#include "d3d11/d3d11_shader_singleflight.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

static void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL " << message << '\n';
    std::abort();
  }
}

template <typename Predicate> static void await(Predicate &&predicate, const char *message) {
  auto deadline = std::chrono::steady_clock::now() + 5s;
  while (!predicate()) {
    check(std::chrono::steady_clock::now() < deadline, message);
    std::this_thread::yield();
  }
}

struct Gate {
  std::promise<void> entered;
  std::promise<void> release;
  std::shared_future<void> released = release.get_future().share();
  void stop() {
    entered.set_value();
    released.wait();
  }
  void wait() { check(entered.get_future().wait_for(5s) == std::future_status::ready, "factory entered"); }
};

struct Value {
  inline static std::atomic<int> live = 0;
  int number;
  explicit Value(int number) : number(number) { ++live; }
  ~Value() { --live; }
};

// A test equality observer proves each waiter has found the pending generation
// before releasing its factory. It does not inspect or alter the cache state.
static thread_local std::atomic<int> *admitted = nullptr;
struct Equal {
  bool operator()(int a, int b) const noexcept {
    if (a == b && admitted) {
      ++*admitted;
      admitted = nullptr;
    }
    return a == b;
  }
};
struct CollisionHash {
  size_t operator()(int) const noexcept { return 0; }
};
using Cache = dxmt::ShaderSingleflightCache<int, Value, CollisionHash, Equal>;

template <typename Function> static auto waiter(std::atomic<int> &count, Function function) {
  return std::async(std::launch::async, [&count, function] {
    admitted = &count;
    return function();
  });
}

static void success_and_collisions() {
  Cache cache;
  Gate gate;
  std::atomic<int> calls = 0;
  auto factory = [&] {
    ++calls;
    gate.stop();
    return std::make_unique<Value>(11);
  };
  auto owner = std::async(std::launch::async, [&] { return cache.GetOrCreate(1, factory); });
  gate.wait();
  std::atomic<int> count = 0;
  std::vector<std::future<Value *>> waiters;
  for (int i = 0; i < 12; ++i)
    waiters.push_back(waiter(count, [&] { return cache.GetOrCreate(1, factory); }));
  await([&] { return count == 12; }, "success waiters admitted");
  for (auto &result : waiters)
    check(result.wait_for(0s) != std::future_status::ready, "pending success is not published early");

  // Deliberately collide while key 1 is blocked: a global factory lock would
  // deadlock here, and hash-only equality would return the wrong value.
  auto distinct = std::async(std::launch::async, [&] {
    return cache.GetOrCreate(2, [] { return std::make_unique<Value>(22); });
  });
  check(distinct.wait_for(5s) == std::future_status::ready, "distinct key progresses");
  auto second = distinct.get();
  check(second && second->number == 22, "full key equality despite hash collision");
  gate.release.set_value();
  auto first = owner.get();
  check(first && first->number == 11 && first != second, "successful keyed values");
  for (auto &result : waiters)
    check(result.get() == first, "same key gets shared result");
  check(calls == 1, "one successful initializer");
  check(cache.GetOrCreate(1, []() -> std::unique_ptr<Value> {
          check(false, "cached factory not called");
          return nullptr;
        }) == first, "subsequent cache hit");
}

static void failed_generation_and_retry(bool throwing) {
  Cache cache;
  Gate gate;
  std::atomic<int> calls = 0;
  auto call = [&]() -> Value * {
#if defined(__cpp_exceptions)
    try {
#endif
      auto result = cache.GetOrCreate(7, [&]() -> std::unique_ptr<Value> {
        ++calls;
        gate.stop();
#if defined(__cpp_exceptions)
        if (throwing)
          throw std::runtime_error("shared initialization failure");
#endif
        return nullptr;
      });
      check(!throwing && !result, "null failure outcome");
      return result;
#if defined(__cpp_exceptions)
    } catch (const std::runtime_error &error) {
      check(throwing && std::string(error.what()) == "shared initialization failure", "shared factory exception");
      return nullptr;
    }
#endif
  };
  auto owner = std::async(std::launch::async, call);
  gate.wait();
  std::atomic<int> count = 0;
  std::vector<std::future<Value *>> waiters;
  for (int i = 0; i < 12; ++i)
    waiters.push_back(waiter(count, call));
  await([&] { return count == 12; }, "failure waiters admitted");
  for (auto &result : waiters)
    check(result.wait_for(0s) != std::future_status::ready, "pending failure is not published early");
  gate.release.set_value();
  check(!owner.get(), "owner receives failure");
  // A new generation is allowed while old waiters still retain their entry.
  auto replacement = cache.GetOrCreate(7, [] { return std::make_unique<Value>(77); });
  check(replacement && replacement->number == 77, "failed generation retries");
  for (auto &result : waiters)
    check(!result.get(), "old waiters keep failed outcome");
  check(calls == 1, "one failing initializer");
  check(cache.GetOrCreate(7, [] { return std::make_unique<Value>(999); }) == replacement,
        "old generation did not erase replacement");
}

static void cancellation_and_lifetime(bool throwing, bool explicit_close) {
  auto cache = std::make_unique<Cache>();
  auto handle = cache->GetHandle();
  Gate gate;
  auto owner = std::async(std::launch::async, [handle, &gate, throwing] {
    return handle.GetOrCreate(3, [&]() -> std::unique_ptr<Value> {
      gate.stop();
#if defined(__cpp_exceptions)
      if (throwing)
        throw std::runtime_error("cancelled factory");
#else
      (void)throwing;
#endif
      return std::make_unique<Value>(33);
    });
  });
  gate.wait();
  std::atomic<int> count = 0;
  auto waiting = waiter(count, [handle] {
    return handle.GetOrCreate(3, [] {
      check(false, "cancelled waiter must not initialize");
      return std::make_unique<Value>(0);
    });
  });
  await([&] { return count == 1; }, "cancel waiter admitted");
  if (explicit_close) {
    cache->Close();
    cache->Close();
  } else {
    cache.reset();
  }
  check(waiting.wait_for(5s) == std::future_status::ready, "close wakes pending waiter before factory finishes");
  check(!waiting.get(), "cancelled waiter returns null");
  check(owner.wait_for(0s) != std::future_status::ready, "factory remains gated");
  cache.reset();
  check(!handle.GetOrCreate(4, [] {
          check(false, "closed cache must not call new factory");
          return std::make_unique<Value>(0);
        }), "retained closed handle rejects new work");
  gate.release.set_value();
  check(!owner.get(), "late factory outcome cancelled after owner destruction");
  check(Value::live == 0, "late cancelled value destroyed");
}

static void successful_value_lifetime() {
  Cache::Handle handle;
  Value *value;
  {
    Cache cache;
    handle = cache.GetHandle();
    value = handle.GetOrCreate(5, [] { return std::make_unique<Value>(55); });
    cache.Close();
    check(value && value->number == 55 && Value::live == 1, "close retains successful pointer");
  }
  check(value->number == 55 && Value::live == 1, "handle retains value after owner destruction");
  handle = {};
  check(Value::live == 0, "last handle releases successful value");
  check(!handle.GetOrCreate(5, [] { return std::make_unique<Value>(0); }), "empty handle returns null");
}

int main() {
  success_and_collisions();
  check(Value::live == 0, "success values released");
  failed_generation_and_retry(false);
#if defined(__cpp_exceptions)
  failed_generation_and_retry(true);
#endif
  check(Value::live == 0, "retry values released");
  cancellation_and_lifetime(false, true);
  cancellation_and_lifetime(false, false);
#if defined(__cpp_exceptions)
  cancellation_and_lifetime(true, false);
#endif
  successful_value_lifetime();
  std::cout << "PASS production singleflight: success, failure, retry, collisions, distinct keys, close and retained lifetime";
#if defined(__cpp_exceptions)
  std::cout << ", exceptions";
#endif
  std::cout << '\n';
}
