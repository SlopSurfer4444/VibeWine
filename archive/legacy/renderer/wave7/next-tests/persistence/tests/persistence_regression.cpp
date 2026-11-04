#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fixture {
using namespace std::chrono_literals;
void require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
struct Gate {
  std::mutex mutex;
  std::condition_variable condition;
  bool open = false;
  void signal() { std::lock_guard lock(mutex); open = true; condition.notify_all(); }
  void wait() { std::unique_lock lock(mutex); condition.wait(lock, [&] { return open; }); }
  bool await(std::chrono::milliseconds timeout = 1500ms) {
    std::unique_lock lock(mutex); return condition.wait_for(lock, timeout, [&] { return open; });
  }
};
std::atomic<Gate *> join_probe{nullptr};
std::atomic_bool join_held_scheduler_lock{false};
} // namespace fixture

namespace dxmt {
// Native pthread-backed adapters replace only the platform-specific thread API.
class mutex {
  std::mutex value;
public:
  static inline thread_local unsigned held_count = 0;
  void lock() { value.lock(); ++held_count; }
  void unlock() { --held_count; value.unlock(); }
};
class condition_variable {
  std::condition_variable_any value;
public:
  template <typename Lock, typename Predicate> void wait(Lock &lock, Predicate predicate) { value.wait(lock, predicate); }
  void notify_all() { value.notify_all(); }
  void notify_one() { value.notify_one(); }
};
class thread {
  std::thread value;
public:
  template <typename Function> explicit thread(Function &&function) : value(std::forward<Function>(function)) {}
  thread(thread &&) = default;
  thread &operator=(thread &&) = default;
  static unsigned hardware_concurrency() { return 1; } // Two real workers, no elastic extra worker masking lock regressions.
  bool joinable() { return value.joinable(); }
  void join() {
    if (mutex::held_count) fixture::join_held_scheduler_lock = true;
    if (auto probe = fixture::join_probe.load()) probe->signal();
    value.join();
  }
};
} // namespace dxmt
int GetCurrentThread() { return 0; }
constexpr int THREAD_PRIORITY_TIME_CRITICAL = 0;
void SetThreadPriority(int, int) {}

#include "production_scheduler.inc"
namespace dxmt {
#include "production_work.inc"
} // namespace dxmt

namespace fixture {
struct Archive {
  uint64_t identity;
  std::atomic<unsigned> *destroyed;
  Archive(uint64_t identity, std::atomic<unsigned> *destroyed) : identity(identity), destroyed(destroyed) {}
  ~Archive() { ++*destroyed; }
};
struct Sink {
  Gate entered, release, complete;
  std::mutex mutex;
  std::vector<std::pair<uint64_t, std::string>> archives;
  std::vector<uint64_t> recipes;
  std::atomic_bool saved_under_scheduler_lock{false};
  std::atomic_bool bad_recipe{false};
  std::atomic<unsigned> archive_destroyed{0};
};
Sink *current_sink = nullptr; // Set before starting workers; changed only after all workers are joined.
} // namespace fixture

namespace dxmt::cs2archive {
struct Pending {
  std::shared_ptr<fixture::Archive> archive;
  std::string path;
  void save() {
    if (!archive || path.empty()) return;
    auto held = std::move(archive); // Same ownership handoff as native Pending::save.
    auto &sink = *fixture::current_sink;
    if (dxmt::mutex::held_count) sink.saved_under_scheduler_lock = true;
    sink.entered.signal();
    sink.release.wait();
    // Deliberately access path AFTER the gate. A raw-this closure and task
    // deletion while blocked must be caught by ASan rather than passing by luck.
    {
      std::lock_guard lock(sink.mutex);
      sink.archives.emplace_back(held->identity, path);
    }
    path.clear();
  }
};
} // namespace dxmt::cs2archive
namespace dxmt::cs2recipe {
struct Recipe { uint64_t identity = 0; std::array<uint8_t, 64> bytes{}; bool configured = true; };
bool enabled(const Recipe &recipe) { return recipe.configured; }
void save(const Recipe &recipe) {
  if (!enabled(recipe)) return;
  auto &sink = *fixture::current_sink;
  if (dxmt::mutex::held_count) sink.saved_under_scheduler_lock = true;
  for (auto byte : recipe.bytes) if (byte != uint8_t(recipe.identity)) sink.bad_recipe = true;
  { std::lock_guard lock(sink.mutex); sink.recipes.push_back(recipe.identity); }
  sink.complete.signal();
}
} // namespace dxmt::cs2recipe

