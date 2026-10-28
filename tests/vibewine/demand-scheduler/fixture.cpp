#define until shared_until
#include "mocks.hpp"
#undef until
#include "dxmt_tasks.hpp"
#include <array>
#include <iostream>
#include <variant>
#include <type_traits>
#include "d3d11_pso_normalization.hpp"

// Preserve the shared four-second bound, adding the caller for diagnostics.
void until(std::function<bool()> predicate, const char *file = __builtin_FILE(),
    unsigned line = __builtin_LINE()) {
  const auto end = std::chrono::steady_clock::now() + 4s;
  while (!predicate()) {
    if (std::chrono::steady_clock::now() >= end) {
      std::cerr << "predicate deadline at " << file << ':' << line << '\n';
      std::abort();
    }
    std::this_thread::yield();
  }
}

// Every task is owned until completion, except the explicit ready-publication
// retirement test. Dependency pointers remain alive through all users.
struct Task {
  std::atomic<bool> done{false};
  std::atomic<unsigned> runs{0}, trace_initializations{0};
  std::function<Task *()> run;
  std::function<void()> on_ready;
  dxmt::post_completion_work post;
  bool compile_failed = false;
  uint64_t serial = 0;
};
namespace dxmt {
template <> struct task_trait<Task *> {
  static uint64_t new_trace_owner() { return 71; }
  work_trace trace(Task *task, uint64_t owner, bool initialize) {
    if (initialize) assert(task->trace_initializations.fetch_add(1) == 0);
    return {1, task->serial, owner};
  }
  Task *run_task(Task *task) { task->runs++; return task->run(); }
  bool get_done(Task *task) { return task->done.load(); }
  void set_done(Task *task) {
    auto callback = std::move(task->on_ready);
    task->done = true;
    if (callback) callback(); // May delete task: no following task dereference.
  }
  post_completion_work take_post_completion(Task *task) {
    auto result = std::move(task->post);
    // A moved-from std::function can remain populated on x86_64 libc++; model
    // the production compiler's explicit detachment of task-owned payloads.
    task->post = {};
    return result;
  }
};
}
using Urgency = dxmt::task_urgency;
using Scheduler = dxmt::task_scheduler<Task *>;

struct Saturated {
  std::array<Task, 8> blockers;
  std::array<std::atomic<bool>, 8> release{};
  std::atomic<unsigned> entered{0};
  std::unique_ptr<Scheduler> scheduler;
  Saturated(bool backlog, bool demand) {
    start_threads = true;
    created_threads = 0;
    scheduler = std::make_unique<Scheduler>(backlog, demand);
    for (unsigned i = 0; i != blockers.size(); ++i) {
      blockers[i].serial = i + 1;
      blockers[i].run = [&, i] {
        entered++;
        until([&] { return release[i].load(); });
        return &blockers[i];
      };
      scheduler->submit(&blockers[i], Urgency::speculative);
      until([&] { return entered == i + 1; });
    }
    assert(created_threads == 8 && scheduler->get_running_threads() == 8);
  }
  void finish() {
    for (auto &gate : release) gate = true;
    scheduler.reset();
  }
  ~Saturated() { finish(); }
};

namespace dxmt {
using CompiledShader = ::Task;
#include "pixel_variant.inc"
using ShaderVariant = std::variant<unsigned, ShaderVariantPixel>;
// The scheduler fixture hashes only its two alternatives; production hash
// semantics are exercised separately by pso-normalization. Equality comes from
// the actual extracted pixel key, including its meaningful specialization bits.
struct FixtureVariantHash {
  size_t operator()(const ShaderVariant &variant) const {
    return std::visit([](const auto &key) -> size_t {
      if constexpr (std::is_same_v<std::decay_t<decltype(key)>, unsigned>)
        return key;
      else
        return size_t(key.sample_mask) ^ (size_t(key.unorm_output_reg_mask) << 1) ^
               (size_t(key.dual_source_blending) << 2) ^ (size_t(key.disable_depth_output) << 3);
    }, variant);
  }
};
inline std::function<Task *(Task *)> lookup_run;
template <typename Owner, typename Variant>
std::unique_ptr<CompiledShader> CreateVariantShader(void *, Owner *, Variant variant) {
  auto result = std::make_unique<Task>();
  if constexpr (std::is_same_v<Variant, unsigned>)
    result->serial = 500 + variant;
  else
    result->serial = 500 + variant.sample_mask;
  result->run = [task = result.get()] { return lookup_run(task); };
  return result;
}
struct LookupCache { Scheduler &scheduler_; void *device = nullptr; };
class CachedLookupFixture {
  LookupCache *cache;
  std::unordered_map<ShaderVariant, std::unique_ptr<CompiledShader>, FixtureVariantHash> variants;
  MTL_SHADER_REFLECTION reflection_{};
  dxmt::mutex variants_mutex;
public:
  explicit CachedLookupFixture(LookupCache *owner) : cache(owner) { reflection_.PixelShader.ValidRenderTargets = 1; }
  // Extracted verbatim from current d3d11_pipeline_cache.cpp by the runner.
#include "cached_lookup.inc"
};
}

