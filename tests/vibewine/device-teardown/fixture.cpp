// SPDX-License-Identifier: MIT
// Source-derived ownership test. No GPU and no deliberate dangling dereference.
#include <atomic>
#include <cassert>
#include <concepts>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#ifdef TEST_RETIREMENT
#include "mock_threadpool.hpp"
#endif

using ULONG = unsigned long;
using UINT = unsigned;
using HRESULT = int;
using GUID = uint64_t;
using REFGUID = const GUID &;
using REFIID = const GUID &;
using LPCSTR = const char *;
constexpr HRESULT S_OK = 0, S_FALSE = 1, E_INVALIDARG = -1;
constexpr HRESULT DXGI_ERROR_MORE_DATA = -2, DXGI_ERROR_NOT_FOUND = -3;
constexpr GUID WKPDID_D3DDebugObjectName = 99;
#define STDMETHODCALLTYPE
#define __uuidof(T) GUID(0)
#define D3D11_ASSERT(x) assert(x)
#define SUCCEEDED(x) ((x) >= 0)
#define likely(x) (x)
#define unlikely(x) (x)

namespace dxmt { struct MTLD3D11Device; }
using ID3D11Device = dxmt::MTLD3D11Device;
struct IUnknown {
  virtual HRESULT QueryInterface(REFIID, void **) { return S_FALSE; }
  virtual ULONG AddRef() = 0;
  virtual ULONG Release() = 0;
  virtual HRESULT GetPrivateData(REFGUID, UINT *, void *) { return S_FALSE; }
  virtual HRESULT SetPrivateData(REFGUID, UINT, const void *) { return S_FALSE; }
  virtual HRESULT SetPrivateDataInterface(REFGUID, const IUnknown *) { return S_FALSE; }
  virtual void GetDevice(ID3D11Device **) {}
};
namespace WMT {
struct AutoreleasePool {};
inline AutoreleasePool MakeAutoreleasePool() { return {}; }
}
namespace dxmt { using mutex = std::mutex; }
#include "com-under-test.hpp"
#ifdef TEST_RETIREMENT
#define _WIN32 1
#include "retirement-under-test.hpp"
#undef _WIN32
#endif

namespace dxmt {
struct StubQueue {
  uint64_t CurrentSeqId() { return 1; }
  std::function<void()> shutdown;
  void ShutdownAndJoin() { if (shutdown) shutdown(); }
};
struct Device { StubQueue &queue() { return queue_; } StubQueue queue_; };
struct MTLD3D11Device : IUnknown { virtual Device &GetDXMTDevice() = 0; };
}
#include "device-child-under-test.hpp"
#include "command-list-under-test.hpp"

