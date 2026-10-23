#pragma once

#include "thread.hpp"
#include "util_win32_compat.h"
#include "dxmt_persistence.hpp"
#include <algorithm>
#include <atomic>
#include <functional>
#include <queue>
#include <unordered_map>

namespace dxmt {

template <typename Task> struct task_trait {
  Task run_task(Task task);
  bool get_done(Task task);
  void set_done(Task task);
  post_completion_work take_post_completion(Task task);
};

template <typename Task> class task_scheduler {
public:
  void submit(Task task);

  explicit task_scheduler(bool backlog_growth = false);
  ~task_scheduler();

  uint64_t
  get_running_threads() {
    return running.load(std::memory_order_relaxed);
  }

  bounded_persistence::statistics get_persistence_statistics() { return persistence_.snapshot(); }

private:
  static uint64_t new_trace_owner() {
    if constexpr (requires { task_trait<Task>::new_trace_owner(); }) return task_trait<Task>::new_trace_owner();
    return 0;
  }
  work_trace trace_for(Task task, bool initialize = false) {
    if constexpr (requires { task_trait<Task>{}.trace(task, trace_owner_, initialize); })
      return task_trait<Task>{}.trace(task, trace_owner_, initialize);
    return {};
  }
  const uint64_t trace_owner_ = new_trace_owner();
  void worker_func();
  void grow_locked();

  dxmt::mutex worker_mutex_;
  dxmt::condition_variable worker_cond_;
  std::queue<Task> task_queue_;
  std::queue<Task> task_continuation_queue_;

  dxmt::mutex deps_mutex_;
  std::unordered_multimap<Task, Task> task_continuation_;

  std::vector<dxmt::thread> workers_;

  std::atomic_bool destroyed = false;
  std::atomic_uint64_t running = 0;
  uint64_t threads;
  uint64_t max_threads;
  const bool backlog_growth_;
  bounded_persistence persistence_;
};

template <typename Task> task_scheduler<Task>::task_scheduler(bool backlog_growth)
    : backlog_growth_(backlog_growth) {
  max_threads = std::max<uint64_t>(2, uint64_t(dxmt::thread::hardware_concurrency()) * 2);
  workers_.reserve(max_threads);
  threads = 2;

  for (unsigned i = 0; i < threads; i++) {
    workers_.emplace_back([this]() { worker_func(); });
  }
}

template <typename Task> task_scheduler<Task>::~task_scheduler() {
  // Publish the wait predicate under the same mutex used by the waiter.
  // An atomic flag alone does not prevent notify_all from preceding wait.
  {
    std::unique_lock<dxmt::mutex> lock(worker_mutex_);
    destroyed.store(true);
  }
  worker_cond_.notify_all();

  for (auto &worker : workers_) {
    if (worker.joinable())
      worker.join();
  }

  workers_.clear();
  // Compile workers have stopped producing detached writes. Cancel optional
  // queued writes and join the active write before task/cache dependencies die.
  persistence_.shutdown();
}

template <typename Task>
void task_scheduler<Task>::grow_locked() {
  // Called only under worker_mutex_. No vector mutation is allowed after close:
  // the destructor then iterates and joins workers_ without this mutex.
  if (destroyed.load())
    return;
  const auto runnable = task_queue_.size() + task_continuation_queue_.size();
  auto idle = threads - running.load(std::memory_order_relaxed);
  if (backlog_growth_) {
    while (runnable > idle && threads < max_threads) {
      try {
        workers_.emplace_back([this] { worker_func(); });
      } catch (...) {
        // Existing workers can drain the queue if an optional expansion fails.
        break;
      }
      threads++;
      idle++;
    }
  } else if (!idle && threads < max_threads) {
    try {
      workers_.emplace_back([this] { worker_func(); });
      threads++;
    } catch (...) {
    }
  }
}

template <typename Task>
void
task_scheduler<Task>::worker_func() {
  struct task_trait<Task> task_trait;
  std::vector<Task> continuation_buffer;
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
  while (!destroyed.load()) {
    Task task;
    work_trace trace;
    uint32_t observed_workers = 0;
    uint64_t observed_depth = 0;
    {
      std::unique_lock<dxmt::mutex> lock(worker_mutex_);

      if (task_queue_.empty() && task_continuation_queue_.empty()) {
        worker_cond_.wait(lock, [this]() {
          return task_queue_.size() || task_continuation_queue_.size() || destroyed.load();
        });
      }

      if (destroyed.load())
        break;

      if (!task_continuation_queue_.empty()) {
        task = task_continuation_queue_.front();
        task_continuation_queue_.pop();
      } else if (!task_queue_.empty()) {
        task = task_queue_.front();
        task_queue_.pop();
      } else {
        break;
      }
      // Reserve this worker under the same lock as queue admission/growth.
      running.fetch_add(1, std::memory_order_relaxed);
      trace = trace_for(task);
      observed_workers = uint32_t(threads);
      observed_depth = task_queue_.size() + task_continuation_queue_.size();
    }
    while (true) {
      trace.emit(81, observed_workers, trace_owner_, observed_depth);
      Task continuation = task_trait.run_task(task);
      if (continuation == task) {
        // Detach ownership before ready publication can let consumers retire
        // the task. The returned work must never retain a raw task pointer.
        post_completion_work post_completion;
        try {
          post_completion = task_trait.take_post_completion(task);
        } catch (...) {
          // Even failure to allocate an optional write must publish readiness.
        }
        {
          std::unique_lock<dxmt::mutex> lock(deps_mutex_);
          auto range = task_continuation_.equal_range(continuation);
          for (auto itr = range.first; itr != range.second; ++itr) {
            auto dependent = trace_for(itr->second);
            dependent.emit(83, 0, trace.serial, trace_owner_);
            continuation_buffer.push_back(itr->second);
          }
          task_continuation_.erase(range.first, range.second);
          trace.emit(84, 0, trace_owner_, 0);
          task_trait.set_done(continuation);
        }
        // Admission is bounded and does not execute I/O or wait for capacity.
        // The callable owns its state independently of ready consumers.
        if (post_completion)
          post_completion.trace = trace;
        if (post_completion)
          persistence_.try_submit(std::move(post_completion));
      } else {
        std::unique_lock<dxmt::mutex> lock(deps_mutex_);
        // spurious dependency
        auto dependency = trace_for(continuation);
        trace.emit(82, 0, dependency.serial, trace_owner_);
        if (task_trait.get_done(continuation)) {
          trace.emit(83, 0, dependency.serial, trace_owner_);
          continue;
        }
        task_continuation_.insert({continuation, task});
      }
      break;
    }
    {
      std::unique_lock<dxmt::mutex> lock(worker_mutex_);
      for (auto &continuation : continuation_buffer)
        task_continuation_queue_.push(continuation);
      continuation_buffer.clear();
      running.fetch_sub(1, std::memory_order_relaxed);
      if (backlog_growth_)
        grow_locked();
    }
    worker_cond_.notify_all();
  }
};

template <typename Task>
void
task_scheduler<Task>::submit(Task task) {
  std::unique_lock<dxmt::mutex> lock(worker_mutex_);
  // Shutdown cancels queued work; it only joins tasks already executing.
  // Callers must still keep the scheduler alive for the duration of submit.
  if (destroyed.load())
    return;
  task_queue_.push(task);

  grow_locked();
  auto trace = trace_for(task, true);
  trace.emit(80, uint32_t(threads), trace_owner_, task_queue_.size() + task_continuation_queue_.size());

  if (backlog_growth_)
    worker_cond_.notify_all();
  else
    worker_cond_.notify_one();
}

}; // namespace dxmt
