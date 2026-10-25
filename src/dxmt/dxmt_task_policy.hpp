// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright 2026 VibeWine contributors
// VibeWine demand scheduling policy. Inspired by DXVK's urgency queues
// (doitsujin/dxvk cbd2206d72d3d9dd9941d5d868bb03a858e85f9b), but implemented
// for DXMT's dependency/ready-publication protocol; no DXVK source is copied.
#pragma once

#include <array>
#include <cstddef>
#include <iterator>
#include <list>
#include <unordered_map>

namespace dxmt {

enum class task_urgency { demand, normal, speculative };

// The scheduler owns the lock and all task lifetimes remain with their existing
// owners. Each admitted task has exactly one state and at most one queue node.
// Completed tasks leave this index before readiness is published; promotion
// only consults the index, so it never dereferences a completed task pointer.
template <typename Task> class demand_task_policy {
  enum class phase { unsubmitted, queued, running, blocked };
  struct state {
    task_urgency urgency;
    phase status = phase::unsubmitted;
    typename std::list<Task>::iterator position;
    Task dependency{};
  };
  std::array<std::list<Task>, 3> queues_;
  std::unordered_map<Task, state> tasks_;
  std::unordered_multimap<Task, Task> continuations_;
  unsigned demand_burst_ = 0;
  unsigned next_lower_ = 1;

  static size_t index(task_urgency urgency) { return size_t(urgency); }

  void raise(Task task, task_urgency urgency) {
    // A bounded walk also tolerates an invalid cyclic dependency graph without
    // turning a promotion into an infinite loop. Execution retains the existing
    // dependency contract; the policy does not invent a result for a cycle.
    for (size_t left = tasks_.size(); left; --left) {
      auto found = tasks_.find(task);
      if (found == tasks_.end())
        break;
      auto &entry = found->second;
      if (index(urgency) < index(entry.urgency)) {
        if (entry.status == phase::queued)
          queues_[index(urgency)].splice(queues_[index(urgency)].end(),
              queues_[index(entry.urgency)], entry.position);
        entry.urgency = urgency;
      }
      urgency = entry.urgency;
      if (entry.status != phase::blocked)
        break;
      task = entry.dependency;
    }
  }

public:
  static constexpr unsigned max_demand_burst = 8;

  bool admit(Task task, task_urgency urgency) {
    auto found = tasks_.try_emplace(task, state{urgency}).first;
    auto &entry = found->second;
    if (entry.status != phase::unsubmitted) {
      raise(task, urgency);
      return false;
    }
    if (index(urgency) < index(entry.urgency))
      entry.urgency = urgency;
    auto &queue = queues_[index(entry.urgency)];
    queue.push_back(task);
    entry.position = std::prev(queue.end());
    entry.status = phase::queued;
    return true;
  }

  void promote(Task task) { raise(task, task_urgency::demand); }

  size_t runnable() const {
    return queues_[0].size() + queues_[1].size() + queues_[2].size();
  }

  Task take() {
    unsigned selected = 0;
    const bool lower = !queues_[1].empty() || !queues_[2].empty();
    if (queues_[0].empty() || (lower && demand_burst_ == max_demand_burst)) {
      selected = !queues_[next_lower_].empty() ? next_lower_ : 3 - next_lower_;
      next_lower_ = 3 - selected;
      demand_burst_ = 0;
    } else if (demand_burst_ < max_demand_burst) {
      ++demand_burst_;
    }
    auto &queue = queues_[selected];
    Task task = queue.front();
    queue.pop_front();
    tasks_.find(task)->second.status = phase::running;
    return task;
  }

  // Only call after checking the returned dependency is not done while holding
  // the scheduler lock. Remember inherited urgency even before its own submit.
  void block(Task task, Task dependency) {
    auto &entry = tasks_.find(task)->second;
    tasks_.try_emplace(dependency, state{entry.urgency});
    continuations_.emplace(dependency, task);
    entry.dependency = dependency;
    entry.status = phase::blocked;
    raise(dependency, entry.urgency);
  }

  template <typename Resume> void complete(Task task, Resume resume) {
    auto range = continuations_.equal_range(task);
    for (auto iter = range.first; iter != range.second; ++iter) {
      Task dependent = iter->second;
      auto &entry = tasks_.find(dependent)->second;
      resume(dependent);
      auto &queue = queues_[index(entry.urgency)];
      queue.push_back(dependent);
      entry.position = std::prev(queue.end());
      entry.dependency = Task{};
      entry.status = phase::queued;
    }
    continuations_.erase(range.first, range.second);
    tasks_.erase(task);
  }
};

} // namespace dxmt