namespace dxmt {
enum class Role { API, Finish };
thread_local Role current_role = Role::API;
struct Observation {
  std::atomic<bool> destroyed = false;
  std::atomic<bool> destroyed_on_finish = false;
  std::atomic<unsigned> recycle_after_destroy = 0;
#ifdef TEST_RETIREMENT
  std::mutex mutex;
  std::condition_variable cv;
  bool allow_drain = false;
  std::atomic<unsigned> queue_shutdown = 0;
  std::atomic<bool> destroyed_on_worker = false;
#endif
};
#ifdef TEST_RETIREMENT
struct DrainContext {
  Observation &observation;
  void WaitUntilGPUIdle() {
    std::unique_lock lock(observation.mutex);
    observation.cv.wait(lock, [&] { return observation.allow_drain; });
  }
};
class DeviceOwner;
using MTLD3D11DXGIDevice = DeviceOwner;
#endif

// This shell uses the actual outer COM base and its unmodified public/private
// reference algorithms. Only native state and the destructor are observation seams.
class DeviceOwner : public ComObjectWithInitialRef<MTLD3D11Device> {
public:
  explicit DeviceOwner(Observation &observation) :
#ifdef TEST_RETIREMENT
      retirement_(this, &Retire),
#endif
      observation_(observation)
#ifdef TEST_RETIREMENT
      , drain_context_{observation}, d3d11_device_{&drain_context_}, cmd_queue_(device_.queue())
#endif
  {
#ifdef TEST_RETIREMENT
    cmd_queue_.shutdown = [&] { observation.queue_shutdown++; };
#endif
  }
  ~DeviceOwner() {
    observation_.destroyed_on_finish = current_role == Role::Finish;
    observation_.destroyed = true;
#ifdef TEST_RETIREMENT
    observation_.destroyed_on_worker = mocktp::worker;
#endif
  }
  Device &GetDXMTDevice() override { return device_; }
private:
#ifdef TEST_RETIREMENT
#include "retirement-owner-methods.hpp"
  DeviceRetirement retirement_;
#endif
  Observation &observation_;
  Device device_;
#ifdef TEST_RETIREMENT
  DrainContext drain_context_;
  struct { DrainContext *context_; } d3d11_device_;
  StubQueue &cmd_queue_;
#endif
};

class ImmediateContext : public MTLD3D11DeviceChild<IUnknown> {
public:
  explicit ImmediateContext(MTLD3D11Device *device) : MTLD3D11DeviceChild(device) {}
#include "context-refs-under-test.hpp"
private:
  std::atomic<uint32_t> refcount = 0;
};

class Query : public MTLD3D11DeviceChild<IUnknown> {
public:
  explicit Query(MTLD3D11Device *device) : MTLD3D11DeviceChild(device) {}
};
using MTLD3D11OcclusionQuery = Query;
using MTLD3D11EventQuery = Query;
using MTLD3D11TimestampQuery = Query;

struct ArgumentEncodingContext {
  void pushDeferredVisibilityQueries(const int *) {}
  void popDeferredVisibilityQueries() {}
};

class MTLD3D11CommandList;
struct MTLD3D11CommandListPoolBase {
  Observation &observation;
  void RecycleCommandList(MTLD3D11CommandList *) {
    if (observation.destroyed.load()) observation.recycle_after_destroy++;
  }
};
struct Allocation {};
struct Dynamic {
  void recycle(uint64_t, std::shared_ptr<Allocation> &&) {}
};
struct used_dynamic_buffer {
  std::shared_ptr<Dynamic> buffer;
  std::shared_ptr<Allocation> allocation;
};
struct used_dynamic_lineartexture {
  std::shared_ptr<Dynamic> texture;
  std::shared_ptr<Allocation> allocation;
};
struct Allocator { void free_blocks(uint64_t) {} };
struct StagingResource {};
template <typename T> using Rc = std::shared_ptr<T>;

class MTLD3D11CommandList : public ManagedDeviceChild<IUnknown> {
public:
  MTLD3D11CommandList(MTLD3D11Device *device, MTLD3D11CommandListPoolBase *pool) :
      ManagedDeviceChild(device), cmdlist_pool(pool) {}
#include "recycle-under-test.hpp"
  void Execute(ArgumentEncodingContext &context) { list.execute(context); }
#include "retained-fields-under-test.hpp"
  MTLD3D11CommandListPoolBase *cmdlist_pool;
  CommandList<ArgumentEncodingContext> list;
  Allocator staging_allocator, cpu_command_allocator;
};

struct QueueCapture {
  CommandList<ArgumentEncodingContext> list;
  std::vector<std::unique_ptr<std::byte[]>> storage;
  template <typename F> void EmitOP(F &&fn) {
    auto memory = std::make_unique<std::byte[]>(list.calculateCommandSize<F>());
    list.emit(std::forward<F>(fn), memory.get());
    storage.push_back(std::move(memory));
  }
  void executeCommandList(MTLD3D11CommandList *pCommandList) {
    Com<MTLD3D11CommandList, false> cmdlist = static_cast<MTLD3D11CommandList *>(pCommandList);
    std::vector<int> query_list;
#include "execute-capture-under-test.hpp"
  }
  ~QueueCapture() { list.reset(); }
};
}
using namespace dxmt;
static unsigned failures = 0;
void check(bool condition, const char *name) {
  if (!condition) { std::cerr << "FAIL " << name << '\n'; ++failures; }
}

