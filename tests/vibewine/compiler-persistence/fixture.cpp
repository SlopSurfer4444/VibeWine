#include "mocks.hpp"
#include "dxmt_tasks.hpp"
#include "dxmt_shader_cache.hpp"
#include <iostream>

namespace dxmt {
// The production header/state/closures are compiled unchanged. Only native
// cache opening is replaced: this fixture must never open a user's database.
ShaderCache::ShaderCache(WMTMetalVersion) { writer_state_->writer = WMT::CacheWriter{fake_writer}; }
}

struct Task {
  std::atomic<bool> done{false};
  std::function<Task *()> run;
  dxmt::post_completion_work post;
};
namespace dxmt {
template <> struct task_trait<Task *> {
  Task *run_task(Task *task) { return task->run(); }
  bool get_done(Task *task) { return task->done.load(); }
  void set_done(Task *task) { task->done.store(true); }
  post_completion_work take_post_completion(Task *task) { return std::move(task->post); }
};
}

void burst(bool backlog) {
  start_threads = false;
  created_threads = 0;
  Task tasks[8];
  std::atomic<unsigned> entered{0};
  std::atomic<bool> release{false};
  for (auto &task : tasks)
    task.run = [&task, &entered, &release] {
      entered++;
      until([&] { return release.load(); });
      return &task;
    };
  dxmt::task_scheduler<Task *> scheduler(backlog);
  for (auto &task : tasks)
    scheduler.submit(&task);
  assert(created_threads == (backlog ? 8u : 2u));
  start_threads = true;
  until([&] { return entered == (backlog ? 8u : 2u); });
  release = true;
  until([&] { return tasks[7].done.load(); });
}

void dependency_and_optional_io() {
  Task shader, pipeline, unrelated;
  std::atomic<bool> saving{false}, release{false}, pipeline_attempted{false};
  shader.run = [&] { return &shader; };
  shader.post = {[&] { saving = true; until([&] { return release.load(); }); }, 64, 1};
  pipeline.run = [&] {
    pipeline_attempted = true;
    return shader.done.load() ? &pipeline : &shader;
  };
  unrelated.run = [&] { return &unrelated; };
  dxmt::task_scheduler<Task *> scheduler(true);
  scheduler.submit(&pipeline);
  until([&] { return pipeline_attempted.load(); });
  scheduler.submit(&shader);
  until([&] { return saving.load(); });
  // The owned writer is still blocked, while a dependent pipeline and unrelated
  // compilation must both publish readiness without waiting for it.
  scheduler.submit(&unrelated);
  until([&] { return pipeline.done.load() && unrelated.done.load(); });
  release = true;
}

void saturation_and_shutdown() {
  // Account executing payload as well as pending payload, at exact boundaries.
  for (unsigned mode = 0; mode < 3; mode++) {
    dxmt::bounded_persistence writer;
    std::atomic<bool> active{false}, release{false};
    const auto bytes = mode == 1 ? dxmt::bounded_persistence::max_entry_bytes : 0;
    const auto objects = mode == 2 ? dxmt::bounded_persistence::max_objects : 0;
    assert(writer.try_submit({[&] { active = true; until([&] { return release.load(); }); }, bytes, objects}));
    until([&] { return active.load(); });
    if (mode == 0) {
      for (unsigned i = 1; i < dxmt::bounded_persistence::max_items; i++)
        assert(writer.try_submit({[] {}, 0, 0}));
      assert(!writer.try_submit({[] {}, 0, 0}));
    } else if (mode == 1) {
      assert(writer.try_submit({[] {}, bytes, 0}));
      assert(!writer.try_submit({[] {}, 1, 0}));
    } else {
      assert(!writer.try_submit({[] {}, 0, 1}));
    }
    assert(!writer.try_submit({[] {}, dxmt::bounded_persistence::max_entry_bytes + 1, 0}));
    auto token = std::make_shared<int>(1);
    std::weak_ptr<int> weak = token;
    assert(!writer.try_submit({[token] {}, dxmt::bounded_persistence::max_entry_bytes + 1, 0}));
    token.reset();
    assert(weak.expired());
    std::atomic<bool> stopped{false};
    std::thread stop([&] { writer.shutdown(); stopped = true; });
    until([&] { const auto state = writer.snapshot(); return state.closed && state.items == 1; });
    assert(!stopped);
    assert(!writer.try_submit({[] {}, 0, 0}));
    release = true;
    stop.join();
    auto result = writer.snapshot();
    assert(result.items == 0 && result.bytes == 0 && result.objects == 0);
    assert(result.completed == 1 && result.dropped > 0);
  }
}