namespace dxmt {
class GraphicsWork : public ThreadpoolWork {
  std::atomic_bool ready_{false};
  cs2archive::Pending archive_pending_;
  cs2recipe::Recipe recipe_{};
public:
  fixture::Gate *compile_entered = nullptr, *compile_release = nullptr, *dependency_checked = nullptr;
  std::atomic<unsigned> run_count{0};
  GraphicsWork(uint64_t identity, bool archive = true, bool recipe = true) {
    if (archive) archive_pending_.archive = std::make_shared<fixture::Archive>(identity, &fixture::current_sink->archive_destroyed);
    archive_pending_.path = "owned-archive-" + std::to_string(identity);
    recipe_.identity = identity; recipe_.bytes.fill(uint8_t(identity)); recipe_.configured = recipe;
  }
  ThreadpoolWork *RunThreadpoolWork() override {
    ++run_count;
    if (compile_entered) compile_entered->signal();
    if (compile_release) compile_release->wait();
    return this;
  }
  bool GetIsDone() override {
    const bool ready = ready_.load();
    if (!ready && dependency_checked) dependency_checked->signal();
    return ready;
  }
  bool payload_detached() const { return !archive_pending_.archive; }
#include "production_graphics_completion.inc"
};

class BasicWork : public ThreadpoolWork {
public:
  std::atomic_bool ready{false};
  std::atomic<unsigned> run_count{0}, post_count{0};
  ThreadpoolWork *dependency = nullptr;
  fixture::Gate *entered = nullptr, *release = nullptr;
  ThreadpoolWork *RunThreadpoolWork() override {
    ++run_count;
    if (dependency && !dependency->GetIsDone()) return dependency;
    if (entered) entered->signal();
    if (release) release->wait();
    return this;
  }
  bool GetIsDone() override { return ready.load(); }
  void SetIsDone(bool state) override { ready.store(state); ready.notify_all(); }
#if FIXTURE_HAS_POST
  std::function<void()> TakePostCompletionWork() override { ++post_count; return {}; }
#endif
};
class DefaultWork : public ThreadpoolWork {
  std::atomic_bool ready{false};
public:
  ThreadpoolWork *RunThreadpoolWork() override { return this; }
  bool GetIsDone() override { return ready.load(); }
  void SetIsDone(bool state) override { ready.store(state); ready.notify_all(); }
};
} // namespace dxmt

