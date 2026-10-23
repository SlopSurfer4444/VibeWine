// SPDX-License-Identifier: MIT
// Counted Windows threadpool contract seam. No native Windows behavior claimed.
#pragma once
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#define CALLBACK
namespace mocktp {
enum class Failure { None, Module, Pool, Minimum, Work };
struct Pool;
struct Work;
struct Instance;
using Callback = void (*)(Instance *, void *, Work *);
struct Ledger {
  Failure failure = Failure::None;
  std::atomic<unsigned> modules = 1, pools_created = 0, pools_closed = 0;
  std::atomic<unsigned> work_created = 0, work_closed = 0, submissions = 0, callbacks = 0;
  std::atomic<unsigned> environment_init = 0, environment_destroy = 0, unload_deferred = 0;
  std::atomic<bool> callback_returned = false;
  std::vector<std::unique_ptr<Pool>> pools;
  std::vector<std::unique_ptr<Work>> works;
  void join();
};
inline Ledger *active = nullptr;
inline thread_local bool worker = false;
struct Instance { Ledger *deferred_module = nullptr; };
struct Work {
  Pool *pool;
  Callback callback;
  void *context;
  std::atomic<bool> closed = false;
};
struct Pool {
  Ledger *ledger;
  std::mutex mutex;
  std::condition_variable cv;
  Work *queued = nullptr;
  bool closed = false;
  std::thread thread;
};
inline void Ledger::join() {
  for (auto &pool : pools) if (pool->thread.joinable()) pool->thread.join();
}
}
using HMODULE = mocktp::Ledger *;
using LPCWSTR = const wchar_t *;
using PTP_POOL = mocktp::Pool *;
using PTP_WORK = mocktp::Work *;
using PTP_CALLBACK_INSTANCE = mocktp::Instance *;
struct TP_CALLBACK_ENVIRON { PTP_POOL pool = nullptr; };
constexpr unsigned GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS = 4;
inline void InitializeThreadpoolEnvironment(TP_CALLBACK_ENVIRON *) { mocktp::active->environment_init++; }
inline void DestroyThreadpoolEnvironment(TP_CALLBACK_ENVIRON *) { mocktp::active->environment_destroy++; }
inline bool GetModuleHandleExW(unsigned flags, LPCWSTR address, HMODULE *module) {
  assert(flags == GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS && address);
  if (mocktp::active->failure == mocktp::Failure::Module) return false;
  *module = mocktp::active; (*module)->modules++; return true;
}
inline bool FreeLibrary(HMODULE module) { assert(module->modules); module->modules--; return true; }
inline PTP_POOL CreateThreadpool(void *) {
  if (mocktp::active->failure == mocktp::Failure::Pool) return nullptr;
  auto pool = std::make_unique<mocktp::Pool>(); pool->ledger = mocktp::active;
  auto *result = pool.get(); mocktp::active->pools.push_back(std::move(pool));
  mocktp::active->pools_created++; return result;
}
inline void SetThreadpoolThreadMaximum(PTP_POOL, unsigned maximum) { assert(maximum == 1); }
inline bool SetThreadpoolThreadMinimum(PTP_POOL pool, unsigned minimum) {
  assert(minimum == 1);
  if (pool->ledger->failure == mocktp::Failure::Minimum) return false;
  pool->thread = std::thread([pool] {
    mocktp::worker = true;
    std::unique_lock lock(pool->mutex);
    pool->cv.wait(lock, [&] { return pool->queued || pool->closed; });
    if (!pool->queued) return;
    auto *work = pool->queued;
    lock.unlock();
    mocktp::Instance instance;
    pool->ledger->callbacks++;
    work->callback(&instance, work->context, work);
    // Simulate TpCallbackUnloadDllOnCompletion after callback code returns.
    if (instance.deferred_module) FreeLibrary(instance.deferred_module);
    pool->ledger->callback_returned = true;
  });
  return true;
}
inline void SetThreadpoolCallbackPool(TP_CALLBACK_ENVIRON *environment, PTP_POOL pool) { environment->pool = pool; }
inline PTP_WORK CreateThreadpoolWork(mocktp::Callback callback, void *context, TP_CALLBACK_ENVIRON *environment) {
  if (mocktp::active->failure == mocktp::Failure::Work) return nullptr;
  auto work = std::make_unique<mocktp::Work>();
  work->pool = environment->pool; work->callback = callback; work->context = context;
  auto *result = work.get(); mocktp::active->works.push_back(std::move(work));
  mocktp::active->work_created++; return result;
}
inline void CloseThreadpoolWork(PTP_WORK work) {
  assert(!work->closed.exchange(true)); work->pool->ledger->work_closed++;
}
inline void CloseThreadpool(PTP_POOL pool) {
  { std::lock_guard lock(pool->mutex); assert(!pool->closed); pool->closed = true; }
  pool->ledger->pools_closed++; pool->cv.notify_all();
}
inline void SubmitThreadpoolWork(PTP_WORK work) {
  auto *pool = work->pool;
  pool->ledger->submissions++;
  { std::lock_guard lock(pool->mutex); assert(!pool->queued && !pool->closed); pool->queued = work; }
  pool->cv.notify_all();
}
inline void FreeLibraryWhenCallbackReturns(PTP_CALLBACK_INSTANCE instance, HMODULE module) {
  assert(mocktp::worker && !instance->deferred_module);
  module->unload_deferred++; instance->deferred_module = module;
}