void cached_lookup_routing(bool backlog, bool pixel = false) {
  // Bits outside reflected outputs normalize to one identity. The same public
  // demand lookup must promote the queued speculative task, not create another.
  auto variant = [pixel](unsigned id, bool unused_output = false) -> dxmt::ShaderVariant {
    if (pixel) return dxmt::ShaderVariantPixel{id, false, false, unused_output ? 0x81u : 1u};
    return id;
  };
  std::array<unsigned, 3> order{};
  std::atomic<unsigned> admitted{0}, released{0};
  Saturated pool(backlog, true);
  dxmt::LookupCache cache{*pool.scheduler};
  dxmt::CachedLookupFixture shader{&cache};
  dxmt::lookup_run = [&](Task *task) {
    const auto position = admitted.load();
    order[position] = unsigned(task->serial - 500);
    admitted.store(position + 1);
    until([&] { return released > position; });
    return task;
  };
  auto *speculative = shader.get_shader(variant(0, true), Urgency::speculative);
  auto *promoted = shader.get_shader(variant(1, true), Urgency::speculative);
  assert(promoted == shader.get_shader(variant(1)));
  auto *created_demand = shader.get_shader(variant(2));
  for (unsigned n = 0; n != 64; ++n)
    assert(promoted == shader.get_shader(variant(1, n % 2 != 0)));
  pool.release[0] = true;
  for (unsigned n = 1; n <= 3; ++n) {
    until([&] { return admitted == n; });
    assert(order[n - 1] == (n == 1 ? 1u : n == 2 ? 2u : 0u));
    released = n;
  }
  until([&] { return speculative->done && promoted->done && created_demand->done; });
  for (auto *task : {speculative, promoted, created_demand})
    assert(task->runs == 1 && task->trace_initializations == 1);
  assert(promoted == shader.get_shader(variant(1)));
  pool.finish();
  dxmt::lookup_run = {};
}

void saturation_queue_bypass(bool backlog, bool demand, bool promote) {
  std::array<Task, 5> queued;
  std::atomic<int> first{-1};
  std::atomic<bool> release{false};
  Saturated pool(backlog, demand);
  for (unsigned i = 0; i != queued.size(); ++i) {
    queued[i].serial = i + 20;
    queued[i].run = [&, i] {
      int empty = -1;
      first.compare_exchange_strong(empty, i);
      until([&] { return release.load(); });
      return &queued[i];
    };
    pool.scheduler->submit(&queued[i], !promote && i == 4 ? Urgency::demand : Urgency::speculative);
  }
  if (promote) {
    std::array<std::thread, 4> promoters;
    for (auto &thread : promoters)
      thread = std::thread([&] { for (unsigned n = 0; n != 128; ++n) pool.scheduler->promote(&queued[4]); });
    for (auto &thread : promoters) thread.join();
  }
  pool.release[0] = true;
  until([&] { return first >= 0; });
  assert(first == (demand ? 4 : 0));
  assert(created_threads == 8); // No growth or reserved worker advantage.
  release = true;
  until([&] { for (auto &task : queued) if (!task.done) return false; return true; });
  for (auto &task : queued) assert(task.runs == 1 && task.trace_initializations == 1);
  pool.finish();
}

void running_promotion(bool backlog) {
  Task parent, dependency, filler;
  std::atomic<bool> parent_entered{false}, parent_return{false}, release{false};
  std::atomic<int> first{-1};
  Saturated pool(backlog, true);
  parent.serial = 41; dependency.serial = 42; filler.serial = 43;
  parent.run = [&] {
    if (dependency.done) return &parent;
    parent_entered = true;
    until([&] { return parent_return.load(); });
    return &dependency;
  };
  auto leaf = [&](Task &task, int id) {
    task.run = [&, id, pointer = &task] {
      int empty = -1; first.compare_exchange_strong(empty, id);
      until([&] { return release.load(); });
      return pointer;
    };
  };
  leaf(filler, 0); leaf(dependency, 1);
  pool.scheduler->submit(&parent, Urgency::speculative);
  pool.release[0] = true;
  until([&] { return parent_entered.load(); });
  pool.scheduler->submit(&filler, Urgency::speculative);
  pool.scheduler->submit(&dependency, Urgency::speculative);
  pool.scheduler->promote(&parent);
  parent_return = true;
  until([&] { return first >= 0; });
  assert(first == 1); // Promotion during Run propagates when it reports a dependency.
  release = true;
  until([&] { return parent.done && dependency.done && filler.done; });
  assert(parent.runs == 2 && dependency.runs == 1 && filler.runs == 1);
  assert(parent.trace_initializations == 1 && dependency.trace_initializations == 1);
  pool.finish();
}

