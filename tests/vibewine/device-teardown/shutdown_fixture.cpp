// SPDX-License-Identifier: MIT
// Production shutdown/cancellation bodies; platform/Metal owners are counted CPU seams.
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include "shutdown-protocol.hpp"
#include "cpu-fence-under-test.hpp"

#define TRACE(...)
#define DXMT_RESIDENCY_SETS 0
constexpr int WMTCommandBufferStatusScheduled = 3;
constexpr int WMTCommandBufferStatusCompleted = 4;
constexpr int WMTCommandBufferStatusError = 5;
namespace cs2trace {
struct ApiScope { ApiScope(unsigned, uint64_t) {} unsigned result = 0; };
}
namespace dxmt {
using mutex = std::mutex;
constexpr unsigned kCommandChunkCount = 4;
static void require(bool value, const char *name) {
  if (!value) {
    std::cerr << "FAIL shutdown " << name << '\n';
    std::_Exit(1); // End only this bounded CPU fixture, including its gated test workers.
  }
}
struct State {
  std::atomic<unsigned> listener_stops = 0, thread_exits = 0, resets = 0;
  std::atomic<bool> listener_stop = false, release_finish = true, owns_capture = false;
  std::atomic<unsigned> ownership_takes = 0, capture_stops = 0;
  std::atomic<bool> encode_exited = false, finish_exited = false;
  std::atomic<bool> release_native = true;
  std::atomic<unsigned> native_waits = 0, native_completed = 0, discards = 0, error_releases = 0;
};
void SharedEventListener_destroy(State *state) {
#if TEST_TERMINAL_SHUTDOWN
  require(state->discards == 1, "discard-before-listener-close");
#endif
  state->listener_stops++;
  state->listener_stop = true;
  state->listener_stop.notify_all();
}
// Count only this queue's capture ownership, and observe the actual join seam.
struct CaptureState {
  State *state;
  std::thread *encode, *finish;
  bool takeCaptureOwnership() {
    assert(!encode->joinable() && !finish->joinable());
#if TEST_TERMINAL_SHUTDOWN
    require(state->discards == 1, "discard-before-capture");
#endif
    state->ownership_takes++;
    return state->owns_capture.exchange(false);
  }
};
} // namespace dxmt
namespace WMT {
struct CaptureManager {
  static inline dxmt::State *state = nullptr;
  static CaptureManager &sharedCaptureManager() {
    static CaptureManager manager;
    return manager;
  }
  void stopCapture() {
    assert(state && !state->owns_capture.load());
    assert(state->ownership_takes == 1 && state->resets == 0);
    state->capture_stops++;
  }
};
} // namespace WMT
namespace dxmt {
struct NativeBuffer {
  State *state = nullptr;
  bool valid = false, submitted = false;
  int terminal_status = WMTCommandBufferStatusScheduled;
  explicit operator bool() const { return valid; }
  int status() const {
    require(valid, "queried-null-buffer");
    return terminal_status;
  }
  void waitUntilCompleted() {
    require(valid && submitted, "waited-unsubmitted-buffer");
    require(state->encode_exited && state->finish_exited, "joined-before-native-drain");
    state->native_waits++;
    while (!state->release_native.load()) state->release_native.wait(false);
    terminal_status = WMTCommandBufferStatusCompleted;
    state->native_completed++;
  }
};
struct CommandChunk {
  State *state = nullptr;
  bool submitted = false;
  NativeBuffer attached_cmdbuf;
  void reset() {
#if TEST_TERMINAL_SHUTDOWN
    require(state->discards == 1, "discard-before-reset");
    require(!submitted || !attached_cmdbuf || attached_cmdbuf.status() > WMTCommandBufferStatusScheduled,
            "native-terminal-before-reset");
#endif
    assert(state->encode_exited && state->finish_exited);
    if (submitted && attached_cmdbuf && attached_cmdbuf.status() == WMTCommandBufferStatusError)
      state->error_releases++;
    submitted = false;
    attached_cmdbuf.valid = false;
    state->resets++;
  }
};
struct EncodingContext {
  State *state;
  std::array<CommandChunk, kCommandChunkCount> *chunks;
  void discardCommands() {
    require(state->encode_exited && state->finish_exited, "joined-before-discard");
    require(state->resets == 0 && state->ownership_takes == 0, "discard-before-owner-reset");
    for (auto &chunk : *chunks)
      require(!chunk.submitted || !chunk.attached_cmdbuf ||
                  chunk.attached_cmdbuf.status() > WMTCommandBufferStatusScheduled,
              "discard-before-native-terminal");
    state->discards++;
  }
};
class CommandQueue {
public:
  explicit CommandQueue(State &state) :
      state_(state), shared_event_listener(&state), capture_state{&state, &encodeThread, &finishThread},
      argument_encoding_ctx{&state, &chunks} {
    WMT::CaptureManager::state = &state;
    for (auto &chunk : chunks) {
      chunk.state = &state;
      chunk.attached_cmdbuf.state = &state;
    }
    encodeThread = std::thread([this] {
      waitForStop(ready_for_encode);
      assert(stopped.load());
      state_.encode_exited = true;
      state_.thread_exits++;
    });
    finishThread = std::thread([this] {
      waitForStop(ready_for_commit);
      assert(stopped.load());
      while (!state_.release_finish.load()) state_.release_finish.wait(false);
      state_.finish_exited = true;
      state_.thread_exits++;
    });
    event_listener_thread = std::thread([this] {
      while (!state_.listener_stop.load()) state_.listener_stop.wait(false);
      state_.thread_exits++;
    });
  }
  ~CommandQueue();
  void ShutdownAndJoin();
  uint64_t traceSerial() const { return 1; }
  void awaitStopPublication() {
#if TEST_TERMINAL_SHUTDOWN
    while (!wait_state_->canceled.load()) std::this_thread::yield();
#else
    while (!state_.listener_stops.load()) std::this_thread::yield();
#endif
  }
  void prepareNativeWork(bool already_completed = false) {
    chunks[0].submitted = chunks[0].attached_cmdbuf.submitted = true;
    chunks[0].attached_cmdbuf.valid = true;
    chunks[0].attached_cmdbuf.terminal_status = already_completed
        ? WMTCommandBufferStatusCompleted : WMTCommandBufferStatusScheduled;
    chunks[1].submitted = chunks[1].attached_cmdbuf.submitted = true;
    chunks[1].attached_cmdbuf.valid = true;
    chunks[1].attached_cmdbuf.terminal_status = WMTCommandBufferStatusError;
    chunks[2].attached_cmdbuf.valid = true; // Recorded, never submitted: waiting is illegal.
    chunks[3].submitted = true; // Nil native object must not be queried or waited.
  }
private:
  void waitForStop(std::atomic<uint64_t> &legacy_ready) {
#if TEST_TERMINAL_SHUTDOWN
    for (;;) {
      const auto epoch = wait_state_->epoch.load(std::memory_order_acquire);
      if (wait_state_->canceled.load(std::memory_order_acquire)) return;
      wait_state_->epoch.wait(epoch, std::memory_order_acquire);
    }
#else
    legacy_ready.wait(1);
#endif
  }
  State &state_;
  std::atomic<bool> stopped = false;
  std::atomic<uint64_t> ready_for_encode = 1, ready_for_commit = 1;
  dxmt::mutex shutdown_mutex_, failure_mutex_;
#if TEST_TERMINAL_SHUTDOWN
  std::shared_ptr<CpuFence::WaitState> wait_state_ = std::make_shared<CpuFence::WaitState>();
#endif
  bool shutdown_complete_ = false;
  std::array<CommandChunk, kCommandChunkCount> chunks;
  std::thread encodeThread, finishThread;
  State *shared_event_listener;
  std::thread event_listener_thread;
  CaptureState capture_state;
  EncodingContext argument_encoding_ctx;
};
#include "shutdown-under-test.hpp"
}
using namespace dxmt;