void check_context_public_reference() {
  Observation observed;
  auto *device = new DeviceOwner(observed);
  ImmediateContext context(device);
  context.AddRef();
  check(device->Release() == 1 && !observed.destroyed, "public context pins device");
  check(context.Release() == 0 && observed.destroyed && !observed.destroyed_on_finish,
        "last public context releases device on API");
}

void check_weak_child_reference() {
  Observation observed;
  auto *device = new DeviceOwner(observed);
  Com<Query> child = new Query(device);
  check(device->Release() == 0 && observed.destroyed, "ordinary query child does not pin parent");
  child = nullptr;
}

void check_public_managed_reference() {
  Observation observed;
  auto *device = new DeviceOwner(observed);
  MTLD3D11CommandListPoolBase pool{observed};
  auto object = std::make_unique<MTLD3D11CommandList>(device, &pool);
  Com<MTLD3D11CommandList> command = object.get();
  check(device->Release() == 1 && !observed.destroyed, "public commandlist pins device");
  command = nullptr;
  check(observed.destroyed && !observed.destroyed_on_finish && !observed.recycle_after_destroy,
        "public commandlist recycles before final parent release");
  // The scaffold's default destructor only destroys already-empty fields.
  // Actual pool destruction is outside this reference-chain fixture.
}

void check_finish_private_data_reference() {
#ifdef TEST_RETIREMENT
  mocktp::Ledger ledger;
  mocktp::active = &ledger;
#endif
  Observation observed;
  auto *device = new DeviceOwner(observed);
  ImmediateContext context(device);
  context.AddRef();
  MTLD3D11CommandListPoolBase pool{observed};
  auto object = std::make_unique<MTLD3D11CommandList>(device, &pool);
  Com<MTLD3D11CommandList> command = object.get();
  Com<Query> query = new Query(device);
  check(query->SetPrivateDataInterface(7, device) == S_OK, "query private data retains real device");
  command->issued_timestamp_query.push_back(query);
  QueueCapture queue;
  queue.executeCommandList(command.ptr());
  query = nullptr;
  command = nullptr;
  context.Release();
  check(device->Release() == 1 && !observed.destroyed, "query private data is final parent reference");
  std::thread finish([&] {
    current_role = Role::Finish;
    ArgumentEncodingContext enc;
    queue.list.execute(enc);
    queue.list.reset();
  });
  finish.join();
#ifdef TEST_RETIREMENT
  check(!observed.destroyed && observed.recycle_after_destroy == 0,
        "real private-data chain returns from finish before destruction");
  FreeLibrary(&ledger);
  check(ledger.modules == 1, "real chain keeps callback module pin");
  { std::lock_guard lock(observed.mutex); observed.allow_drain = true; }
  observed.cv.notify_all();
  ledger.join();
  check(observed.destroyed && observed.destroyed_on_worker && !observed.destroyed_on_finish
            && observed.queue_shutdown == 1 && observed.recycle_after_destroy == 0,
        "real private-data chain retires on independent worker");
  check(ledger.modules == 0 && ledger.submissions == 1 && ledger.callbacks == 1,
        "real chain submits exactly once and releases module after callback");
#else
  check(observed.destroyed && observed.destroyed_on_finish,
        "actual private capture retirement reaches final device release on finish");
  check(observed.recycle_after_destroy == 1,
        "recycle resumes after device destruction would retire pool");
#endif
  // The production owner owns and destroys the pool/object. This shell keeps
  // those observability seams outliving the modeled owner to avoid mock UAF.
}

int main() {
#ifndef TEST_RETIREMENT
  check_context_public_reference();
  check_weak_child_reference();
  check_public_managed_reference();
#endif
  check_finish_private_data_reference();
  if (failures) return 1;
#ifdef TEST_RETIREMENT
  std::cout << "PASS integrated retirement: actual private-data capture chain\n";
#else
  std::cout << "PASS source ownership: API final release and finish-thread final release\n";
#endif
}