void detached_cache_ownership_and_errors() {
  fake_writer = std::make_shared<FakeWriterState>();
  auto cache = std::make_unique<dxmt::ShaderCache>(0);
  WMT::DispatchData data{std::make_shared<int>(42)};
  std::weak_ptr<int> weak = data.payload;
  auto first = cache->makeWrite(std::make_pair(17, 23), data, 4);
  auto second = cache->makeWrite(std::make_pair(17, 23), data, 4);
  data = {};
  cache.reset();
  assert(!weak.expired());
  dxmt::bounded_persistence a, b;
  assert(a.try_submit(std::move(first)));
  assert(b.try_submit(std::move(second)));
  until([&] { return a.snapshot().completed == 1 && b.snapshot().completed == 1; });
  assert(fake_writer->peak == 1 && fake_writer->calls == 2);
  assert(weak.expired());
  // A failed optional operation cannot kill the writer or prevent later jobs.
  assert(a.try_submit({[] { throw 3; }, 0, 0}));
  std::atomic<bool> next{false};
  assert(a.try_submit({[&] { next = true; }, 0, 0}));
  until([&] { return next.load() && a.snapshot().completed == 3; });
  assert(a.snapshot().failed == 1);
}

void compiler_completion_during_shutdown() {
  scheduler_close_observed = false;
  Task saving, dependency, continuations[4];
  std::atomic<bool> io_active{false}, release_io{false}, dependency_active{false}, release_dependency{false};
  std::atomic<bool> destroyed{false}, cancelled_write_ran{false};
  std::atomic<unsigned> attempts{0};
  saving.run = [&] { return &saving; };
  saving.post = {[&] { io_active = true; until([&] { return release_io.load(); }); }, 0, 0};
  dependency.run = [&] {
    dependency_active = true;
    until([&] { return release_dependency.load(); });
    return &dependency;
  };
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> weak = token;
  dependency.post = {[token, &cancelled_write_ran] { cancelled_write_ran = true; }, 4, 1};
  token.reset();
  auto scheduler = std::make_unique<dxmt::task_scheduler<Task *>>(true);
  scheduler->submit(&saving);
  until([&] { return io_active.load(); });
  scheduler->submit(&dependency);
  until([&] { return dependency_active.load(); });
  for (auto &continuation : continuations) {
    continuation.run = [&continuation, &dependency, &attempts] {
      attempts++;
      return dependency.done.load() ? &continuation : &dependency;
    };
    scheduler->submit(&continuation);
  }
  until([&] { return attempts == 4 && scheduler->get_running_threads() == 1; });
  std::thread stop([&] {
    destroying_scheduler = true;
    scheduler.reset();
    destroyed = true;
  });
  until([&] { return scheduler_close_observed.load(); });
  const auto workers_at_close = created_threads.load();
  release_dependency = true;
  until([&] { return dependency.done.load() && weak.expired(); });
  // Queued optional work is discarded after compiler join. Completion cannot
  // grow the closed compiler pool, and the active owned writer is still joined.
  assert(!destroyed && !cancelled_write_ran && attempts == 4);
  assert(created_threads == workers_at_close);
  release_io = true;
  stop.join();
  assert(destroyed);
}

int main() {
  burst(false);
  burst(true);
  dependency_and_optional_io();
  saturation_and_shutdown();
  detached_cache_ownership_and_errors();
  compiler_completion_during_shutdown();
  std::cout << "PASS exact scheduler/cache headers: burst policy, dependencies, optional I/O isolation, "
               "active+queued item/byte/object caps, shutdown/drop, retained lifetime, shared writer, failures\n";
}