void blocked_late_dependency(bool backlog) {
  Task parent, keeper, dependency, filler;
  std::atomic<bool> attempted{false}, keeper_entered{false}, release_keeper{false}, release{false};
  std::atomic<int> first{-1};
  Saturated pool(backlog, true);
  parent.serial = 51; keeper.serial = 52; dependency.serial = 53; filler.serial = 54;
  parent.run = [&] { attempted = true; return dependency.done ? &parent : &dependency; };
  keeper.run = [&] {
    keeper_entered = true;
    until([&] { return release_keeper.load(); });
    return &keeper;
  };
  auto leaf = [&](Task &task, int id) {
    task.run = [&, id, pointer = &task] {
      int empty = -1; first.compare_exchange_strong(empty, id);
      until([&] { return release.load(); });
      return pointer;
    };
  };
  leaf(filler, 0); leaf(dependency, 1);
  pool.scheduler->submit(&parent, Urgency::speculative);
  pool.release[0] = true;
  until([&] { return attempted && pool.scheduler->get_running_threads() == 7; });
  pool.scheduler->submit(&keeper, Urgency::normal);
  until([&] { return keeper_entered.load(); });
  pool.scheduler->promote(&parent);
  pool.scheduler->submit(&filler, Urgency::speculative);
  pool.scheduler->submit(&dependency, Urgency::speculative);
  release_keeper = true;
  until([&] { return first >= 0; });
  assert(first == 1); // Missing-at-promotion dependency inherits urgency on submit.
  release = true;
  until([&] { return parent.done && keeper.done && dependency.done && filler.done; });
  assert(parent.runs == 2 && dependency.runs == 1 && filler.runs == 1);
  assert(parent.trace_initializations == 1 && dependency.trace_initializations == 1);
  pool.finish();
}

void fairness(bool backlog) {
  std::array<Task, 76> tasks;
  std::array<int, 76> order{};
  std::atomic<unsigned> admitted{0}, released{0};
  Saturated pool(backlog, true);
  // Sustain demand with new arrivals while lower classes are pending.
  for (unsigned i = 0; i != tasks.size(); ++i) {
    const int category = i < 36 || i >= 40 ? 0 : (i < 38 ? 1 : 2);
    tasks[i].serial = 100 + i;
    tasks[i].run = [&, i, category] {
      const auto position = admitted.load();
      order[position] = category;
      admitted.store(position + 1);
      until([&] { return released > position; });
      return &tasks[i];
    };
    if (i < 40)
      pool.scheduler->submit(&tasks[i], category == 0 ? Urgency::demand : category == 1 ? Urgency::normal : Urgency::speculative);
  }
  pool.release[0] = true;
  for (unsigned n = 1; n <= 40; ++n) {
    until([&] { return admitted == n; });
    if (n <= 36 && n % 9) assert(order[n - 1] == 0);
    if (n % 9 == 0 && n <= 36) assert(order[n - 1] != 0);
    if (n == 18) assert(order[8] != order[17]);
    if (n <= 36) pool.scheduler->submit(&tasks[39 + n], Urgency::demand);
    released = n;
  }
  released = tasks.size();
  until([&] { for (auto &task : tasks) if (!task.done) return false; return true; });
  for (auto &task : tasks) assert(task.runs == 1 && task.trace_initializations == 1);
  pool.finish();
}

void completion_race_and_retirement(bool backlog) {
  Task dependency, parent;
  std::atomic<bool> dependency_entered{false}, release_dependency{false};
  std::atomic<bool> parent_entered{false}, release_parent{false};
  Scheduler scheduler(backlog, true);
  dependency.run = [&] {
    dependency_entered = true;
    until([&] { return release_dependency.load(); });
    return &dependency;
  };
  parent.run = [&] {
    if (dependency.done) return &parent;
    parent_entered = true;
    until([&] { return release_parent.load(); });
    return &dependency;
  };
  scheduler.submit(&dependency, Urgency::speculative);
  until([&] { return dependency_entered.load(); });
  scheduler.submit(&parent, Urgency::normal);
  until([&] { return parent_entered.load(); });
  std::thread promote([&] {
    for (unsigned i = 0; i != 256; ++i) { scheduler.promote(&parent); scheduler.promote(&dependency); }
  });
  release_dependency = true;
  until([&] { return dependency.done.load(); });
  release_parent = true;
  promote.join();
  until([&] { return parent.done.load(); });
  assert(parent.runs == 2 && dependency.runs == 1);
  assert(parent.trace_initializations == 1 && dependency.trace_initializations == 1);
  // A live done cache entry must not be admitted again or reset trace identity.
  scheduler.submit(&dependency, Urgency::demand);
  assert(dependency.runs == 1 && dependency.trace_initializations == 1);

  std::atomic<bool> retired{false}, optional_ran{false};
  auto *task = new Task;
  task->run = [task] { return task; };
  task->post = {[&] { optional_ran = true; }, 0, 0};
  task->on_ready = [&, task] { delete task; retired = true; };
  scheduler.submit(task, Urgency::speculative);
  until([&] { return retired.load(); });
  // The address is intentionally dangling; promote must look up scheduler-owned
  // state, do no task read, and never recreate a completed queue entry.
  for (unsigned i = 0; i != 256; ++i) scheduler.promote(task);
  until([&] { return optional_ran.load(); });
  assert(scheduler.get_persistence_statistics().accepted == 1);
}

