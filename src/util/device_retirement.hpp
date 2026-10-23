#pragma once

#include "util_error.hpp"

#ifdef _WIN32
#include <windows.h>
#include <threadpoolapiset.h>
#endif

namespace dxmt {

#ifdef _WIN32
// Reserve the complete final-release execution path before publishing a device.
// Final release can run inside a queue callback, so it must not wait for that
// queue, allocate another worker, or inherit the caller's locks into teardown.
class DeviceRetirement {
public:
  using Destroy = void (*)(void *) noexcept;

  DeviceRetirement(void *context, Destroy destroy) : context_(context), destroy_(destroy) {
    TP_CALLBACK_ENVIRON environment;
    InitializeThreadpoolEnvironment(&environment);
    try {
      // Keep the callback and its imported dependencies loaded after the
      // caller returns from Release and drops its own module reference. As
      // with other DLL calls, the caller must retain that reference until
      // Release returns; this pin does not legalize unloading during a call.
      if (!GetModuleHandleExW(
              GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
              reinterpret_cast<LPCWSTR>(&WorkCallback), &module_))
        throw MTLD3DError("Failed to retain device retirement module");

      pool_ = CreateThreadpool(nullptr);
      if (!pool_)
        throw MTLD3DError("Failed to create device retirement pool");
      SetThreadpoolThreadMaximum(pool_, 1);
      if (!SetThreadpoolThreadMinimum(pool_, 1))
        throw MTLD3DError("Failed to create device retirement worker");

      SetThreadpoolCallbackPool(&environment, pool_);
      work_ = CreateThreadpoolWork(&WorkCallback, this, &environment);
      if (!work_)
        throw MTLD3DError("Failed to create device retirement work");
    } catch (...) {
      DestroyThreadpoolEnvironment(&environment);
      CloseUnsubmitted();
      throw;
    }
    DestroyThreadpoolEnvironment(&environment);
  }

  ~DeviceRetirement() {
    CloseUnsubmitted();
  }

  DeviceRetirement(const DeviceRetirement &) = delete;
  DeviceRetirement &operator=(const DeviceRetirement &) = delete;

  // Called exactly once, at the actual final private-reference transition.
  // The callback may destroy this object before SubmitThreadpoolWork returns.
  void submit() noexcept {
    SubmitThreadpoolWork(work_);
  }

private:
  void *context_;
  Destroy destroy_;

  HMODULE module_ = nullptr;
  PTP_POOL pool_ = nullptr;
  PTP_WORK work_ = nullptr;

  void CloseUnsubmitted() noexcept {
    if (work_)
      CloseThreadpoolWork(work_);
    if (pool_)
      CloseThreadpool(pool_);
    if (module_)
      FreeLibrary(module_);
  }

  static void CALLBACK WorkCallback(PTP_CALLBACK_INSTANCE instance, void *context, PTP_WORK) noexcept {
    auto *retirement = static_cast<DeviceRetirement *>(context);
    const auto module = retirement->module_;
    const auto pool = retirement->pool_;
    const auto work = retirement->work_;
    const auto destroy = retirement->destroy_;
    const auto object = retirement->context_;

    // The object owns this helper. Transfer all handles before deleting it;
    // neither its destructor nor the returning callback may unload our code.
    retirement->module_ = nullptr;
    retirement->pool_ = nullptr;
    retirement->work_ = nullptr;
    destroy(object);
    CloseThreadpoolWork(work);
    CloseThreadpool(pool);
    FreeLibraryWhenCallbackReturns(instance, module);
  }
};
#endif

} // namespace dxmt
