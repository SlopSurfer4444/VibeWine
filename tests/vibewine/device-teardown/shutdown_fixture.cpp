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
  std::atomic<bool> listener_stop = false, release_finish = true;
};
void SharedEventListener_destroy(State *state) {
  state->listener_stops++;
  state->listener_stop = true;
  state->listener_stop.notify_all();
}
struct CommandChunk {
  State *state = nullptr;
  void reset() { state->resets++; }
};
class CommandQueue {
public:
  explicit CommandQueue(State &state) : state_(state), shared_event_listener(&state) {
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
};
#include "shutdown-under-test.hpp"
}
using namespace dxmt;

int main() {
  State idle;
  {
    CommandQueue queue(idle);
    queue.ShutdownAndJoin();
    queue.ShutdownAndJoin();
  }
  assert(idle.listener_stops == 1 && idle.thread_exits == 3 && idle.resets == 4);
  State pending;
  pending.release_finish = false;
  std::atomic<unsigned> returns = 0;
  {
    CommandQueue queue(pending);
    std::thread first([&] { queue.ShutdownAndJoin(); returns++; });
    while (!pending.listener_stops.load()) std::this_thread::yield();
    std::thread second([&] { queue.ShutdownAndJoin(); returns++; });
    // A still-running finish worker must retire before either caller returns.
    assert(returns == 0 && pending.resets == 0);
    pending.release_finish = true;
    pending.release_finish.notify_all();
    first.join(); second.join();
    assert(returns == 2);
  }
  assert(pending.listener_stops == 1 && pending.thread_exits == 3 && pending.resets == 4);
  std::cout << "PASS queue shutdown: repeated, concurrent, pending worker and destructor\n";
}
