#pragma once
#include <atomic>
#include <cstdint>
#include <memory>

namespace dxmt {

class CpuFence {
public:
  // Related fences keep distinct completion values, but share cancellation
  // and the exact atomic observed by every waiter.
  struct WaitState {
    std::atomic<uint64_t> epoch{0};
    std::atomic_bool canceled{false};
    void notify() noexcept {
      epoch.fetch_add(1, std::memory_order_release);
      epoch.notify_all();
    }
    void cancel() noexcept {
      canceled.store(true, std::memory_order_release);
      notify();
    }
  };

  bool wait(uint64_t value) {
    for (;;) {
      const auto epoch = wake_->epoch.load(std::memory_order_acquire);
      if (wake_->canceled.load(std::memory_order_acquire))
        return false;
      if (value_.load(std::memory_order_acquire) >= value)
        return true;
      wake_->epoch.wait(epoch, std::memory_order_acquire);
    }
  }

  void signal(uint64_t value) {
    auto current = value_.load(std::memory_order_relaxed);
    do {
      if (value <= current)
        return;
    } while (!value_.compare_exchange_weak(current, value, std::memory_order_release, std::memory_order_relaxed));
    wake_->notify();
  }

  uint64_t signaledValue() { return value_.load(std::memory_order_acquire); }
  void cancel() noexcept { wake_->cancel(); }

  CpuFence(): CpuFence(0) {}
  CpuFence(uint64_t initial_value): value_(initial_value), wake_(std::make_shared<WaitState>()) {}
  explicit CpuFence(std::shared_ptr<WaitState> wake): value_(0), wake_(std::move(wake)) {}

private:
  std::atomic<uint64_t> value_;
  std::shared_ptr<WaitState> wake_;
};
} // namespace dxmt