namespace fixture {
bool done(dxmt::ThreadpoolWork &work) {
  const auto deadline = std::chrono::steady_clock::now() + 1500ms;
  while (std::chrono::steady_clock::now() < deadline) {
    if (work.GetIsDone()) return true;
    std::this_thread::yield();
  }
  return work.GetIsDone();
}

void persistence_does_not_block_graph_and_owns_payload() {
  Sink sink; current_sink = &sink;
  Gate compile_entered, compile_release, dependency_checked;
  auto root = std::make_unique<dxmt::GraphicsWork>(41);
  root->compile_entered = &compile_entered; root->compile_release = &compile_release;
  root->dependency_checked = &dependency_checked;
  dxmt::BasicWork continuation, independent;
  continuation.dependency = root.get();
  auto scheduler = std::make_unique<dxmt::task_scheduler<dxmt::ThreadpoolWork *>>();
  scheduler->submit(root.get());
  require(compile_entered.await(), "root compilation did not enter");
  scheduler->submit(&continuation);
  const bool registered = dependency_checked.await();
  // get_done is called under the production deps lock; insertion completes
  // before root can take that lock after this gate is released.
  const bool premature_detach = continuation.post_count.load() != 0;
  compile_release.signal();
  const bool persistence_entered = sink.entered.await();
  scheduler->submit(&independent);
  const bool continuation_done = done(continuation);
  const bool independent_done = done(independent);
  const bool ready = root->GetIsDone();
  const bool detached = root->payload_detached();
  const bool no_locks = !sink.saved_under_scheduler_lock.load();
  const bool safe_to_retire = registered && persistence_entered && continuation_done && independent_done && ready && no_locks;
  if (safe_to_retire) {
    root.reset(); // SetIsDone has returned; persistence is still blocked.
  }
  sink.release.signal();
  scheduler.reset();
  root.reset();
  require(registered && !premature_detach, "non-completing dependency path detached post-completion work");
  require(persistence_entered, "completed graphics work skipped persistence");
  require(no_locks, "archive/recipe persistence executed under a scheduler mutex");
  require(continuation_done && independent_done, "blocked persistence prevented continuation or independent completion");
  require(ready && detached && safe_to_retire, "completed task did not detach owned payload before ready publication");
  require(sink.complete.await(), "detached persistence skipped recipe save");
  {
    std::lock_guard lock(sink.mutex);
    require(sink.archives == std::vector<std::pair<uint64_t, std::string>>{{41, "owned-archive-41"}} &&
            sink.recipes == std::vector<uint64_t>{41} && !sink.bad_recipe,
            "detached archive/path/recipe changed, duplicated or disappeared after task deletion");
  }
  require(sink.archive_destroyed.load() == 1, "detached archive ownership leaked or released more than once");
#if FIXTURE_HAS_POST
  require(continuation.post_count.load() == 1 && independent.post_count.load() == 1,
          "completion hook did not run exactly once per actually completed task");
#endif
  std::cout << "PASS blocked_persistence_unblocks_graph_continuations_owned_payload_survives_task_deletion\n";
}

void destruction_joins_persistence_and_cancels_queue() {
  Sink sink; current_sink = &sink;
  Gate blocker_entered, blocker_release, join_entered;
  auto root = std::make_unique<dxmt::GraphicsWork>(52);
  dxmt::BasicWork blocker, queued;
  blocker.entered = &blocker_entered; blocker.release = &blocker_release;
  auto scheduler = std::make_unique<dxmt::task_scheduler<dxmt::ThreadpoolWork *>>();
  scheduler->submit(root.get());
  require(sink.entered.await(), "shutdown fixture did not block active persistence");
  scheduler->submit(&blocker);
  const bool blocker_running = blocker_entered.await();
  scheduler->submit(&queued); // Both real workers occupied; this must remain queued.
  join_probe = &join_entered;
  auto destroying = std::async(std::launch::async, [&] { scheduler.reset(); });
  const bool joining = join_entered.await();
  const bool waited_for_persistence = destroying.wait_for(0ms) != std::future_status::ready;
  blocker_release.signal();
  const bool blocker_finished = done(blocker);
  const bool still_waiting = destroying.wait_for(0ms) != std::future_status::ready;
  if (joining && root->GetIsDone()) root.reset();
  sink.release.signal();
  const bool destroyed = destroying.wait_for(1500ms) == std::future_status::ready;
  destroying.get(); join_probe = nullptr; root.reset();
  require(blocker_running && joining && waited_for_persistence && blocker_finished && still_waiting && destroyed,
          "scheduler destruction did not join active persistence cleanly");
  require(!join_held_scheduler_lock.load(), "scheduler joined a worker under a scheduler mutex");
  require(queued.run_count.load() == 0 && !queued.GetIsDone(), "shutdown executed queued work instead of canceling it");
  require(sink.complete.await(), "shutdown abandoned active owned persistence");
  std::cout << "PASS scheduler_destruction_joins_active_persistence_and_cancels_queued_work\n";
}

void disabled_and_default_hooks() {
  Sink sink; current_sink = &sink; sink.release.signal();
  dxmt::GraphicsWork disabled(63, false, false);
  dxmt::DefaultWork shader_or_compute;
#if FIXTURE_HAS_POST
  require(!disabled.TakePostCompletionWork(), "disabled archive/recipe created post-completion closure");
  require(!shader_or_compute.TakePostCompletionWork(), "default shader/compute hook was not empty");
#endif
  {
    dxmt::task_scheduler<dxmt::ThreadpoolWork *> scheduler;
    scheduler.submit(&disabled); scheduler.submit(&shader_or_compute);
    require(done(disabled) && done(shader_or_compute), "disabled/default persistence work did not become ready");
  }
  require(sink.archives.empty() && sink.recipes.empty(), "disabled persistence unexpectedly saved data");
  std::cout << "PASS disabled_archive_recipe_and_default_hook_complete_without_persistence\n";
}
} // namespace fixture

int main() {
  try {
    fixture::persistence_does_not_block_graph_and_owns_payload();
    fixture::destruction_joins_persistence_and_cancels_queue();
    fixture::disabled_and_default_hooks();
    std::cout << "PASS production_scheduler_persistence_detach_all\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL " << error.what() << '\n'; return 1;
  }
}
