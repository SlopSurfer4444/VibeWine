#include "mocks.hpp"
#include "dxmt_tasks.hpp"
#include "dxmt_compiler_options.hpp"
#include <array>
#include <iostream>
#include <limits>

struct Task {
  std::atomic<bool> done{false};
  std::atomic<unsigned> runs{0};
  std::function<Task *()> run;
};
namespace dxmt {
template <> struct task_trait<Task *> {
  Task *run_task(Task *task) { task->runs++; return task->run(); }
  bool get_done(Task *task) { return task->done.load(); }
  void set_done(Task *task) { task->done = true; }
  post_completion_work take_post_completion(Task *) { return {}; }
};
}
using Scheduler = dxmt::task_scheduler<Task *>;

void saturation(bool backlog, bool demand, uint64_t cap) {
  // The shared platform shim reports four hardware threads: legacy max is 8.
  const unsigned expected = cap ? std::min<uint64_t>(cap, 8) : 8;
  created_threads = 0;
  std::array<Task, 16> tasks;
  std::atomic<unsigned> entered{0}, active{0}, peak{0};
  std::atomic<bool> release{false};
  Scheduler scheduler(backlog, demand, cap);
  assert(created_threads == std::min(2u, expected));
  for (auto &task : tasks)
    task.run = [&, pointer = &task] {
      const auto count = ++active;
      auto previous = peak.load();
      while (count > previous && !peak.compare_exchange_weak(previous, count)) {}
      entered++;
      until([&] { return release.load(); });
      active--;
      return pointer;
    };
  // Force real running saturation, also exercising legacy growth on submission.
  for (unsigned i = 0; i < expected; ++i) {
    scheduler.submit(&tasks[i]);
    until([&] { return entered == i + 1; });
  }
  for (unsigned i = expected; i < tasks.size(); ++i)
    scheduler.submit(&tasks[i]);
  assert(created_threads == expected && active == expected && peak == expected);
  assert(scheduler.get_running_threads() == expected);
  release = true;
  until([&] { for (auto &task : tasks) if (!task.done) return false; return true; });
  assert(created_threads == expected && peak == expected);
  for (auto &task : tasks) assert(task.runs == 1);
}

void serial_dependencies(bool backlog, bool demand) {
  Task parent, middle, leaf, other;
  parent.run = [&] { return middle.done ? &parent : &middle; };
  middle.run = [&] { return leaf.done ? &middle : &leaf; };
  leaf.run = [&] { return &leaf; };
  other.run = [&] { return &other; };
  created_threads = 0;
  Scheduler scheduler(backlog, demand, 1);
  // Each blocked dependency must release the sole worker. The dependency is
  // admitted later, so a blocked-task spin cannot accidentally pass the test.
  scheduler.submit(&parent, dxmt::task_urgency::demand);
  until([&] { return parent.runs == 1 && scheduler.get_running_threads() == 0; });
  scheduler.submit(&middle, dxmt::task_urgency::speculative);
  until([&] { return middle.runs == 1 && scheduler.get_running_threads() == 0; });
  scheduler.submit(&other);
  scheduler.submit(&leaf, dxmt::task_urgency::speculative);
  until([&] { return parent.done && middle.done && leaf.done && other.done; });
  assert(parent.runs == 2 && middle.runs == 2 && leaf.runs == 1 && other.runs == 1);
  assert(created_threads == 1);
}

void shutdown(bool backlog, bool demand, unsigned cap) {
  scheduler_close_observed = false;
  created_threads = 0;
  std::array<Task, 3> active;
  Task queued;
  std::atomic<unsigned> entered{0};
  std::atomic<bool> release{false}, stopped{false};
  auto scheduler = std::make_unique<Scheduler>(backlog, demand, cap);
  for (unsigned i = 0; i < cap; ++i) {
    active[i].run = [&, pointer = &active[i]] {
      entered++;
      until([&] { return release.load(); });
      return pointer;
    };
    scheduler->submit(&active[i]);
    until([&] { return entered == i + 1; });
  }
  queued.run = [&] { return &queued; };
  scheduler->submit(&queued);
  std::thread stop([&] {
    destroying_scheduler = true;
    scheduler.reset();
    stopped = true;
  });
  until([&] { return scheduler_close_observed.load(); });
  assert(!stopped && queued.runs == 0 && created_threads == cap);
  release = true;
  stop.join();
  assert(stopped && queued.runs == 0 && !queued.done && created_threads == cap);
  for (unsigned i = 0; i < cap; ++i) assert(active[i].done && active[i].runs == 1);
}

void config_values() {
  for (auto value : {"", "0", "-1", "+1", " 1", "1 ", "1x", "1.5", "-", "true",
                     "18446744073709551616", "9999999999999999999999999999999999999999"})
    assert(dxmt::parse_compiler_worker_limit(value) == 0);
  assert(dxmt::parse_compiler_worker_limit("1") == 1);
  assert(dxmt::parse_compiler_worker_limit("0003") == 3);
  assert(dxmt::parse_compiler_worker_limit("4294967296") == 4294967296ull);
  assert(dxmt::parse_compiler_worker_limit("18446744073709551615") == std::numeric_limits<uint64_t>::max());
}

int main() {
  config_values();
  for (bool backlog : {false, true}) {
    for (bool demand : {false, true}) {
      for (uint64_t cap : {0ull, 1ull, 2ull, 3ull, 8ull, 9ull, 18446744073709551615ull})
        saturation(backlog, demand, cap);
      serial_dependencies(backlog, demand);
      shutdown(backlog, demand, 1);
      shutdown(backlog, demand, 3);
    }
  }
  std::cout << "PASS production scheduler: actual worker/task saturation caps, zero legacy policy, "
               "all growth/demand modes, serial dependency continuations, shutdown cancellation/join; "
               "strict malformed/overflow config parsing\n";
}
