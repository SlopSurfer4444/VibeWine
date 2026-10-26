#pragma once
#include "dxmt_idle_retirement.hpp"
#include <cstdio>

namespace dxmt {

// Finish-thread owned, optional bounded summaries into the Logger diagnostic route.
// The first record acknowledges enablement; it is not a pool sample. Each queue
// gets at most 64 records including that acknowledgement, at most 1023 bytes each.
// Fields describe a partial scan and pool references, never process RSS/freed RAM.
class IdleRetirementDiagnostics {
  static constexpr uint64_t kPeriodNs = 10ull * 1000 * 1000 * 1000;
  bool enabled_;
  uint32_t snapshots_ = 0;
  uint64_t last_ = 0, released_ = 0, bytes_ = 0;
public:
  explicit IdleRetirementDiagnostics(bool enabled) : enabled_(enabled) {}
  bool enabled() const { return enabled_ && snapshots_ < 64; }
  bool record(uint64_t now, const IdleRetirementDomain::Sample &sample,
              uint64_t pid, uint64_t queue, char (&line)[1024]) {
    if (!enabled()) return false;
    released_ += sample.released_count; bytes_ += sample.released_bytes;
    if (!snapshots_) {
      const auto n = std::snprintf(line, sizeof(line),
        "VIBE_IDLE_START pid=%llu queue=%llu enabled=1 time_ns=%llu\n",
        (unsigned long long)pid, (unsigned long long)queue, (unsigned long long)now);
      if (n <= 0 || size_t(n) >= sizeof(line)) {
        enabled_ = false; line[0] = 0; return false;
      }
      ++snapshots_; last_ = now;
      return true;
    }
    if (now < last_ || now - last_ < kPeriodNs) return false;
    const auto n = std::snprintf(line, sizeof(line),
      "VIBE_IDLE pid=%llu queue=%llu snapshot=%u start_ns=%llu end_ns=%llu seq=%llu "
      "sampled=%llu registered=%llu excluded=%llu busy=%llu "
      "current=%llu/%llu/%llu inflight=%llu/%llu/%llu idle=%llu/%llu/%llu "
      "unclassified=%llu/%llu/%llu released_window=%llu/%llu\n",
      (unsigned long long)pid, (unsigned long long)queue, snapshots_ + 1,
      (unsigned long long)last_, (unsigned long long)now, (unsigned long long)sample.coherent_seq,
      (unsigned long long)sample.scanned_resources, (unsigned long long)sample.registered_resources,
      (unsigned long long)sample.excluded_resources, (unsigned long long)sample.skipped_busy,
      (unsigned long long)sample.current.count, (unsigned long long)sample.current.bytes,
      (unsigned long long)sample.current.unknown_bytes_count,
      (unsigned long long)sample.inflight.count, (unsigned long long)sample.inflight.bytes,
      (unsigned long long)sample.inflight.unknown_bytes_count,
      (unsigned long long)sample.completed_idle.count, (unsigned long long)sample.completed_idle.bytes,
      (unsigned long long)sample.completed_idle.unknown_bytes_count,
      (unsigned long long)sample.unclassified.count, (unsigned long long)sample.unclassified.bytes,
      (unsigned long long)sample.unclassified.unknown_bytes_count,
      (unsigned long long)released_, (unsigned long long)bytes_);
    ++snapshots_; last_ = now; released_ = bytes_ = 0;
    if (n <= 0 || size_t(n) >= sizeof(line)) { enabled_ = false; line[0] = 0; return false; }
    return true;
  }
};

} // namespace dxmt