int main() {
  for (bool owned : {false, true}) {
    State idle;
    idle.owns_capture = owned;
    {
      CommandQueue queue(idle);
      queue.ShutdownAndJoin();
      queue.ShutdownAndJoin();
    }
    assert(idle.listener_stops == 1 && idle.thread_exits == 3 && idle.resets == 4);
    assert(idle.ownership_takes == 1 && idle.capture_stops == unsigned(owned));
    assert(!idle.owns_capture);
#if TEST_TERMINAL_SHUTDOWN
    assert(idle.discards == 1);
#endif
  }
  for (bool owned : {false, true}) {
    State pending;
    pending.owns_capture = owned;
    pending.release_finish = false;
    std::atomic<unsigned> returns = 0;
    {
      CommandQueue queue(pending);
      std::thread first([&] { queue.ShutdownAndJoin(); returns++; });
      queue.awaitStopPublication();
      std::thread second([&] { queue.ShutdownAndJoin(); returns++; });
      // A still-running finish worker must retire before either caller returns.
      assert(returns == 0 && pending.resets == 0);
      assert(pending.ownership_takes == 0 && pending.capture_stops == 0);
#if TEST_TERMINAL_SHUTDOWN
      assert(pending.listener_stops == 0 && pending.discards == 0);
#endif
      pending.release_finish = true;
      pending.release_finish.notify_all();
      first.join(); second.join();
      assert(returns == 2);
    }
    assert(pending.listener_stops == 1 && pending.thread_exits == 3 && pending.resets == 4);
    assert(pending.ownership_takes == 1 && pending.capture_stops == unsigned(owned));
    assert(!pending.owns_capture);
  }
  State destructor_only;
  destructor_only.owns_capture = true;
  { CommandQueue queue(destructor_only); }
  assert(destructor_only.listener_stops == 1 && destructor_only.thread_exits == 3 && destructor_only.resets == 4);
  assert(destructor_only.ownership_takes == 1 && destructor_only.capture_stops == 1 && !destructor_only.owns_capture);
#if TEST_TERMINAL_SHUTDOWN
  State native;
  native.release_native = false;
  std::atomic<unsigned> returns = 0;
  {
    CommandQueue queue(native);
    queue.prepareNativeWork();
    std::thread first([&] { queue.ShutdownAndJoin(); returns++; });
    while (!native.native_waits.load()) std::this_thread::yield();
    std::thread second([&] { queue.ShutdownAndJoin(); returns++; });
    assert(native.encode_exited && native.finish_exited && returns == 0);
    assert(native.listener_stops == 0 && native.discards == 0 && native.resets == 0);
    assert(native.ownership_takes == 0 && native.error_releases == 0);
    native.release_native = true;
    native.release_native.notify_all();
    first.join(); second.join();
    assert(returns == 2);
  }
  assert(native.native_waits == 1 && native.native_completed == 1 && native.discards == 1);
  assert(native.error_releases == 1 && native.resets == 4 && native.listener_stops == 1 && native.thread_exits == 3);
  State completed;
  {
    CommandQueue queue(completed);
    queue.prepareNativeWork(true);
    queue.ShutdownAndJoin();
  }
  assert(completed.native_waits == 0 && completed.discards == 1 && completed.error_releases == 1);
  assert(completed.resets == 4 && completed.thread_exits == 3);
#endif
  std::cout << "PASS queue shutdown: repeated, concurrent, pending worker, destructor and capture ownership\n";
}
