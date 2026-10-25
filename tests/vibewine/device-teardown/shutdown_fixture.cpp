// SPDX-License-Identifier: MIT
// Production queue shutdown body; platform/Metal workers replaced by counted threads.
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>

#define TRACE(...)
namespace cs2trace {
struct ApiScope { ApiScope(unsigned, uint64_t) {} unsigned result = 0; };
}
namespace dxmt {
using mutex = std::mutex;
constexpr unsigned kCommandChunkCount = 4;
struct State {
  std::atomic<unsigned> listener_stops = 0, thread_exits = 0, resets = 0;
  std::atomic<bool> listener_stop = false, release_finish = true, owns_capture = false;
  std::atomic<unsigned> ownership_takes = 0, capture_stops = 0;
};
void SharedEventListener_destroy(State *state) {
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
struct CommandChunk {
  State *state = nullptr;
  void reset() { state->resets++; }
};
class CommandQueue {
public:
  explicit CommandQueue(State &state) :
      state_(state), shared_event_listener(&state), capture_state{&state, &encodeThread, &finishThread} {
    WMT::CaptureManager::state = &state;
    for (auto &chunk : chunks) chunk.state = &state;
    encodeThread = std::thread([this] {
      ready_for_encode.wait(1);
      assert(stopped.load());
      state_.thread_exits++;
    });
    finishThread = std::thread([this] {
      ready_for_commit.wait(1);
      assert(stopped.load());
      while (!state_.release_finish.load()) state_.release_finish.wait(false);
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
private:
  State &state_;
  std::atomic<bool> stopped = false;
  std::atomic<uint64_t> ready_for_encode = 1, ready_for_commit = 1;
  dxmt::mutex shutdown_mutex_;
  bool shutdown_complete_ = false;
  std::array<CommandChunk, kCommandChunkCount> chunks;
  std::thread encodeThread, finishThread;
  State *shared_event_listener;
  std::thread event_listener_thread;
  CaptureState capture_state;
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
  }
  for (bool owned : {false, true}) {
    State pending;
    pending.owns_capture = owned;
    pending.release_finish = false;
    std::atomic<unsigned> returns = 0;
    {
      CommandQueue queue(pending);
      std::thread first([&] { queue.ShutdownAndJoin(); returns++; });
      while (!pending.listener_stops.load()) std::this_thread::yield();
      std::thread second([&] { queue.ShutdownAndJoin(); returns++; });
      // A still-running finish worker must retire before either caller returns.
      assert(returns == 0 && pending.resets == 0);
      assert(pending.ownership_takes == 0 && pending.capture_stops == 0);
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
  std::cout << "PASS queue shutdown: repeated, concurrent, pending worker, destructor and capture ownership\n";
}
