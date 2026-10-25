// VibeWine opt-in completed-idle pool maintenance. Existing allocation owners
// remain authoritative; this registry never owns a resource or allocation.
#pragma once
#include "thread.hpp"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#ifndef DXMT_IDLE_RETIREMENT
#define DXMT_IDLE_RETIREMENT 0
#endif

namespace dxmt {

template<class T> class IdleRetirementQueue : public std::queue<T> {
public:
  const auto &entries() const { return this->c; }
};

class IdleRetirementDomain {
public:
  static constexpr uint64_t kLifetime = 300;
  static constexpr uint32_t kResourceLimit = 4096;
  static constexpr uint32_t kScanResources = 16;
  static constexpr uint32_t kScanEntries = 128;
  static constexpr uint32_t kReleaseCount = 64;
  static constexpr uint64_t kReleaseBytes = 64ull * 1024 * 1024;
  struct Category { uint64_t count = 0, bytes = 0, unknown_bytes_count = 0; void add(uint64_t n, uint64_t size) { count += n; bytes += n * size; if (!size) unknown_bytes_count += n; } };
  struct Sample {
    // Categories describe only visited resource owners, with explicit unscanned
    // tails. They count pool references, not unique physical allocations/RSS.
    Category current, inflight, completed_idle, unclassified;
    uint64_t registered_resources = 0, scanned_resources = 0, excluded_resources = 0;
    uint64_t skipped_busy = 0, created_count = 0, released_count = 0, released_bytes = 0;
    uint64_t coherent_seq = 0;
  };
  struct Budget {
    uint32_t entries = kScanEntries, releases = kReleaseCount;
    uint64_t bytes = kReleaseBytes;
    bool release(uint64_t size) {
      // Unknown size cannot safely be charged against a byte budget.
      if (!size || !releases || size > bytes) return false;
      --releases; bytes -= size; return true;
    }
  };
  class Registration {
    friend class IdleRetirementDomain;
    std::shared_ptr<IdleRetirementDomain> domain_;
    Registration *previous_ = nullptr, *next_ = nullptr;
    void *owner_ = nullptr;
    using Callback = void (*)(void *, uint64_t, Budget &, Sample &);
    Callback callback_ = nullptr;
    bool linked_ = false;
  public:
    Registration() = default;
    Registration(const Registration &) = delete;
    Registration &operator=(const Registration &) = delete;
    ~Registration() { detach(); }
    void attach(std::shared_ptr<IdleRetirementDomain> domain, void *owner, Callback callback) {
      if (!domain) return;
      domain_ = std::move(domain);
      owner_ = owner; callback_ = callback;
      std::lock_guard<dxmt::mutex> lock(domain_->mutex_);
      if (domain_->count_ == kResourceLimit) { ++domain_->excluded_; return; }
      next_ = domain_->head_;
      if (next_) next_->previous_ = this;
      domain_->head_ = this; linked_ = true; ++domain_->count_;
    }
    void detach() {
      if (!domain_) return;
      // Retain domain while its mutex is locked, including after queue death.
      auto domain = std::move(domain_);
      std::lock_guard<dxmt::mutex> lock(domain->mutex_);
      if (!linked_) { --domain->excluded_; return; }
      if (domain->cursor_ == this) domain->cursor_ = next_;
      if (previous_) previous_->next_ = next_; else domain->head_ = next_;
      if (next_) next_->previous_ = previous_;
      --domain->count_; linked_ = false;
    }
  };

  Sample sweep(uint64_t coherent_seq) {
    Sample sample;
    std::unique_lock<dxmt::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) { sample.skipped_busy = 1; return sample; }
    // No process-global sequence IDs: each queue owns one domain. A stale or
    // repeated frontier may continue a bounded scan without inventing progress.
    coherent_ = std::max(coherent_, coherent_seq);
    sample.coherent_seq = coherent_; sample.registered_resources = count_; sample.excluded_resources = excluded_;
    Budget budget;
    auto remaining = std::min<uint32_t>(count_, kScanResources);
    // Stop at budget exhaustion so an unchargeable large head cannot consume
    // every pass while a whole resource cycle returns to that same head.
    while (remaining-- && budget.entries) {
      if (!cursor_) cursor_ = head_;
      auto node = cursor_;
      cursor_ = node->next_;
      ++sample.scanned_resources;
      node->callback_(node->owner_, coherent_, budget, sample);
    }
    return sample;
  }
private:
  // Only lock order is domain -> resource. Normal owner methods never acquire
  // the domain lock. Explicit owner destructors detach before member teardown.
  dxmt::mutex mutex_;
  Registration *head_ = nullptr, *cursor_ = nullptr;
  uint32_t count_ = 0, excluded_ = 0;
  uint64_t coherent_ = 0;
};

} // namespace dxmt
