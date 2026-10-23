#pragma once

#include "thread.hpp"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <queue>
#include <utility>

namespace dxmt {

// Fixed-size optional probe seam: generic executors remain platform independent.
// No pointer to the originating task survives readiness publication.
struct work_trace {
  uint32_t gen = 0;
  uint64_t serial = 0, owner = 0;
  uint64_t (*clock)() = nullptr;
  void (*record)(const work_trace &, uint32_t, uint64_t, uint64_t, uint32_t, uint64_t, uint64_t) = nullptr;
  uint64_t now() const { return gen && clock ? clock() : 0; }
  void emit(uint32_t type, uint32_t flags, uint64_t arg0, uint64_t arg1, uint64_t start = 0, uint64_t end = 0) const {
    if (!gen || !record) return;
    if (!start) start = end = now();
    record(*this, type, start, end, flags, arg0, arg1);
  }
};
inline thread_local work_trace active_persistence_trace;

// A detached, optional write. The callable must own every referenced object.
// charged_bytes includes known payload bytes and reservations for opaque objects;
// it is an admission budget, not a measurement of framework-resident memory.
struct post_completion_work {
  enum class cost_kind { known_payload, opaque_reservation };
  std::function<void()> run;
  uint64_t charged_bytes = 0;
  uint64_t retained_objects = 0;
  cost_kind kind = cost_kind::known_payload;
  work_trace trace;

  explicit operator bool() const { return bool(run); }
};

class bounded_persistence {
public:
  static constexpr uint64_t max_items = 64;
  static constexpr uint64_t max_bytes = 32ull * 1024 * 1024;
  static constexpr uint64_t max_objects = 64;
  static constexpr uint64_t max_entry_bytes = 16ull * 1024 * 1024;

  struct statistics {
    bool closed = false;
    uint64_t accepted = 0, completed = 0, dropped = 0, failed = 0;
    uint64_t items = 0, bytes = 0, objects = 0;
    uint64_t peak_items = 0, peak_bytes = 0, peak_objects = 0;
    uint64_t known_payload_bytes = 0, opaque_reservation_bytes = 0;
  };

  bounded_persistence() = default;
  bounded_persistence(const bounded_persistence &) = delete;
  ~bounded_persistence() { shutdown(); }

  // Never execute a rejected write on the producer thread. Both queued and
  // executing work count against every limit until their captures are released.
  bool try_submit(post_completion_work work) noexcept {
    if (!work)
      return false;
    std::unique_lock<dxmt::mutex> lock(mutex_);
    if (closed_ || work.charged_bytes > max_entry_bytes ||
        stats_.items >= max_items || work.charged_bytes > max_bytes - stats_.bytes ||
        work.retained_objects > max_objects - stats_.objects) {
      stats_.dropped++;
      work.trace.emit(85, 1, work.trace.owner, work.charged_bytes);
      return false;
    }
    try {
      if (!worker_.joinable())
        worker_ = dxmt::thread([this] { worker_func(); });
      queue_.push(std::move(work));
    } catch (...) {
      stats_.dropped++;
      work.trace.emit(85, 1, work.trace.owner, work.charged_bytes);
      return false;
    }
    const auto &queued = queue_.back();
    queued.trace.emit(85, 0, queued.trace.owner, queued.charged_bytes);
    stats_.accepted++;
    stats_.items++;
    stats_.bytes += queued.charged_bytes;
    stats_.objects += queued.retained_objects;
    if (queued.kind == post_completion_work::cost_kind::known_payload)
      stats_.known_payload_bytes += queued.charged_bytes;
    else
      stats_.opaque_reservation_bytes += queued.charged_bytes;
    stats_.peak_items = std::max(stats_.peak_items, stats_.items);
    stats_.peak_bytes = std::max(stats_.peak_bytes, stats_.bytes);
    stats_.peak_objects = std::max(stats_.peak_objects, stats_.objects);
    lock.unlock();
    cond_.notify_one();
    return true;
  }

  statistics snapshot() {
    std::lock_guard<dxmt::mutex> lock(mutex_);
    return stats_;
  }

  // The owner calls this only after its producers have joined. Work currently
  // executing is joined; queued optional writes are discarded, never replayed.
  // Callers must keep this object alive throughout admission/snapshot calls.
  void shutdown() {
    std::queue<post_completion_work> discarded;
    {
      std::lock_guard<dxmt::mutex> lock(mutex_);
      closed_ = true;
      stats_.closed = true;
      discarded.swap(queue_);
    }
    // Capture destruction can release framework objects: keep it outside the
    // admission mutex, including on shutdown and on the worker completion path.
    cond_.notify_all();
    while (!discarded.empty()) {
      const auto bytes = discarded.front().charged_bytes;
      const auto objects = discarded.front().retained_objects;
      const auto kind = discarded.front().kind;
      discarded.pop();
      std::lock_guard<dxmt::mutex> lock(mutex_);
      stats_.items--;
      stats_.bytes -= bytes;
      stats_.objects -= objects;
      release_cost(bytes, kind);
      stats_.dropped++;
    }
    if (worker_.joinable())
      worker_.join();
  }

private:
  // Caller holds mutex_.
  void release_cost(uint64_t bytes, post_completion_work::cost_kind kind) {
    if (kind == post_completion_work::cost_kind::known_payload)
      stats_.known_payload_bytes -= bytes;
    else
      stats_.opaque_reservation_bytes -= bytes;
  }

  void worker_func() {
    for (;;) {
      post_completion_work work;
      {
        std::unique_lock<dxmt::mutex> lock(mutex_);
        cond_.wait(lock, [this] { return closed_ || !queue_.empty(); });
        if (closed_)
          return;
        work = std::move(queue_.front());
        queue_.pop();
      }
      bool failed = false;
      const auto trace_start = work.trace.now();
      const auto previous_trace = active_persistence_trace;
      active_persistence_trace = work.trace;
      try {
        work.run();
      } catch (...) {
        // Persistence is optional; one failing write cannot kill the compiler
        // pool, the persistence worker, or readiness already published.
        failed = true;
      }
      active_persistence_trace = previous_trace;
      work.trace.emit(86, failed ? 1 : 0, work.trace.owner, work.charged_bytes, trace_start, work.trace.now());
      work.run = {};
      {
        std::lock_guard<dxmt::mutex> lock(mutex_);
        stats_.items--;
        stats_.bytes -= work.charged_bytes;
        stats_.objects -= work.retained_objects;
        release_cost(work.charged_bytes, work.kind);
        stats_.completed++;
        stats_.failed += failed;
      }
    }
  }

  dxmt::mutex mutex_;
  dxmt::condition_variable cond_;
  std::queue<post_completion_work> queue_;
  statistics stats_;
  bool closed_ = false;
  dxmt::thread worker_;
};

} // namespace dxmt
