#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// Headless buffer-copy correctness probe. No window, swapchain, shader, game,
// external file access, child process, or runtime selection changes.
struct Failure { int code; };
template<class T> struct Ref {
  T* p = nullptr;
  Ref() = default;
  Ref(const Ref&) = delete;
  Ref& operator=(const Ref&) = delete;
  ~Ref() { if (p) p->Release(); }
  T** out() { if (p) p->Release(); p = nullptr; return &p; }
  T* operator->() const { return p; }
};
constexpr UINT kBytes = 64 * 1024;
constexpr unsigned kCopies = 32, kRounds = 6, kProgressChunks = 320;
constexpr ULONGLONG kBudgetMs = 45000, kQueryMs = 5000;
static ULONGLONG started;
static HANDLE finished;
static unsigned checks = 0, completedQueries = 0;
static void require(bool valid, const char* name, int code = 11) {
  if (!valid) { std::printf("FAIL check=%s elapsed_ms=%llu\n", name, GetTickCount64() - started); throw Failure{code}; }
  ++checks;
}
static void step(HRESULT hr, const char* name) {
  if (FAILED(hr)) { std::printf("FAIL api=%s hr=0x%08lx elapsed_ms=%llu\n", name, static_cast<unsigned long>(hr), GetTickCount64() - started); throw Failure{10}; }
}
static void budget() { require(GetTickCount64() - started < kBudgetMs, "global_45s_budget", 12); }
static DWORD WINAPI watchdog(void*) {
  if (WaitForSingleObject(finished, 55000) != WAIT_OBJECT_0) {
    std::fprintf(stderr, "FAIL watchdog_55s\n"); std::fflush(nullptr);
    TerminateProcess(GetCurrentProcess(), 124);
  }
  return 0;
}
static void module(const wchar_t* name) {
  wchar_t path[32768] = {};
  auto handle = GetModuleHandleW(name);
  require(handle && GetModuleFileNameW(handle, path, 32768), "required_module_loaded", 10);
  std::printf("MODULE %ls %ls\n", name, path);
}
static void complete(ID3D11Device* device, ID3D11DeviceContext* context, const char* phase) {
  budget();
  D3D11_QUERY_DESC description{D3D11_QUERY_EVENT, 0};
  Ref<ID3D11Query> query;
  step(device->CreateQuery(&description, query.out()), "CreateQuery(EVENT)");
  context->End(query.p);
  context->Flush();
  const auto begin = GetTickCount64();
  for (;;) {
    BOOL done = FALSE;
    HRESULT hr = context->GetData(query.p, &done, sizeof(done), D3D11_ASYNC_GETDATA_DONOTFLUSH);
    step(hr, "GetData(EVENT)");
    if (hr == S_OK && done) { ++completedQueries; return; }
    if (GetTickCount64() - begin >= kQueryMs || GetTickCount64() - started >= kBudgetMs) {
      std::printf("FAIL query_timeout phase=%s hr=0x%08lx removed=0x%08lx\n", phase,
                  static_cast<unsigned long>(hr), static_cast<unsigned long>(device->GetDeviceRemovedReason()));
      throw Failure{12};
    }
    Sleep(1);
  }
}
static void compare(const void* actual, const std::vector<unsigned char>& expected, const char* phase) {
  const auto* bytes = static_cast<const unsigned char*>(actual);
  for (UINT i = 0; i < kBytes; ++i) {
    if (bytes[i] != expected[i]) {
      std::printf("FAIL bytes phase=%s offset=%u actual=%u expected=%u\n", phase, i, bytes[i], expected[i]);
      throw Failure{11};
    }
  }
  ++checks;
}
static void sourceCheck(ID3D11DeviceContext* context, ID3D11Buffer* source,
                        const std::vector<unsigned char>& expected, const char* phase) {
  budget();
  D3D11_MAPPED_SUBRESOURCE mapped{};
  step(context->Map(source, 0, D3D11_MAP_READ_WRITE, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped), "Map(source READ_WRITE no-wait)");
  try { require(mapped.pData != nullptr, "mapped_source_pointer"); compare(mapped.pData, expected, phase); }
  catch (...) { context->Unmap(source, 0); throw; }
  context->Unmap(source, 0);
}
static void readback(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Buffer* gpu,
                     ID3D11Buffer* read, const std::vector<unsigned char>& expected) {
  context->CopyResource(read, gpu);
  complete(device, context, "snapshot_readback");
  D3D11_MAPPED_SUBRESOURCE mapped{};
  step(context->Map(read, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped), "Map(readback READ no-wait)");
  try { require(mapped.pData != nullptr, "mapped_readback_pointer"); compare(mapped.pData, expected, "GPU_snapshot"); }
  catch (...) { context->Unmap(read, 0); throw; }
  context->Unmap(read, 0);
}
static void workflow() {
  Ref<ID3D11Device> device;
  Ref<ID3D11DeviceContext> context;
  D3D_FEATURE_LEVEL level{};
  const D3D_FEATURE_LEVEL requested[] = {D3D_FEATURE_LEVEL_11_0};
  step(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, requested, 1,
                         D3D11_SDK_VERSION, device.out(), &level, context.out()), "D3D11CreateDevice");
  require(level == D3D_FEATURE_LEVEL_11_0, "feature_level_11_0");
  module(L"d3d11.dll"); module(L"dxgi.dll"); module(L"winemetal.dll");
  std::printf("BEGIN bytes=%u copies=%u rounds=%u progress_chunks=%u budget_ms=%llu\n",
              kBytes, kCopies, kRounds, kProgressChunks, kBudgetMs);
  D3D11_BUFFER_DESC staging{};
  staging.ByteWidth = kBytes; staging.Usage = D3D11_USAGE_STAGING;
  staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
  Ref<ID3D11Buffer> source, read;
  step(device->CreateBuffer(&staging, nullptr, source.out()), "CreateBuffer(source staging RW)");
  staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  step(device->CreateBuffer(&staging, nullptr, read.out()), "CreateBuffer(read staging)");
  D3D11_BUFFER_DESC gpu{}; gpu.ByteWidth = kBytes; gpu.Usage = D3D11_USAGE_DEFAULT;
  std::array<Ref<ID3D11Buffer>, kCopies> snapshots;
  for (auto& buffer : snapshots) step(device->CreateBuffer(&gpu, nullptr, buffer.out()), "CreateBuffer(snapshot DEFAULT)");
  Ref<ID3D11Buffer> ping, pong;
  step(device->CreateBuffer(&gpu, nullptr, ping.out()), "CreateBuffer(progress ping)");
  step(device->CreateBuffer(&gpu, nullptr, pong.out()), "CreateBuffer(progress pong)");
  std::vector<unsigned char> expected(kBytes);
  for (UINT i = 0; i < kBytes; ++i) expected[i] = static_cast<unsigned char>((i * 37u + (i >> 8u) + 91u) & 255u);
  D3D11_MAPPED_SUBRESOURCE initial{};
  step(context->Map(source.p, 0, D3D11_MAP_READ_WRITE, D3D11_MAP_FLAG_DO_NOT_WAIT, &initial), "Map(initial source)");
  require(initial.pData != nullptr, "initial_source_pointer");
  std::memcpy(initial.pData, expected.data(), kBytes); context->Unmap(source.p, 0);
  std::array<std::vector<unsigned char>, kCopies> golden;
  for (unsigned round = 0; round < kRounds; ++round) {
    budget();
    for (unsigned copy = 0; copy < kCopies; ++copy) {
      golden[copy] = expected;
      context->CopyResource(snapshots[copy].p, source.p);
      D3D11_MAPPED_SUBRESOURCE mapped{};
      step(context->Map(source.p, 0, D3D11_MAP_READ_WRITE, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped), "Map(rename staging RW)");
      try {
        require(mapped.pData != nullptr, "rename_pointer");
        compare(mapped.pData, expected, "partial_write_preserves_old_bytes");
        // Changing only 257 bytes forces preservation of the remaining source.
        const UINT offset = (round * 4999u + copy * 2017u) % (kBytes - 257u);
        for (UINT j = 0; j < 257; ++j)
          expected[offset + j] = static_cast<unsigned char>((round * 73u + copy * 11u + j * 17u + 1u) & 255u);
        std::memcpy(static_cast<unsigned char*>(mapped.pData) + offset, expected.data() + offset, 257);
      } catch (...) { context->Unmap(source.p, 0); throw; }
      context->Unmap(source.p, 0);
    }
    complete(device.p, context.p, "burst_complete");
    for (unsigned copy = 0; copy < kCopies; ++copy) readback(device.p, context.p, snapshots[copy].p, read.p, golden[copy]);
    sourceCheck(context.p, source.p, expected, "source_after_burst");
    std::printf("BURST_PASS round=%u snapshots=%u\n", round, kCopies); std::fflush(stdout);
    if (round == 1 || round == 3 || round == 5) {
      context->CopyResource(ping.p, snapshots[kCopies - 1].p);
      context->CopyResource(pong.p, snapshots[kCopies - 1].p);
      complete(device.p, context.p, "progress_seed");
      for (unsigned i = 0; i < kProgressChunks; ++i) {
        budget();
        context->CopyResource((i & 1) ? ping.p : pong.p, (i & 1) ? pong.p : ping.p);
        complete(device.p, context.p, "coherent_progress");
      }
      // This next Map is the lazy cleanup trigger; counts are intentionally not inferred.
      sourceCheck(context.p, source.p, expected, "source_after_320_completed_chunks");
      readback(device.p, context.p, ping.p, read.p, golden[kCopies - 1]);
      std::printf("AGED_PASS round=%u completed_copy_chunks=%u\n", round, kProgressChunks); std::fflush(stdout);
    }
  }
  context->ClearState(); context->Flush();
  require(device->GetDeviceRemovedReason() == S_OK, "device_not_removed");
  std::printf("PASS checks=%u completed_queries=%u elapsed_ms=%llu\n", checks, completedQueries, GetTickCount64() - started);
}
int main() {
  setvbuf(stdout, nullptr, _IONBF, 0); started = GetTickCount64();
  finished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!finished) return 13;
  HANDLE guard = CreateThread(nullptr, 0, watchdog, nullptr, 0, nullptr);
  if (!guard) { CloseHandle(finished); return 13; }
  int result = 0;
  try { workflow(); } catch (Failure failure) { result = failure.code; }
  catch (...) { std::fprintf(stderr, "FAIL unexpected_exception\n"); result = 14; }
  SetEvent(finished); WaitForSingleObject(guard, 1000); CloseHandle(guard); CloseHandle(finished);
  return result;
}
