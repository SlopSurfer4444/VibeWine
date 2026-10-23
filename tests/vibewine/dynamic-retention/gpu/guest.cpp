#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// Headless Dynamic buffer/linear-texture ownership probe. No window, swapchain, shader, game,
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
constexpr unsigned kCopies = 32, kDeferredCopies = 16, kProgressChunks = 384;
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
struct BufferKind {
  using Resource = ID3D11Buffer;
  static constexpr const char* name = "buffer";
  static constexpr UINT rows = 1, rowBytes = kBytes;
  static UINT pitch(const D3D11_MAPPED_SUBRESOURCE&) { return rowBytes; }
  static void create(ID3D11Device* device, D3D11_USAGE usage, Resource** out) {
    D3D11_BUFFER_DESC d{}; d.ByteWidth = kBytes; d.Usage = usage;
    if (usage == D3D11_USAGE_DYNAMIC) { d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; d.BindFlags = D3D11_BIND_VERTEX_BUFFER; }
    if (usage == D3D11_USAGE_STAGING) d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    step(device->CreateBuffer(&d, nullptr, out), "CreateBuffer");
  }
};
struct TextureKind {
  using Resource = ID3D11Texture2D;
  static constexpr const char* name = "linear_texture";
  static constexpr UINT rows = 128, rowBytes = 128 * 4;
  static UINT pitch(const D3D11_MAPPED_SUBRESOURCE& m) { require(m.RowPitch >= rowBytes, "texture_row_pitch"); return m.RowPitch; }
  static void create(ID3D11Device* device, D3D11_USAGE usage, Resource** out) {
    D3D11_TEXTURE2D_DESC d{}; d.Width = 128; d.Height = rows; d.MipLevels = 1; d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1; d.Usage = usage;
    if (usage == D3D11_USAGE_DYNAMIC) { d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; d.BindFlags = D3D11_BIND_SHADER_RESOURCE; }
    if (usage == D3D11_USAGE_STAGING) d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    step(device->CreateTexture2D(&d, nullptr, out), "CreateTexture2D");
  }
};
static std::vector<unsigned char> pattern(unsigned seed) {
  std::vector<unsigned char> bytes(kBytes);
  for (UINT i = 0; i < kBytes; ++i) bytes[i] = static_cast<unsigned char>((i * 37u + (i >> 8) + seed * 73u + (seed >> 3)) & 255u);
  return bytes;
}
static void mapRead(ID3D11DeviceContext* context, ID3D11Resource* resource, D3D11_MAPPED_SUBRESOURCE* mapped) {
  const auto begin = GetTickCount64();
  for (;;) {
    budget();
    const auto hr = context->Map(resource, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, mapped);
    if (hr != DXGI_ERROR_WAS_STILL_DRAWING) { step(hr, "Map(readback READ no-wait)"); return; }
    // GPU EVENT completion precedes the finish thread's CPU-coherent signal.
    // Retry only this documented transient result, without a blocking Map.
    require(GetTickCount64() - begin < kQueryMs, "readback_cpu_coherence_5s", 12);
    Sleep(1);
  }
}
template<class Kind> struct Probe {
  using Resource = typename Kind::Resource;
  ID3D11Device* device;
  ID3D11DeviceContext* immediate;
  Ref<ID3D11DeviceContext> deferred;
  Ref<Resource> source, read;
  std::array<Ref<Resource>, kCopies> snapshots;
  std::array<Ref<ID3D11CommandList>, 2> retained;
  std::array<std::array<std::vector<unsigned char>, kDeferredCopies>, 2> golden;
  Probe(ID3D11Device* d, ID3D11DeviceContext* c) : device(d), immediate(c) {
    Kind::create(device, D3D11_USAGE_DYNAMIC, source.out());
    Kind::create(device, D3D11_USAGE_STAGING, read.out());
    for (auto& snapshot : snapshots) Kind::create(device, D3D11_USAGE_DEFAULT, snapshot.out());
    step(device->CreateDeferredContext(0, deferred.out()), "CreateDeferredContext");
  }
  void write(ID3D11DeviceContext* context, const std::vector<unsigned char>& expected) {
    budget();
    D3D11_MAPPED_SUBRESOURCE mapped{};
    step(context->Map(source.p, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map(dynamic WRITE_DISCARD)");
    try {
      require(mapped.pData != nullptr, "dynamic_map_pointer");
      const auto pitch = Kind::pitch(mapped);
      for (UINT row = 0; row < Kind::rows; ++row)
        std::memcpy(static_cast<unsigned char*>(mapped.pData) + row * pitch, expected.data() + row * Kind::rowBytes, Kind::rowBytes);
    } catch (...) { context->Unmap(source.p, 0); throw; }
    context->Unmap(source.p, 0);
  }
  void readback(unsigned index, const std::vector<unsigned char>& expected) {
    immediate->CopyResource(read.p, snapshots[index].p);
    complete(device, immediate, "snapshot_readback");
    D3D11_MAPPED_SUBRESOURCE mapped{};
    mapRead(immediate, read.p, &mapped);
    try {
      require(mapped.pData != nullptr, "readback_pointer");
      const auto pitch = Kind::pitch(mapped);
      for (UINT row = 0; row < Kind::rows; ++row) {
        const auto* actual = static_cast<const unsigned char*>(mapped.pData) + row * pitch;
        for (UINT column = 0; column < Kind::rowBytes; ++column) {
          const auto wanted = expected[row * Kind::rowBytes + column];
          if (actual[column] != wanted) {
            std::printf("FAIL bytes kind=%s snapshot=%u row=%u byte=%u actual=%u expected=%u\n", Kind::name, index, row, column, actual[column], wanted);
            throw Failure{11};
          }
        }
      }
      ++checks;
    } catch (...) { immediate->Unmap(read.p, 0); throw; }
    immediate->Unmap(read.p, 0);
  }
  void burst(unsigned round) {
    std::array<std::vector<unsigned char>, kCopies> expected;
    for (unsigned i = 0; i < kCopies; ++i) {
      expected[i] = pattern(1000 + round * 100 + i);
      write(immediate, expected[i]);
      immediate->CopyResource(snapshots[i].p, source.p);
    }
    complete(device, immediate, "immediate_burst");
    for (unsigned i = 0; i < kCopies; ++i) readback(i, expected[i]);
    std::printf("IMMEDIATE_PASS kind=%s round=%u snapshots=%u\n", Kind::name, round, kCopies);
  }
  void record(unsigned round) {
    for (unsigned finishRestore = 0; finishRestore < 2; ++finishRestore) {
      require(retained[finishRestore].p == nullptr, "previous_retained_list_released");
      for (unsigned i = 0; i < kDeferredCopies; ++i) {
        golden[finishRestore][i] = pattern(2000 + round * 100 + finishRestore * 33 + i);
        write(deferred.p, golden[finishRestore][i]);
        deferred->CopyResource(snapshots[i].p, source.p);
      }
      step(deferred->FinishCommandList(finishRestore ? TRUE : FALSE, retained[finishRestore].out()), "FinishCommandList");
      require(retained[finishRestore].p != nullptr, "recorded_command_list");
      std::printf("RECORDED kind=%s round=%u finish_restore=%u maps=%u\n", Kind::name, round, finishRestore, kDeferredCopies);
    }
  }
  void replay(unsigned round, const char* phase) {
    for (unsigned finishRestore = 0; finishRestore < 2; ++finishRestore) {
      for (unsigned executeRestore = 0; executeRestore < 2; ++executeRestore) {
        // Mutate immediate name before each replay: the retained command list
        // must resolve its own recorded allocations, not this new CPU data.
        write(immediate, pattern(9000 + finishRestore * 10 + executeRestore));
        immediate->ExecuteCommandList(retained[finishRestore].p, executeRestore ? TRUE : FALSE);
        complete(device, immediate, "deferred_execute");
        for (unsigned i = 0; i < kDeferredCopies; ++i) readback(i, golden[finishRestore][i]);
        std::printf("DEFERRED_PASS kind=%s round=%u phase=%s finish_restore=%u execute_restore=%u snapshots=%u\n",
                    Kind::name, round, phase, finishRestore, executeRestore, kDeferredCopies);
      }
    }
  }
  void releaseLists() { for (auto& list : retained) list.out(); }
};
// EVENT completion precedes CPU chunk reset/coherence. Allow 300 sequences plus
// two 32-chunk pipeline depths and 20 margin; actual trim counts remain opaque.
static void age(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Buffer* ping, ID3D11Buffer* pong, const char* phase) {
  for (unsigned i = 0; i < kProgressChunks; ++i) {
    context->CopyResource((i & 1) ? ping : pong, (i & 1) ? pong : ping);
    complete(device, context, "aging_copy");
  }
  std::printf("AGED_PASS phase=%s completed_copy_queries=%u\n", phase, kProgressChunks);
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
  UINT support = 0;
  step(device->CheckFormatSupport(DXGI_FORMAT_R8G8B8A8_UNORM, &support), "CheckFormatSupport(RGBA8)");
  require((support & D3D11_FORMAT_SUPPORT_TEXTURE2D) && (support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE), "RGBA8_texture_support");
  std::printf("BEGIN bytes=%u immediate_copies=%u deferred_copies=%u age_queries=%u budget_ms=%llu texture_format=RGBA8\n",
              kBytes, kCopies, kDeferredCopies, kProgressChunks, kBudgetMs);
  Probe<BufferKind> buffers(device.p, context.p);
  Probe<TextureKind> textures(device.p, context.p);
  Ref<ID3D11Buffer> ping, pong;
  BufferKind::create(device.p, D3D11_USAGE_DEFAULT, ping.out());
  BufferKind::create(device.p, D3D11_USAGE_DEFAULT, pong.out());
  const auto seed = pattern(88);
  context->UpdateSubresource(ping.p, 0, nullptr, seed.data(), 0, 0);
  context->CopyResource(pong.p, ping.p);
  complete(device.p, context.p, "progress_seed");
  buffers.burst(0); textures.burst(0);
  buffers.record(0); textures.record(0);
  buffers.replay(0, "retained_before_age"); textures.replay(0, "retained_before_age");
  age(device.p, context.p, ping.p, pong.p, "lists_retained");
  buffers.replay(0, "retained_after_age"); textures.replay(0, "retained_after_age");
  buffers.releaseLists(); textures.releaseLists();
  age(device.p, context.p, ping.p, pong.p, "lists_recycled");
  // These next DISCARD maps exercise lazy pool reclamation after completion age.
  buffers.burst(1); textures.burst(1);
  buffers.record(1); textures.record(1);
  buffers.replay(1, "recorded_after_age"); textures.replay(1, "recorded_after_age");
  buffers.releaseLists(); textures.releaseLists();
  context->ClearState(); context->Flush();
  require(device->GetDeviceRemovedReason() == S_OK, "device_not_removed");
  budget();
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
