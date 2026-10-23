// SPDX-License-Identifier: MIT
// Actual retirement helper, COM destruction hook and outer retirement methods.
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "mock_threadpool.hpp"

using ULONG = unsigned long; using UINT = unsigned; using HRESULT = int;
using GUID = uint64_t; using REFGUID = const GUID &; using REFIID = const GUID &;
constexpr int S_OK = 0, S_FALSE = 1, E_INVALIDARG = -1, DXGI_ERROR_MORE_DATA = -2, DXGI_ERROR_NOT_FOUND = -3;
#define STDMETHODCALLTYPE
#define __uuidof(T) GUID(0)
#define likely(x) (x)
#define unlikely(x) (x)
#define SUCCEEDED(x) ((x) >= 0)
struct IUnknown {
  virtual ULONG AddRef() = 0; virtual ULONG Release() = 0;
  virtual HRESULT QueryInterface(REFIID, void **) { return S_FALSE; }
};
namespace dxmt { using mutex = std::mutex; }
#include "com-under-test.hpp"
#define _WIN32 1
#include "retirement-under-test.hpp"
#undef _WIN32

struct State {
  std::mutex mutex, caller_mutex;
  std::condition_variable cv;
  bool allow_drain = false, drain_entered = false;
  std::atomic<bool> destroyed = false, destroyed_on_worker = false;
  std::atomic<unsigned> queue_shutdown = 0;
};
struct DrainContext {
  State &state;
  void WaitUntilGPUIdle() {
    // The original caller may hold a mutex while its final Release runs.
    // A synchronous teardown would block that caller on itself.
    std::lock_guard caller_lock(state.caller_mutex);
    std::unique_lock lock(state.mutex);
    state.drain_entered = true; state.cv.notify_all();
    state.cv.wait(lock, [&] { return state.allow_drain; });
    assert(!state.destroyed);
  }
};
struct Queue {
  State &state;
  void ShutdownAndJoin() {
    assert(state.allow_drain && !state.destroyed);
    state.queue_shutdown++;
  }
};
class MTLD3D11DXGIDevice : public dxmt::ComObjectWithInitialRef<IUnknown> {
public:
  explicit MTLD3D11DXGIDevice(State &state) : retirement_(this, &Retire), state_(state),
      context_(state), d3d11_device_{&context_}, cmd_queue_{state} {}
  ~MTLD3D11DXGIDevice() {
    state_.destroyed_on_worker = mocktp::worker;
    state_.destroyed = true;
  }
private:
#include "retirement-owner-methods.hpp"
  dxmt::DeviceRetirement retirement_;
  State &state_;
  DrainContext context_;
  struct { DrainContext *context_; } d3d11_device_;
  Queue cmd_queue_;
};

static unsigned failures = 0;
void check(bool condition, const char *name) {
  if (!condition) { std::cerr << "FAIL " << name << '\n'; ++failures; }
}
void release_gate(State &state) {
  { std::lock_guard lock(state.mutex); state.allow_drain = true; }
  state.cv.notify_all();
}

bool origin_case(bool worker_origin, bool private_base_path, bool already_idle) {
  mocktp::Ledger ledger; mocktp::active = &ledger;
  State state; state.allow_drain = already_idle;
  auto *object = new MTLD3D11DXGIDevice(state);
  if (private_base_path) object->AddRefPrivate();
  auto release = [&] {
    std::lock_guard caller_lock(state.caller_mutex);
    if (private_base_path) {
      object->Release();
      static_cast<dxmt::ComObjectWithInitialRef<IUnknown> *>(object)->ReleasePrivate();
    } else object->Release();
  };
  if (worker_origin) { std::thread origin(release); origin.join(); } else release();
  if (!already_idle && state.destroyed) {
    check(false, "deferred destruction required");
    ledger.join(); return false;
  }
  if (!already_idle) {
    std::unique_lock lock(state.mutex);
    check(state.cv.wait_for(lock, std::chrono::seconds(2), [&] { return state.drain_entered; }),
          "retirement worker reaches drain");
    check(!state.destroyed && state.queue_shutdown == 0 && ledger.modules == 2,
          "pending work keeps device dependencies and module alive");
    lock.unlock();
  }
  // Legal dynamic caller holds its module reference through the Release call.
  FreeLibrary(&ledger);
  if (!already_idle) check(ledger.modules == 1, "worker module pin survives caller release");
  release_gate(state);
  ledger.join();
  check(state.destroyed && state.destroyed_on_worker && state.queue_shutdown == 1,
        "external retirement worker drains joins then destroys");
  check(ledger.submissions == 1 && ledger.callbacks == 1 && ledger.work_closed == 1
            && ledger.pools_closed == 1 && ledger.unload_deferred == 1 && ledger.modules == 0,
        "once-only handoff and balanced callback unload");
  return true;
}

void setup_failures() {
  for (auto failure : {mocktp::Failure::Module, mocktp::Failure::Pool, mocktp::Failure::Minimum, mocktp::Failure::Work}) {
    mocktp::Ledger ledger; ledger.failure = failure; mocktp::active = &ledger;
    State state;
    bool threw = false;
    try { auto object = std::make_unique<MTLD3D11DXGIDevice>(state); }
    catch (const std::exception &) { threw = true; }
    ledger.join();
    check(threw && !state.destroyed && ledger.callbacks == 0 && ledger.submissions == 0,
          "retirement setup fails before object publication");
    check(ledger.modules == 1 && ledger.pools_created == ledger.pools_closed
              && ledger.work_created == ledger.work_closed && ledger.environment_init == ledger.environment_destroy,
          "retirement setup failure balances resources");
  }
  mocktp::Ledger ledger; mocktp::active = &ledger;
  { dxmt::DeviceRetirement unsubmitted(nullptr, [](void *) noexcept { assert(false); }); }
  ledger.join();
  check(ledger.modules == 1 && ledger.pools_created == ledger.pools_closed
            && ledger.work_created == ledger.work_closed && ledger.callbacks == 0,
        "unsubmitted object closes reserved resources");
}

int main() {
  if (!origin_case(false, false, false)) return 1;
  origin_case(true, false, false);
  origin_case(true, true, false);
  origin_case(false, false, true);
  setup_failures();
  if (failures) return 1;
  std::cout << "PASS retirement: origins, private hook, pending drain, setup failures and module pin\n";
}
