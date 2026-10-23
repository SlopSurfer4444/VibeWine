// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright 2026 VibeWine contributors.
// Reuses local procedural shader and bounded readback/query fixture patterns;
// see README.md for source provenance and the isolation/acceptance boundary.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

template<class T> struct Ref {
  T *p = nullptr;
  Ref() = default;
  Ref(const Ref &) = delete;
  Ref &operator=(const Ref &) = delete;
  ~Ref() { if (p) p->Release(); }
  T **out() { if (p) p->Release(); p = nullptr; return &p; }
  T *operator->() const { return p; }
};
struct Failure { const char *detail; HRESULT hr; bool unsupported = false, negative = false; };
constexpr UINT kWidth = 64, kHeight = 64;
constexpr ULONGLONG kBudgetMs = 90000, kWaitMs = 5000;
static ULONGLONG started;
static HANDLE finished;
static const char *negative_control = "none", *stage_name = nullptr;
static unsigned checks, stage_start_checks, reported_stage_checks, stages_passed, stages_unsupported;

static void json_string(const char *text) {
  std::putchar('"');
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p; ++p) {
    if (*p == '"' || *p == '\\') { std::putchar('\\'); std::putchar(*p); }
    else if (*p < 32 || *p == 127) std::printf("\\u%04x", unsigned(*p));
    else std::putchar(*p);
  }
  std::putchar('"');
}
static void require(bool ok, const char *detail, bool negative = false) {
  if (!ok) throw Failure{detail, E_FAIL, false, negative};
  ++checks;
}
static void step(HRESULT hr, const char *detail) {
  if (FAILED(hr)) throw Failure{detail, hr};
  ++checks;
}
static void budget() { require(GetTickCount64() - started < kBudgetMs, "global_deadline"); }
static bool selected(const char *name) { return std::strcmp(negative_control, name) == 0; }
static void report_stage(const char *status, const char *detail, HRESULT hr = S_OK) {
  reported_stage_checks += checks - stage_start_checks;
  std::printf("{\"schema\":1,\"type\":\"stage\",\"name\":"); json_string(stage_name);
  std::printf(",\"status\":"); json_string(status);
  std::printf(",\"checks\":%u,\"detail\":", checks - stage_start_checks); json_string(detail);
  std::printf(",\"hr\":\"0x%08lx\"}\n", static_cast<unsigned long>(hr));
}
static void summary(const char *status, unsigned rejected = 0, const char *detail = "complete", HRESULT hr = S_OK) {
  std::printf("{\"schema\":1,\"type\":\"summary\",\"status\":"); json_string(status);
  std::printf(",\"stages_passed\":%u,\"stages_unsupported\":%u,\"checks\":%u,\"non_stage_checks\":%u,\"negative_controls_rejected\":%u,\"detail\":",
              stages_passed, stages_unsupported, checks, checks - reported_stage_checks, rejected);
  json_string(detail); std::printf(",\"hr\":\"0x%08lx\"}\n", static_cast<unsigned long>(hr));
}
static DWORD WINAPI watchdog(void *) {
  if (WaitForSingleObject(finished, 100000) != WAIT_OBJECT_0) {
    // No competing JSON writes: the parent treats timeout/exit124 as failure.
    TerminateProcess(GetCurrentProcess(), 124);
  }
  return 0;
}
static void module(const wchar_t *name, const char *label) {
  wchar_t path[2048]{}; char utf8[8192]{};
  auto handle = GetModuleHandleW(name);
  const DWORD size = handle ? GetModuleFileNameW(handle, path, 2048) : 0;
  require(size > 0 && size < 2048, "required_module_path");
  require(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, utf8, sizeof(utf8), nullptr, nullptr) > 0,
          "module_path_utf8");
  std::printf("{\"schema\":1,\"type\":\"module\",\"name\":"); json_string(label);
  std::printf(",\"path\":"); json_string(utf8); std::printf("}\n");
}
static void complete(ID3D11Device *device, ID3D11DeviceContext *context) {
  budget();
  D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT, 0}; Ref<ID3D11Query> query;
  step(device->CreateQuery(&desc, query.out()), "event_query_create");
  context->End(query.p); context->Flush();
  const ULONGLONG begin = GetTickCount64();
  for (;;) {
    BOOL done = FALSE;
    const HRESULT hr = context->GetData(query.p, &done, sizeof(done), D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if (FAILED(hr)) step(hr, "event_query_getdata");
    if (hr == S_OK && done) { ++checks; return; }
    require(GetTickCount64() - begin < kWaitMs && GetTickCount64() - started < kBudgetMs, "event_query_deadline");
    Sleep(1);
  }
}
struct Mapping {
  ID3D11DeviceContext *context; ID3D11Resource *resource;
  D3D11_MAPPED_SUBRESOURCE value{}; bool active = false;
  Mapping(ID3D11DeviceContext *ctx, ID3D11Resource *res) : context(ctx), resource(res) {
    const auto begin = GetTickCount64();
    for (;;) {
      const auto hr = context->Map(resource, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &value);
      if (hr == S_OK) { active = true; break; }
      if (hr != DXGI_ERROR_WAS_STILL_DRAWING) step(hr, "readback_map");
      require(GetTickCount64() - begin < kWaitMs && GetTickCount64() - started < kBudgetMs, "readback_coherence_deadline");
      Sleep(1);
    }
  }
  ~Mapping() { if (active) context->Unmap(resource, 0); }
};
static void create_texture(ID3D11Device *device, DXGI_FORMAT format, UINT flags, UINT samples,
                           Ref<ID3D11Texture2D> &texture) {
  D3D11_TEXTURE2D_DESC d{};
  d.Width = kWidth; d.Height = kHeight; d.MipLevels = d.ArraySize = 1;
  d.Format = format; d.SampleDesc.Count = samples; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = flags;
  step(device->CreateTexture2D(&d, nullptr, texture.out()), "texture_create");
}
static void make_readback(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11Texture2D *source,
                          Ref<ID3D11Texture2D> &readback) {
  D3D11_TEXTURE2D_DESC d{}; source->GetDesc(&d);
  require(d.SampleDesc.Count == 1 && d.Width == kWidth && d.Height == kHeight, "readback_single_sample_geometry");
  d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
  step(device->CreateTexture2D(&d, nullptr, readback.out()), "staging_create");
  context->CopyResource(readback.p, source); complete(device, context);
}
static void rgba(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11Texture2D *source,
                 std::array<unsigned char, 4> expected, bool corrupt = false) {
  Ref<ID3D11Texture2D> readback; make_readback(device, context, source, readback);
  Mapping map(context, readback.p);
  require(map.value.pData && map.value.RowPitch >= kWidth * 4, "rgba_readback_layout");
  bool valid = true, corrupted_matches = true;
  for (UINT y = 0; y < kHeight; ++y) for (UINT x = 0; x < kWidth; ++x) for (UINT c = 0; c < 4; ++c) {
    const auto actual = static_cast<const unsigned char *>(map.value.pData)[y * map.value.RowPitch + x * 4 + c];
    valid &= actual == expected[c];
    const auto bad = static_cast<unsigned char>(expected[c] ^ ((x == 0 && y == 0 && c == 0) ? 1 : 0));
    corrupted_matches &= actual == bad;
  }
  require(valid, "rgba_all_pixels_exact");
  if (corrupt) require(corrupted_matches, "negative_control_pixel", true);
}
static const char shaders[] = R"(
cbuffer Params : register(b0) { float4 color; float z; float3 unused; };
float4 vs_main(uint id:SV_VertexID):SV_POSITION {
  return float4(id==2 ? 3.0 : -1.0, id==1 ? 3.0 : -1.0, z, 1.0);
}
float4 ps_one():SV_TARGET { return color; }
struct Pair { float4 first:SV_TARGET0; float4 second:SV_TARGET1; };
Pair ps_mrt() { Pair p; p.first=color; p.second=float4(1.0-color.rgb,1.0); return p; }
float4 ps_msaa(uint sample:SV_SampleIndex):SV_TARGET {
  return sample==0 ? float4(1,0,0,1) : float4(0,0,0,1);
}
)";
static const char compute_shader[] = R"(
cbuffer Values : register(b0) { uint seed; uint width; uint height; uint unused; };
RWTexture2D<uint> result:register(u0);
[numthreads(8,8,1)] void cs_main(uint3 id:SV_DispatchThreadID) {
  if(id.x<width && id.y<height) result[id.xy]=(id.x*17u+id.y*31u)^seed;
}
)";
static void compile(const char *text, size_t length, const char *entry, const char *target, Ref<ID3DBlob> &out) {
  Ref<ID3DBlob> errors;
  step(D3DCompile(text, length, "gpu-wave-procedural", nullptr, nullptr, entry, target,
                  D3DCOMPILE_ENABLE_STRICTNESS, 0, out.out(), errors.out()), "shader_compile");
  // Compiler diagnostics are intentionally not copied into the bounded JSON.
}
struct Pipeline {
  Ref<ID3D11VertexShader> vs; Ref<ID3D11PixelShader> ps, mrt, sample_colors;
  Ref<ID3D11Buffer> constants; Ref<ID3D11RasterizerState> raster;
  Ref<ID3D11DepthStencilState> depth_on, depth_off;
  explicit Pipeline(ID3D11Device *device) {
    Ref<ID3DBlob> code;
    compile(shaders, sizeof(shaders) - 1, "vs_main", "vs_5_0", code);
    step(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, vs.out()), "vertex_shader_create");
    compile(shaders, sizeof(shaders) - 1, "ps_one", "ps_5_0", code);
    step(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, ps.out()), "pixel_shader_create");
    compile(shaders, sizeof(shaders) - 1, "ps_mrt", "ps_5_0", code);
    step(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, mrt.out()), "mrt_shader_create");
    compile(shaders, sizeof(shaders) - 1, "ps_msaa", "ps_5_0", code);
    step(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, sample_colors.out()), "msaa_shader_create");
    D3D11_BUFFER_DESC bd{}; bd.ByteWidth = 32; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    step(device->CreateBuffer(&bd, nullptr, constants.out()), "render_constants_create");
    D3D11_RASTERIZER_DESC rd{}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE;
    step(device->CreateRasterizerState(&rd, raster.out()), "raster_state_create");
    D3D11_DEPTH_STENCIL_DESC dd{}; dd.DepthEnable = TRUE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dd.DepthFunc = D3D11_COMPARISON_LESS;
    dd.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK; dd.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
    dd.FrontFace.StencilFailOp = dd.FrontFace.StencilDepthFailOp = dd.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    dd.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS; dd.BackFace = dd.FrontFace;
    step(device->CreateDepthStencilState(&dd, depth_on.out()), "depth_state_create");
    dd.DepthEnable = FALSE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    step(device->CreateDepthStencilState(&dd, depth_off.out()), "no_depth_state_create");
  }
  void bind(ID3D11DeviceContext *context, UINT count, ID3D11RenderTargetView *const *targets,
             ID3D11DepthStencilView *depth = nullptr) {
    D3D11_VIEWPORT viewport{0, 0, float(kWidth), float(kHeight), 0, 1};
    context->RSSetViewports(1, &viewport); context->RSSetState(raster.p);
    context->IASetInputLayout(nullptr); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->OMSetRenderTargets(count, targets, depth); context->OMSetDepthStencilState(depth ? depth_on.p : depth_off.p, 0);
    context->VSSetShader(vs.p, nullptr, 0); context->PSSetShader(count == 2 ? mrt.p : ps.p, nullptr, 0);
    context->VSSetConstantBuffers(0, 1, &constants.p); context->PSSetConstantBuffers(0, 1, &constants.p);
  }
  void values(ID3D11DeviceContext *context, std::array<float, 4> color, float z) {
    const float data[8] = {color[0], color[1], color[2], color[3], z, 0, 0, 0};
    context->UpdateSubresource(constants.p, 0, nullptr, data, 0, 0);
  }
};
struct ColorTarget {
  Ref<ID3D11Texture2D> image; Ref<ID3D11RenderTargetView> view;
  explicit ColorTarget(ID3D11Device *device, UINT samples = 1) {
    create_texture(device, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET, samples, image);
    step(device->CreateRenderTargetView(image.p, nullptr, view.out()), "rtv_create");
  }
};
static void clear(ID3D11DeviceContext *context, ID3D11RenderTargetView *view) {
  const FLOAT poison[4] = {0, 0, 1, 0}; context->ClearRenderTargetView(view, poison);
}
static void basic(ID3D11Device *device, ID3D11DeviceContext *context, Pipeline &pipeline) {
  ColorTarget target(device); pipeline.bind(context, 1, &target.view.p);
  pipeline.values(context, {1, 0, 0, 1}, 0.5f); clear(context, target.view.p); context->Draw(3, 0);
  context->OMSetRenderTargets(0, nullptr, nullptr);
  rgba(device, context, target.image.p, {255, 0, 0, 255}, selected("pixel"));
}
static void mrt_depth(ID3D11Device *device, ID3D11DeviceContext *context, Pipeline &pipeline) {
  ColorTarget first(device), second(device); Ref<ID3D11Texture2D> depth; Ref<ID3D11DepthStencilView> view;
  create_texture(device, DXGI_FORMAT_D32_FLOAT, D3D11_BIND_DEPTH_STENCIL, 1, depth);
  step(device->CreateDepthStencilView(depth.p, nullptr, view.out()), "dsv_create");
  ID3D11RenderTargetView *targets[2] = {first.view.p, second.view.p}; pipeline.bind(context, 2, targets, view.p);
  clear(context, first.view.p); clear(context, second.view.p); context->ClearDepthStencilView(view.p, D3D11_CLEAR_DEPTH, 1, 0);
  pipeline.values(context, {0, 1, 0, 1}, 0.25f); context->Draw(3, 0); context->Flush();
  // A later pass must load the written depth and reject this farther red draw.
  pipeline.values(context, {1, 0, 0, 1}, 0.75f); context->Draw(3, 0);
  context->OMSetRenderTargets(0, nullptr, nullptr);
  rgba(device, context, first.image.p, {0, 255, 0, 255});
  rgba(device, context, second.image.p, {255, 0, 255, 255});
}
static void compute(ID3D11Device *device, ID3D11DeviceContext *context) {
  Ref<ID3DBlob> code; compile(compute_shader, sizeof(compute_shader) - 1, "cs_main", "cs_5_0", code);
  Ref<ID3D11ComputeShader> shader;
  step(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.out()), "compute_shader_create");
  Ref<ID3D11Texture2D> output; Ref<ID3D11UnorderedAccessView> uav;
  create_texture(device, DXGI_FORMAT_R32_UINT, D3D11_BIND_UNORDERED_ACCESS, 1, output);
  step(device->CreateUnorderedAccessView(output.p, nullptr, uav.out()), "uav_create");
  constexpr uint32_t seed = 0x13579bdf;
  const uint32_t data[4] = {seed, kWidth, kHeight, 0};
  D3D11_BUFFER_DESC bd{}; bd.ByteWidth = sizeof(data); bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  D3D11_SUBRESOURCE_DATA initial{data, 0, 0}; Ref<ID3D11Buffer> constants;
  step(device->CreateBuffer(&bd, &initial, constants.out()), "compute_constants_create");
  context->CSSetShader(shader.p, nullptr, 0); context->CSSetConstantBuffers(0, 1, &constants.p);
  context->CSSetUnorderedAccessViews(0, 1, &uav.p, nullptr); context->Dispatch(kWidth / 8, kHeight / 8, 1);
  ID3D11UnorderedAccessView *none = nullptr; context->CSSetUnorderedAccessViews(0, 1, &none, nullptr);
  Ref<ID3D11Texture2D> readback; make_readback(device, context, output.p, readback); Mapping map(context, readback.p);
  require(map.value.pData && map.value.RowPitch >= kWidth * sizeof(uint32_t), "compute_readback_layout");
  bool valid = true, corrupted_matches = true;
  for (UINT y = 0; y < kHeight; ++y) for (UINT x = 0; x < kWidth; ++x) {
    uint32_t actual; std::memcpy(&actual, static_cast<const unsigned char *>(map.value.pData) + y * map.value.RowPitch + x * 4, 4);
    const uint32_t expected = (x * 17u + y * 31u) ^ seed;
    valid &= actual == expected; corrupted_matches &= actual == (expected ^ ((x == 0 && y == 0) ? 1u : 0u));
  }
  require(valid, "compute_all_pixels_exact");
  if (selected("compute")) require(corrupted_matches, "negative_control_compute", true);
}
static bool msaa(ID3D11Device *device, ID3D11DeviceContext *context, Pipeline &pipeline) {
  UINT quality = 0; step(device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, 4, &quality), "msaa_quality_query");
  if (!quality) return false;
  ColorTarget target(device, 4), resolved(device);
  pipeline.bind(context, 1, &target.view.p); pipeline.values(context, {1, 0, 0, 1}, 0.5f);
  context->PSSetShader(pipeline.sample_colors.p, nullptr, 0);
  clear(context, target.view.p); context->Draw(3, 0); context->OMSetRenderTargets(0, nullptr, nullptr);
  context->ResolveSubresource(resolved.image.p, 0, target.image.p, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
  // Sample zero is red and three samples are black: copying any one sample
  // cannot satisfy the quarter-red resolve golden (0.25 * 255 rounds to 64).
  rgba(device, context, resolved.image.p, {64, 0, 0, 255});
  return true;
}
static UINT64 occlusion(ID3D11Device *device, ID3D11DeviceContext *context, bool later_chunk) {
  D3D11_QUERY_DESC desc{D3D11_QUERY_OCCLUSION, 0}; Ref<ID3D11Query> query;
  step(device->CreateQuery(&desc, query.out()), "occlusion_query_create");
  context->Begin(query.p); context->Draw(3, 0);
  if (later_chunk) context->Flush();
  // No API operation creates another visibility offset before this End/Flush.
  // Begin+Draw+Flush writes offset(s) in chunk A. End+Flush commits chunk B
  // with a zero visibility count and must still publish the retained query.
  context->End(query.p); context->Flush();
  const auto begin = GetTickCount64();
  for (;;) {
    UINT64 value = 0;
    const auto hr = context->GetData(query.p, &value, sizeof(value), D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if (FAILED(hr)) step(hr, "occlusion_query_getdata");
    if (hr == S_OK) { ++checks; return value; }
    require(GetTickCount64() - begin < kWaitMs && GetTickCount64() - started < kBudgetMs, "occlusion_query_deadline");
    Sleep(1);
  }
}
static void query_later_chunk(ID3D11Device *device, ID3D11DeviceContext *context, Pipeline &pipeline) {
  ColorTarget target(device); pipeline.bind(context, 1, &target.view.p);
  pipeline.values(context, {1, 0, 0, 1}, 0.5f); clear(context, target.view.p);
  const auto baseline = occlusion(device, context, false);
  const auto later = occlusion(device, context, true);
  require(baseline == UINT64(kWidth) * kHeight && later == baseline, "occlusion_later_chunk_exact_samples");
  if (selected("query")) require(later == baseline + 1, "negative_control_query", true);
  context->OMSetRenderTargets(0, nullptr, nullptr);
  rgba(device, context, target.image.p, {255, 0, 0, 255});
}
template<class Fn> static void stage(const char *name, ID3D11DeviceContext *context, Fn &&fn) {
  budget(); stage_name = name; stage_start_checks = checks;
  fn(); ++stages_passed; report_stage("pass", "verified");
  stage_name = nullptr; context->ClearState(); context->Flush();
}
static void workflow() {
  Ref<IDXGIFactory1> factory;
  step(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(factory.out())), "dxgi_factory_create");
  Ref<ID3D11Device> device; Ref<ID3D11DeviceContext> context;
  const D3D_FEATURE_LEVEL requested[] = {D3D_FEATURE_LEVEL_11_0}; D3D_FEATURE_LEVEL level{};
  const auto hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, requested, 1,
                                  D3D11_SDK_VERSION, device.out(), &level, context.out());
  if (hr == DXGI_ERROR_UNSUPPORTED) throw Failure{"feature_level_11_hardware_unsupported", hr, true};
  step(hr, "hardware_device_create"); require(level == D3D_FEATURE_LEVEL_11_0, "feature_level_11");
  module(L"d3d11.dll", "d3d11.dll"); module(L"dxgi.dll", "dxgi.dll");
  module(L"winemetal.dll", "winemetal.dll"); module(L"d3dcompiler_47.dll", "d3dcompiler_47.dll");
  Sleep(1000); // Bounded external module-mapping observation window; no input/UI.
  Pipeline pipeline(device.p);
  stage("draw_basic", context.p, [&] { basic(device.p, context.p, pipeline); });
  stage("mrt_depth", context.p, [&] { mrt_depth(device.p, context.p, pipeline); });
  stage("compute", context.p, [&] { compute(device.p, context.p); });
  budget(); stage_name = "msaa"; stage_start_checks = checks;
  if (msaa(device.p, context.p, pipeline)) { ++stages_passed; report_stage("pass", "verified_four_samples"); }
  else { ++stages_unsupported; report_stage("unsupported", "rgba8_four_samples_unavailable"); }
  stage_name = nullptr; context->ClearState(); context->Flush();
  stage("query_later_chunk", context.p, [&] { query_later_chunk(device.p, context.p, pipeline); });
  step(device->GetDeviceRemovedReason(), "device_not_removed");
}
int main(int argc, char **argv) {
  setvbuf(stdout, nullptr, _IONBF, 0); started = GetTickCount64();
  if (argc == 3 && std::strcmp(argv[1], "--negative-control") == 0 &&
      (!std::strcmp(argv[2], "pixel") || !std::strcmp(argv[2], "compute") || !std::strcmp(argv[2], "query")))
    negative_control = argv[2];
  else if (argc != 1) return 64;
  std::printf("{\"schema\":1,\"type\":\"start\",\"probe\":\"gpu-wave\",\"negative_control\":");
  json_string(negative_control); std::printf("}\n");
  finished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!finished) { summary("fail", 0, "watchdog_event_create"); return 10; }
  HANDLE guard = CreateThread(nullptr, 0, watchdog, nullptr, 0, nullptr);
  if (!guard) { CloseHandle(finished); summary("fail", 0, "watchdog_thread_create"); return 10; }
  int result = 0;
  try { workflow(); summary("pass"); }
  catch (const Failure &failure) {
    if (stage_name) report_stage(failure.unsupported ? "unsupported" : "fail", failure.detail, failure.hr);
    const bool intended_negative = failure.negative && !selected("none");
    summary(intended_negative ? "negative_control_rejected" : failure.unsupported ? "unsupported" : "fail",
            intended_negative ? 1 : 0, failure.detail, failure.hr);
    result = intended_negative ? 20 : failure.unsupported ? 2 : 10;
  }
  catch (...) { if (stage_name) report_stage("fail", "unexpected_exception"); summary("fail"); result = 10; }
  SetEvent(finished); WaitForSingleObject(guard, 1000); CloseHandle(guard); CloseHandle(finished);
  return result;
}
