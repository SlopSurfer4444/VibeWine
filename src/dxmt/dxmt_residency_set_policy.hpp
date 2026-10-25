/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Copyright 2026 VibeWine contributors
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#ifndef DXMT_RESIDENCY_SETS
#define DXMT_RESIDENCY_SETS 0
#endif

namespace dxmt {

// One encode-thread owner. NativeRef retains the exact native allocation, so a
// rename or an allocator reusing an address cannot inherit membership. Candidates
// also count against both caps: the stability filter is not an unbounded cache.
template <typename NativeRef> class ResidencySetPolicy {
public:
  struct Limits {
    size_t count = 256;
    uint64_t bytes = 64ull * 1024 * 1024;
    uint64_t member_bytes = 16ull * 1024 * 1024;
    uint64_t idle_completed_sequences = 32;
  };
  struct Statistics {
    uint64_t covered = 0;
    uint64_t admitted = 0;
    uint64_t retired = 0;
    uint64_t overflow = 0;
    uint64_t failed = 0;
    uint64_t fallback = 0;
  };

  explicit ResidencySetPolicy(Limits limits = {}) : limits_(limits) { entries_.reserve(limits.count); }

  // Both identities remain retained until retirement. A native handle alone
  // cannot reuse a different owner's charge, and erased entries have no cache.
  uint64_t cachedCharge(uint64_t handle, const void *owner) const {
    if (!handle || !owner)
      return 0;
    for (const auto &entry : entries_)
      if (entry.allocation.handle == handle && entry.owner == owner)
        return entry.bytes;
    return 0;
  }

  template <typename Native, typename Backend>
  bool cover(Native allocation, uint64_t bytes, uint64_t seq, bool eligible, Backend &backend,
             const void *owner = nullptr) {
    if (!eligible || !allocation.handle || !bytes || !seq || seq <= completed_ || bytes > limits_.member_bytes) {
      increment(statistics_.fallback);
      return false;
    }
    for (auto &entry : entries_) {
      if (entry.allocation.handle != allocation.handle)
        continue;
      // Allocation size is immutable. Disagreement or stale encoding must not
      // produce coverage, even when the native handle is already a set member.
      if (bytes != entry.bytes || seq < entry.last_observation || owner != entry.owner) {
        increment(statistics_.fallback);
        return false;
      }
      const bool stable = seq > entry.last_observation;
      entry.last_observation = seq;
      if (seq > entry.last_use)
        entry.last_use = seq;
      if (!entry.member && stable) {
        if (!backend.add(entry.allocation)) {
          increment(statistics_.failed);
          return false;
        }
        entry.member = true;
        increment(statistics_.admitted);
      }
      if (entry.member)
        increment(statistics_.covered);
      else
        increment(statistics_.fallback);
      return entry.member;
    }
    // Subtraction avoids overflow, including adversarial uint64_t sizes.
    if (entries_.size() >= limits_.count || bytes > limits_.bytes || held_bytes_ > limits_.bytes - bytes) {
      increment(statistics_.overflow);
      return false;
    }
    entries_.push_back({NativeRef(allocation), bytes, seq, seq, owner, false});
    held_bytes_ += bytes;
    increment(statistics_.fallback);
    return false; // observe this backing in a second distinct chunk first
  }

  // All access paths retain their backing, including views/copies that do not
  // suppress declarations. A retained owner identity prevents pointer reuse.
  void touch(const void *owner, uint64_t seq) {
    if (!owner || seq <= completed_)
      return;
    for (auto &entry : entries_)
      if (entry.owner == owner && seq > entry.last_use)
        entry.last_use = seq;
  }

  template <typename Backend> void retire(uint64_t completed, Backend &backend) {
    if (completed <= completed_)
      return;
    completed_ = completed;
    for (size_t i = 0; i < entries_.size();) {
      auto &entry = entries_[i];
      if (entry.last_use > completed || completed - entry.last_use < limits_.idle_completed_sequences) {
        ++i;
        continue;
      }
      if (entry.member)
        backend.remove(entry.allocation); // removal+commit precede native release
      held_bytes_ -= entry.bytes;
      entries_.erase(entries_.begin() + i);
      increment(statistics_.retired);
    }
  }

  // Queue shutdown calls this only after waiting all attached command buffers.
  template <typename Backend> void clearAfterDrain(Backend &backend) {
    for (auto &entry : entries_)
      if (entry.member)
        backend.remove(entry.allocation);
    entries_.clear();
    held_bytes_ = 0;
  }

  uint64_t heldBytes() const { return held_bytes_; }
  size_t heldCount() const { return entries_.size(); }
  const Statistics &statistics() const { return statistics_; }

private:
  struct Entry {
    NativeRef allocation;
    uint64_t bytes;
    uint64_t last_use;
    uint64_t last_observation;
    const void *owner;
    bool member;
  };
  static void increment(uint64_t &value) {
    if (value != std::numeric_limits<uint64_t>::max())
      ++value;
  }
  Limits limits_;
  std::vector<Entry> entries_;
  uint64_t held_bytes_ = 0;
  uint64_t completed_ = 0;
  Statistics statistics_;
};

} // namespace dxmt
