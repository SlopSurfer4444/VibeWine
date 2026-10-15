#pragma once
#include "util_env.hpp"
#include "cs2_trace.hpp"
#include "log/log.hpp"
#include <atomic>
#include <chrono>

namespace dxmt::cs2lab {
inline bool enabled() {
  static const bool value = env::getEnvVar("DXMT_CS2_COMPILE_DIAGNOSTICS") == "1" && !cs2trace::configured();
  return value;
}
inline bool earlyCompile() {
  static const bool value = env::getEnvVar("DXMT_CS2_EARLY_COMPILE") == "1";
  return value;
}
using Clock = std::chrono::steady_clock;
inline double milliseconds(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
inline void wait(std::atomic_bool &ready, const char *kind) {
  if (cs2trace::configured()) {
    cs2trace::Scope timer{.type=3, .object=(uint64_t)&ready, .flags=kind[0]=='c'?2u:kind[0]=='t'?4u:kind[1]=='e'?3u:1u};
    ready.wait(false, std::memory_order_acquire);
    return;
  }
  if (!enabled() || ready.load(std::memory_order_acquire)) {
    ready.wait(false, std::memory_order_acquire);
    return;
  }
  auto start = Clock::now();
  ready.wait(false, std::memory_order_acquire);
  double elapsed = milliseconds(start);
  if (elapsed >= 5.0) {
    static std::atomic_uint count{0};
    if (count.fetch_add(1, std::memory_order_relaxed) < 512)
      Logger::info(str::format("CS2LAB pipeline_wait kind=", kind, " ms=", elapsed));
  }
}
struct CompileTimer {
  uint32_t trace_generation = cs2trace::generation();
  uint64_t trace_start = trace_generation ? cs2trace::now() : 0;
  bool active = enabled() || trace_generation;
  const char *kind;
  const char *name;
  bool cache_hit = false;
  Clock::time_point start = active ? Clock::now() : Clock::time_point{};
  ~CompileTimer() {
    if (trace_generation) {
      uint64_t hash=14695981039346656037ULL;
      for (const char *p=name;*p;p++) hash=(hash^(unsigned char)*p)*1099511628211ULL;
      cs2trace::record(trace_generation,4,trace_start,cs2trace::now(),hash,cache_hit?1u:0u);
    }
    if (!enabled()) return;
    double elapsed = milliseconds(start);
    static std::atomic_uint count{0};
    if (elapsed >= 5.0 && count.fetch_add(1, std::memory_order_relaxed) < 512)
      Logger::info(str::format("CS2LAB compile kind=", kind, " name=", name,
                              " cache_hit=", cache_hit, " ms=", elapsed));
  }
};
} // namespace dxmt::cs2lab
