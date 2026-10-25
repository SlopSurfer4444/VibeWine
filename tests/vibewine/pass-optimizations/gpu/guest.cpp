// Copyright 2026 VibeWine contributors.
// SPDX-License-Identifier: LGPL-2.1-or-later
// Reuses the bounded headless readback patterns in ../../gpu-wave/guest.cpp.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <array>
#include <cstdio>
#include <cstring>

template<class T> struct Ref {
  T *p = nullptr;
  ~Ref() { if (p) p->Release(); }
  T **out() { if (p) p->Release(); p = nullptr; return &p; }
  T *operator->() const { return p; }
};
struct Failure { int code = 1; };
constexpr UINT width = 64, height = 64;
static ULONGLONG started;
static HANDLE finished;
static unsigned cases;
static bool negative;
static void require(bool valid, const char *detail) {
  if (!valid) { std::printf("FAIL %s\n", detail); throw Failure{}; }
}
static void step(HRESULT hr, const char *detail) {
  if (FAILED(hr)) { std::printf("FAIL %s hr=%08lx\n", detail, static_cast<unsigned long>(hr)); throw Failure{}; }
}
static void deadline(ULONGLONG begin) {
  require(GetTickCount64() - begin < 5000 && GetTickCount64() - started < 30000, "deadline");
}
static DWORD WINAPI watchdog(void *) {
  if (WaitForSingleObject(finished, 40000) != WAIT_OBJECT_0) TerminateProcess(GetCurrentProcess(), 124);
  return 0;
}
static void module(const wchar_t *name) {
  wchar_t path[32768]{};
  const auto handle = GetModuleHandleW(name);
  require(handle && GetModuleFileNameW(handle, path, 32768), "module_path");
  std::printf("MODULE %ls %ls\n", name, path);
}
static void complete(ID3D11Device *device, ID3D11DeviceContext *context) {
  Ref<ID3D11Query> query; D3D11_QUERY_DESC d{D3D11_QUERY_EVENT, 0};
  step(device->CreateQuery(&d, query.out()), "event_create");
  context->End(query.p); context->Flush();
  const auto begin = GetTickCount64();
  for (;;) {
    BOOL done = FALSE;
    const auto hr = context->GetData(query.p, &done, sizeof(done), D3D11_ASYNC_GETDATA_DONOTFLUSH);
    step(hr, "event_getdata");
    if (hr == S_OK && done) return;
    deadline(begin); Sleep(1);
  }
}
static void texture(ID3D11Device *device, Ref<ID3D11Texture2D> &out, D3D11_USAGE usage, UINT flags,
                    const void *pixels = nullptr) {
  D3D11_TEXTURE2D_DESC d{}; d.Width = width; d.Height = height; d.MipLevels = d.ArraySize = 1;
  d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1; d.Usage = usage; d.BindFlags = flags;
  if (usage == D3D11_USAGE_STAGING) d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  const D3D11_SUBRESOURCE_DATA data{pixels, width * 4, 0};
  step(device->CreateTexture2D(&d, pixels ? &data : nullptr, out.out()), "texture_create");
}
static void image(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11Texture2D *source,
                  std::array<unsigned char, 4> left, std::array<unsigned char, 4> right) {
  Ref<ID3D11Texture2D> staging; texture(device, staging, D3D11_USAGE_STAGING, 0);
  context->CopyResource(staging.p, source); complete(device, context);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  const auto begin = GetTickCount64();
  for (;;) {
    const auto hr = context->Map(staging.p, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
    if (hr == S_OK) break;
    if (hr != DXGI_ERROR_WAS_STILL_DRAWING) step(hr, "readback_map");
    deadline(begin); Sleep(1);
  }
  bool correct = mapped.pData && mapped.RowPitch >= width * 4, corrupted_matches = correct;
  if (correct) for (UINT y = 0; y < height; ++y) for (UINT x = 0; x < width; ++x) {
    auto expected = x < width / 2 ? left : right;
    const auto *actual = static_cast<const unsigned char *>(mapped.pData) + y * mapped.RowPitch + x * 4;
    correct &= !std::memcmp(actual, expected.data(), 4);
    if (x == 0 && y == 0) expected[0] ^= 1;
    corrupted_matches &= !std::memcmp(actual, expected.data(), 4);
  }
  context->Unmap(staging.p, 0);
  require(correct, "image_all_pixels_exact");
  if (negative) {
    require(!corrupted_matches, "negative_golden_unexpected_match");
    std::printf("NEGATIVE_REJECTED pixel\n");
    throw Failure{20};
  }
}
static const char shader[] = R"(
Texture2D<float4> white : register(t0);
float4 vs_main(uint id:SV_VertexID):SV_POSITION {
  return float4(id==2 ? 3.0 : -1.0, id==1 ? 3.0 : -1.0, white.Load(int3(0,0,0)).r * 0.5, 1.0);
}
float4 ps_red():SV_TARGET { return white.Load(int3(0,0,0)) * float4(1,0,0,1); }
float4 ps_blue():SV_TARGET { return white.Load(int3(0,0,0)) * float4(0,0,1,1); }
)";
static void compile(const char *entry, const char *target, Ref<ID3DBlob> &code) {
  Ref<ID3DBlob> errors;
  step(D3DCompile(shader, sizeof(shader) - 1, "pass-optimizations", nullptr, nullptr, entry, target,
                  D3DCOMPILE_ENABLE_STRICTNESS, 0, code.out(), errors.out()), "compile");
}
static void workflow() {
  Ref<IDXGIFactory1> factory;
  step(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(factory.out())), "dxgi_factory");
  Ref<ID3D11Device> device; Ref<ID3D11DeviceContext> context;
  D3D_FEATURE_LEVEL level{}, requested = D3D_FEATURE_LEVEL_11_0;
  step(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &requested, 1, D3D11_SDK_VERSION,
                         device.out(), &level, context.out()), "device");
  require(level == requested, "feature_level");
  module(L"d3d11.dll"); module(L"dxgi.dll"); module(L"winemetal.dll"); module(L"d3dcompiler_47.dll");
  wchar_t mapping_ack[32768]{};
  const auto ack_length = GetEnvironmentVariableW(L"VIBEWINE_GPU_MAP_ACK", mapping_ack, 32768);
  require(ack_length < 32768, "module_mapping_ack_path");
  if (ack_length) {
    const auto map_started = GetTickCount64();
    while (GetFileAttributesW(mapping_ack) == INVALID_FILE_ATTRIBUTES) {
      require(GetTickCount64() - map_started < 15000, "module_mapping_ack_deadline");
      Sleep(10);
    }
  }
  Ref<ID3DBlob> code; Ref<ID3D11VertexShader> vs; Ref<ID3D11PixelShader> ps, blue_ps;
  compile("vs_main", "vs_5_0", code);
  step(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, vs.out()), "vs");
  compile("ps_red", "ps_5_0", code);
  step(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, ps.out()), "ps");
  compile("ps_blue", "ps_5_0", code);
  step(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, blue_ps.out()), "blue_ps");
  Ref<ID3D11Texture2D> target, snapshot, white; Ref<ID3D11RenderTargetView> rtv;
  texture(device.p, target, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET);
  texture(device.p, snapshot, D3D11_USAGE_DEFAULT, 0);
  step(device->CreateRenderTargetView(target.p, nullptr, rtv.out()), "rtv");
  std::array<unsigned char, width * height * 4> ones{}; ones.fill(255);
  texture(device.p, white, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, ones.data());
  Ref<ID3D11ShaderResourceView> srv;
  step(device->CreateShaderResourceView(white.p, nullptr, srv.out()), "srv");
  Ref<ID3D11RasterizerState> raster; D3D11_RASTERIZER_DESC rd{};
  rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE; rd.ScissorEnable = TRUE;
  step(device->CreateRasterizerState(&rd, raster.out()), "raster");
  context->VSSetShader(vs.p, nullptr, 0); context->PSSetShader(ps.p, nullptr, 0);
  context->VSSetShaderResources(0, 1, &srv.p); context->PSSetShaderResources(0, 1, &srv.p);
  context->RSSetState(raster.p);
  D3D11_VIEWPORT viewport{0, 0, float(width), float(height), 0, 1}; context->RSSetViewports(1, &viewport);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  constexpr std::array<unsigned char, 4> red{255,0,0,255}, green{0,255,0,255}, blue{0,0,255,255};
  const FLOAT greenf[]{0,1,0,1};
  const D3D11_RECT full{0,0,LONG(width),LONG(height)}, half{0,0,LONG(width / 2),LONG(height)};
  for (unsigned mode = 0; mode < 4; ++mode) {
    context->OMSetRenderTargets(1, &rtv.p, nullptr); context->RSSetScissorRects(1, &full);
    context->PSSetShader(ps.p, nullptr, 0); context->Draw(3, 0);
    if (mode == 2) context->CopyResource(snapshot.p, target.p); // Reader must retain predecessor Store.
    if (mode == 3) {
      context->OMSetRenderTargets(0, nullptr, nullptr); context->OMSetRenderTargets(1, &rtv.p, nullptr);
    } else context->ClearRenderTargetView(rtv.p, greenf);
    if (mode != 1) {
      context->RSSetScissorRects(1, &half);
      context->PSSetShader(blue_ps.p, nullptr, 0); context->Draw(3, 0);
    }
    context->OMSetRenderTargets(0, nullptr, nullptr);
    if (mode == 2) image(device.p, context.p, snapshot.p, red, red);
    image(device.p, context.p, target.p, mode == 1 ? green : blue, mode == 3 ? red : green);
    ++cases; std::printf("CASE_PASS mode=%u\n", mode);
  }
}
int main(int argc, char **argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  started = GetTickCount64(); negative = argc == 2 && !std::strcmp(argv[1], "--negative-pixel");
  if (argc != 1 && !negative) return 2;
  finished = CreateEventW(nullptr, TRUE, FALSE, nullptr); if (!finished) return 3;
  HANDLE thread = CreateThread(nullptr, 0, watchdog, nullptr, 0, nullptr); if (!thread) return 3;
  int result = 0;
  try { workflow(); require(cases == 4, "case_count"); std::printf("PASS cases=4 images=5\n"); }
  catch (const Failure &failure) { result = failure.code; }
  SetEvent(finished); WaitForSingleObject(thread, INFINITE); CloseHandle(thread); CloseHandle(finished);
  return result;
}