void errors_optional_io_and_shutdown(bool backlog, bool demand) {
  Task failed, saving, dependency, parent;
  std::atomic<bool> io_entered{false}, release_io{false}, active{false}, release_active{false};
  std::atomic<bool> parent_attempted{false}, closed{false}, cancelled_write_ran{false};
  failed.run = [&] { failed.compile_failed = true; return &failed; };
  failed.post = {[] { throw 7; }, 0, 0};
  saving.run = [&] { return &saving; };
  saving.post = {[&] { io_entered = true; until([&] { return release_io.load(); }); }, 0, 0};
  dependency.run = [&] { active = true; until([&] { return release_active.load(); }); return &dependency; };
  auto payload = std::make_shared<int>(1);
  std::weak_ptr<int> weak = payload;
  dependency.post = {[payload, &cancelled_write_ran] { cancelled_write_ran = true; }, 4, 1};
  payload.reset();
  parent.run = [&] { parent_attempted = true; return dependency.done ? &parent : &dependency; };
  auto scheduler = std::make_unique<Scheduler>(backlog, demand);
  scheduler->submit(&failed, Urgency::demand);
  until([&] { return failed.done && scheduler->get_persistence_statistics().failed == 1; });
  assert(failed.compile_failed && failed.runs == 1);
  scheduler->submit(&saving);
  until([&] { return io_entered.load(); });
  scheduler->submit(&dependency);
  until([&] { return active.load(); });
  scheduler->submit(&parent, Urgency::demand);
  until([&] { return parent_attempted && scheduler->get_running_threads() == 1; });
  scheduler_close_observed = false;
  std::thread stop([&] { destroying_scheduler = true; scheduler.reset(); closed = true; });
  until([&] { return scheduler_close_observed.load(); });
  const auto workers_at_close = created_threads.load();
  release_active = true;
  until([&] { return dependency.done && weak.expired(); });
  assert(!closed && !cancelled_write_ran && !parent.done && parent.runs == 1);
  assert(created_threads == workers_at_close);
  release_io = true;
  stop.join();
  assert(closed);
}

void queued_shutdown(bool backlog, bool demand) {
  Task queued;
  queued.run = [&] { return &queued; };
  Saturated pool(backlog, demand);
  pool.scheduler->submit(&queued, Urgency::demand);
  scheduler_close_observed = false;
  std::atomic<bool> closed{false};
  std::thread stop([&] { destroying_scheduler = true; pool.scheduler.reset(); closed = true; });
  until([&] { return scheduler_close_observed.load(); });
  assert(!closed && queued.runs == 0 && !queued.done);
  const auto workers_at_close = created_threads.load();
  for (auto &gate : pool.release) gate = true;
  stop.join();
  assert(closed && queued.runs == 0 && !queued.done);
  assert(created_threads == workers_at_close && queued.trace_initializations == 1);
}

int main() {
  for (bool backlog : {false, true}) {
    cached_lookup_routing(backlog);
    cached_lookup_routing(backlog, true);
    saturation_queue_bypass(backlog, false, false);
    saturation_queue_bypass(backlog, true, false);
    saturation_queue_bypass(backlog, false, true);
    saturation_queue_bypass(backlog, true, true);
    running_promotion(backlog);
    blocked_late_dependency(backlog);
    fairness(backlog);
    completion_race_and_retirement(backlog);
    errors_optional_io_and_shutdown(backlog, false);
    errors_optional_io_and_shutdown(backlog, true);
    queued_shutdown(backlog, false);
    queued_shutdown(backlog, true);
  }
  std::cout << "PASS exact scheduler header: equal-capacity saturation discriminator, queued/running/blocked promotion, "
               "exact cached lookup routing including canonical pixel promotion, late dependency inheritance, bounded fairness, completion race, retired pointer, trace identity, "
               "compile failure readiness, optional I/O failure, shutdown cancellation/join\n";
}
