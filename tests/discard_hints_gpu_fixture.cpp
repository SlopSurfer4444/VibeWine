// Copyright 2026 VibeWine contributors.
// SPDX-License-Identifier: LGPL-2.1-or-later
// Offscreen D3D11.1 acceptance fixture. Reuses tests/vibewine/gpu-wave/guest.cpp
// procedural shaders, event-query completion, and exact bounded readback patterns.
// Discarded bytes are NEVER an oracle: each inspected discarded region has a
// subsequent defining clear/draw; only explicitly unaffected regions retain data.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <array>
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
struct Failure { int code; };
using Pixel = std::array<unsigned char, 4>;
constexpr UINT kSize = 32;
constexpr Pixel red{255,0,0,255}, green{0,255,0,255}, blue{0,0,255,255};
static const FLOAT rf[]{1,0,0,1}, gf[]{0,1,0,1}, bf[]{0,0,1,1}, kf[]{0,0,0,1};
static ULONGLONG started;
static HANDLE finished;
static unsigned cases, images;
static const char *control = "none";
static bool selected(const char *name) { return std::strcmp(control, name) == 0; }
static void require(bool ok, const char *detail) {
  if (!ok) { std::printf("FAIL detail=%s\n", detail); throw Failure{1}; }
}
static void step(HRESULT hr, const char *detail) {
  if (FAILED(hr)) {
    std::printf("FAIL detail=%s hr=%08lx\n", detail, static_cast<unsigned long>(hr)); throw Failure{1};
  }
}
static void deadline(ULONGLONG begin) {
  require(GetTickCount64() - begin < 5000 && GetTickCount64() - started < 90000, "deadline");
}
static DWORD WINAPI watchdog(void *) {
  if (WaitForSingleObject(finished, 100000) != WAIT_OBJECT_0) TerminateProcess(GetCurrentProcess(), 124);
  return 0;
}
static void module(const wchar_t *name) {
  wchar_t path[2048]{};
  auto handle = GetModuleHandleW(name);
  const DWORD length = handle ? GetModuleFileNameW(handle, path, 2048) : 0;
  require(length > 0 && length < 2048, "module_path");
  std::printf("MODULE %ls %ls\n", name, path);
}
static void complete(ID3D11Device *device, ID3D11DeviceContext *ctx) {
  Ref<ID3D11Query> query; D3D11_QUERY_DESC d{D3D11_QUERY_EVENT,0};
  step(device->CreateQuery(&d, query.out()), "query_create");
  ctx->End(query.p); ctx->Flush();
  const auto begin = GetTickCount64();
  for (;;) {
    BOOL done = FALSE;
    const auto hr = ctx->GetData(query.p, &done, sizeof(done), D3D11_ASYNC_GETDATA_DONOTFLUSH);
    step(hr, "query_getdata");
    if (hr == S_OK && done) return;
    deadline(begin); Sleep(1);
  }
}
static void unbind(ID3D11DeviceContext *ctx) { ctx->OMSetRenderTargets(0, nullptr, nullptr); }
static void passed(const char *name) { ++cases; std::printf("CASE_PASS name=%s\n", name); }

struct Surface {
  Ref<ID3D11Texture2D> texture;
  Ref<ID3D11RenderTargetView> views[4];
  UINT mips, slices;
  Surface(ID3D11Device *device, UINT levels = 1, UINT arrays = 1, bool readable = false)
      : mips(levels), slices(arrays) {
    require(levels <= 2 && arrays <= 2, "bounded_surface_geometry");
    D3D11_TEXTURE2D_DESC d{};
    d.Width = d.Height = kSize; d.MipLevels = levels; d.ArraySize = arrays;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_RENDER_TARGET | (readable ? D3D11_BIND_SHADER_RESOURCE : 0);
    step(device->CreateTexture2D(&d, nullptr, texture.out()), "texture_create");
    for (UINT a = 0; a < slices; ++a) for (UINT m = 0; m < mips; ++m) {
      D3D11_RENDER_TARGET_VIEW_DESC vd{}; vd.Format = d.Format;
      if (slices==1) {
        // Positive cases must retain the base 2D native view, rather than an
        // array-view alias that is deliberately ineligible for conservative S2.
        vd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D; vd.Texture2D.MipSlice = m;
      } else {
        vd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
        vd.Texture2DArray.MipSlice = m; vd.Texture2DArray.FirstArraySlice = a; vd.Texture2DArray.ArraySize = 1;
      }
      step(device->CreateRenderTargetView(texture.p, &vd, views[index(m,a)].out()), "rtv_create");
    }
  }
  UINT index(UINT mip, UINT slice) const { return D3D11CalcSubresource(mip, slice, mips); }
  ID3D11RenderTargetView *view(UINT mip = 0, UINT slice = 0) const { return views[index(mip,slice)].p; }
};

// Read the selected subresource's entire defined image, respecting RowPitch.
static void image(ID3D11Device *device, ID3D11DeviceContext *ctx, Surface &surface,
                  const char *name, Pixel left, Pixel right, UINT mip = 0, UINT slice = 0,
                  bool injected = false) {
  Ref<ID3D11Texture2D> staging; D3D11_TEXTURE2D_DESC d{}; surface.texture->GetDesc(&d);
  d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
  step(device->CreateTexture2D(&d, nullptr, staging.out()), "staging_create");
  unbind(ctx); ctx->CopyResource(staging.p, surface.texture.p); complete(device, ctx);
  const UINT sub = surface.index(mip,slice), size = kSize >> mip;
  D3D11_MAPPED_SUBRESOURCE map{}; const auto begin = GetTickCount64();
  for (;;) {
    const auto hr = ctx->Map(staging.p, sub, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &map);
    if (hr == S_OK) break;
    if (hr != DXGI_ERROR_WAS_STILL_DRAWING) step(hr, "readback_map");
    deadline(begin); Sleep(1);
  }
  if (!map.pData || map.RowPitch < size * 4) {
    ctx->Unmap(staging.p,sub); require(false,"readback_layout");
  }
  bool valid = true;
  UINT mismatch_x = 0, mismatch_y = 0; Pixel actual{};
  if (valid) for (UINT y = 0; y < size; ++y) for (UINT x = 0; x < size; ++x) {
    const auto *pixel = static_cast<const unsigned char *>(map.pData) + y * map.RowPitch + x * 4;
    const auto &expected = x < size / 2 ? left : right;
    if (std::memcmp(pixel, expected.data(), 4)) {
      if (valid) { mismatch_x = x; mismatch_y = y; std::memcpy(actual.data(), pixel, 4); }
      valid = false;
    }
  }
  ctx->Unmap(staging.p, sub);
  if (!valid) {
    std::printf("%s name=%s mip=%u slice=%u x=%u y=%u actual=%u,%u,%u,%u\n",
                injected ? "CONTROL_DETECTED" : "IMAGE_FAIL", name, mip, slice, mismatch_x, mismatch_y,
                unsigned(actual[0]), unsigned(actual[1]), unsigned(actual[2]), unsigned(actual[3]));
    throw Failure{injected ? 20 : 1};
  }
  ++images; std::printf("IMAGE_PASS name=%s mip=%u slice=%u pixels=%u\n", name, mip, slice, size * size);
}

static const char shader[] = R"(
cbuffer Params : register(b0) { float4 color; float z; float3 padding; };
Texture2D<float4> source : register(t0);
float4 vs_main(uint id : SV_VertexID) : SV_POSITION {
  return float4(id==2 ? 3.0 : -1.0, id==1 ? 3.0 : -1.0, z, 1.0);
}
float4 ps_color() : SV_TARGET { return color; }
float4 ps_read(float4 position : SV_POSITION) : SV_TARGET { return source.Load(int3(uint2(position.xy),0)); }
)";
static void compile(const char *entry, const char *target, Ref<ID3DBlob> &code) {
  Ref<ID3DBlob> errors;
  step(D3DCompile(shader, sizeof(shader)-1, "discard-hints", nullptr, nullptr, entry, target,
                  D3DCOMPILE_ENABLE_STRICTNESS, 0, code.out(), errors.out()), "shader_compile");
}
struct Pipeline {
  Ref<ID3D11VertexShader> vs;
  Ref<ID3D11PixelShader> color, read;
  Ref<ID3D11Buffer> constants;
  Ref<ID3D11RasterizerState> raster;
  Ref<ID3D11DepthStencilState> depth_stencil;
  explicit Pipeline(ID3D11Device *device) {
    Ref<ID3DBlob> code;
    compile("vs_main", "vs_5_0", code);
    step(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, vs.out()), "vs_create");
    compile("ps_color", "ps_5_0", code);
    step(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, color.out()), "ps_create");
    compile("ps_read", "ps_5_0", code);
    step(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, read.out()), "read_ps_create");
    D3D11_BUFFER_DESC bd{}; bd.ByteWidth = 32; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    step(device->CreateBuffer(&bd, nullptr, constants.out()), "cb_create");
    D3D11_RASTERIZER_DESC rd{}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE; rd.ScissorEnable = TRUE;
    step(device->CreateRasterizerState(&rd, raster.out()), "raster_create");
    D3D11_DEPTH_STENCIL_DESC dd{}; dd.DepthEnable = TRUE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dd.DepthFunc = D3D11_COMPARISON_LESS; dd.StencilEnable = TRUE; dd.StencilReadMask = 255; dd.StencilWriteMask = 255;
    dd.FrontFace.StencilFunc = D3D11_COMPARISON_EQUAL;
    dd.FrontFace.StencilFailOp = dd.FrontFace.StencilDepthFailOp = dd.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    dd.BackFace = dd.FrontFace;
    step(device->CreateDepthStencilState(&dd, depth_stencil.out()), "depth_state_create");
  }
  void draw(ID3D11DeviceContext *ctx, ID3D11RenderTargetView *rtv, const FLOAT *value,
            UINT size = kSize, int half = 0, ID3D11DepthStencilView *dsv = nullptr,
            FLOAT z = 0.1f, UINT stencil = 7, ID3D11ShaderResourceView *srv = nullptr) {
    struct Constants { FLOAT color[4], z, padding[3]; } data{};
    std::memcpy(data.color, value, sizeof(data.color)); data.z = z;
    ctx->UpdateSubresource(constants.p, 0, nullptr, &data, 0, 0);
    ctx->OMSetRenderTargets(1, &rtv, dsv);
    ctx->OMSetDepthStencilState(dsv ? depth_stencil.p : nullptr, stencil);
    ctx->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
    ctx->IASetInputLayout(nullptr); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs.p, nullptr, 0); ctx->PSSetShader(srv ? read.p : color.p, nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, &constants.p); ctx->PSSetConstantBuffers(0, 1, &constants.p);
    ctx->PSSetShaderResources(0, 1, &srv);
    ctx->RSSetState(raster.p);
    const D3D11_VIEWPORT vp{0,0,FLOAT(size),FLOAT(size),0,1}; ctx->RSSetViewports(1, &vp);
    const D3D11_RECT rect{half == 2 ? LONG(size/2) : 0, 0, half == 1 ? LONG(size/2) : LONG(size), LONG(size)};
    ctx->RSSetScissorRects(1, &rect); ctx->Draw(3, 0);
    ID3D11ShaderResourceView *none = nullptr; ctx->PSSetShaderResources(0, 1, &none);
  }
};

static void basic(ID3D11Device *device, ID3D11DeviceContext1 *ctx, Pipeline &p, bool resource) {
  Surface target(device);
  ctx->ClearRenderTargetView(target.view(), rf); p.draw(ctx, target.view(), rf);
  if (resource) ctx->DiscardResource(target.texture.p); else ctx->DiscardView(target.view());
  // Even the omission control first defines every discarded byte with green.
  ctx->ClearRenderTargetView(target.view(), gf);
  const bool omit = resource && selected("omit-overwrite");
  if (omit) std::printf("CONTROL_INJECTED kind=omit-overwrite command=final_blue_draw\n");
  else p.draw(ctx, target.view(), bf);
  image(device, ctx, target, resource ? "discard_resource" : "discard_view", blue, blue, 0, 0, omit);
  passed(resource ? "discard_resource" : "discard_view");
}
static void rectangles(ID3D11Device *device, ID3D11DeviceContext1 *ctx, Pipeline &p) {
  Surface target(device);
  ctx->ClearRenderTargetView(target.view(), rf); p.draw(ctx, target.view(), rf);
  const D3D11_RECT left{0,0,LONG(kSize/2),LONG(kSize)};
  ctx->DiscardView1(target.view(), &left, 1);
  // Only left was discarded. The right half remains a defined preservation oracle.
  p.draw(ctx, target.view(), bf, kSize, 1);
  image(device, ctx, target, "partial_rect_untouched_complement", blue, red);
  const D3D11_RECT full{0,0,LONG(kSize),LONG(kSize)};
  ctx->DiscardView1(target.view(), &full, 1);
  ctx->ClearRenderTargetView(target.view(), gf); p.draw(ctx, target.view(), bf);
  image(device, ctx, target, "explicit_full_rect", blue, blue);
  passed("rectangles");
}
static void subresources(ID3D11Device *device, ID3D11DeviceContext1 *ctx, Pipeline &p) {
  Surface target(device, 2, 2);
  for (UINT a=0; a<2; ++a) for (UINT m=0; m<2; ++m) {
    ctx->ClearRenderTargetView(target.view(m,a), rf); p.draw(ctx, target.view(m,a), rf, kSize >> m);
  }
  ctx->DiscardView(target.view(1,1));
  ctx->ClearRenderTargetView(target.view(1,1), gf); p.draw(ctx, target.view(1,1), bf, kSize/2);
  const bool wrong = selected("wrong-action");
  if (wrong) {
    // A real wrong-resource GPU action, not a changed expected value or a read of
    // discarded data. (0,0) was never discarded and is explicitly blackened.
    std::printf("CONTROL_INJECTED kind=wrong-action command=clear_untouched_subresource\n");
    ctx->ClearRenderTargetView(target.view(0,0), kf);
  }
  image(device, ctx, target, "untouched_mip0_slice0", red, red, 0, 0, wrong);
  image(device, ctx, target, "untouched_mip1_slice0", red, red, 1, 0);
  image(device, ctx, target, "untouched_mip0_slice1", red, red, 0, 1);
  image(device, ctx, target, "overwritten_mip1_slice1", blue, blue, 1, 1);
  passed("mip_slice_isolation");
}
static void depth_stencil(ID3D11Device *device, ID3D11DeviceContext1 *ctx, Pipeline &p) {
  Surface color(device); Ref<ID3D11Texture2D> depth; Ref<ID3D11DepthStencilView> dsv;
  D3D11_TEXTURE2D_DESC d{}; d.Width=d.Height=kSize; d.MipLevels=d.ArraySize=1;
  d.Format=DXGI_FORMAT_D24_UNORM_S8_UINT; d.SampleDesc.Count=1; d.Usage=D3D11_USAGE_DEFAULT;
  d.BindFlags=D3D11_BIND_DEPTH_STENCIL;
  step(device->CreateTexture2D(&d,nullptr,depth.out()), "depth_create");
  step(device->CreateDepthStencilView(depth.p,nullptr,dsv.out()), "dsv_create");
  ctx->ClearRenderTargetView(color.view(), rf);
  ctx->ClearDepthStencilView(dsv.p, D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL, 0.9f, 1);
  p.draw(ctx,color.view(),rf,kSize,0,dsv.p,0.1f,1);
  ctx->DiscardView(dsv.p);
  // Define BOTH discarded aspects before comparing depth or stencil.
  ctx->ClearDepthStencilView(dsv.p, D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL, 0.25f, 7);
  p.draw(ctx,color.view(),bf,kSize,1,dsv.p,0.1f,7); // passes both
  p.draw(ctx,color.view(),gf,kSize,2,dsv.p,0.5f,7); // fails depth
  p.draw(ctx,color.view(),gf,kSize,2,dsv.p,0.1f,6); // fails stencil
  image(device,ctx,color,"dsv_discard_defined_aspects_color_preserved",blue,red);
  ctx->ClearDepthStencilView(dsv.p,D3D11_CLEAR_DEPTH,0.75f,0);
  p.draw(ctx,color.view(),bf,kSize,2,dsv.p,0.5f,7); // defined stencil preserved by depth-only clear
  image(device,ctx,color,"depth_only_clear_keeps_stencil",blue,blue);
  ctx->ClearDepthStencilView(dsv.p,D3D11_CLEAR_STENCIL,0.0f,6);
  p.draw(ctx,color.view(),gf,kSize,0,dsv.p,0.9f,6); // defined depth preserved by stencil-only clear
  image(device,ctx,color,"stencil_only_clear_keeps_depth",blue,blue);
  p.draw(ctx,color.view(),gf,kSize,0,dsv.p,0.1f,6);
  image(device,ctx,color,"dsv_final_comparisons_pass",green,green);
  passed("depth_stencil");
}
static void readers(ID3D11Device *device, ID3D11DeviceContext1 *ctx, Pipeline &p) {
  Surface source(device,1,1,true), copied(device), sampled(device);
  ctx->ClearRenderTargetView(source.view(),rf); p.draw(ctx,source.view(),rf); unbind(ctx);
  // These readers occur before discard. Their outputs must survive any attempt
  // to move DontCare backward through a copy or shader read.
  ctx->CopyResource(copied.texture.p,source.texture.p);
  Ref<ID3D11ShaderResourceView> srv;
  step(device->CreateShaderResourceView(source.texture.p,nullptr,srv.out()),"reader_srv_create");
  p.draw(ctx,sampled.view(),rf,kSize,0,nullptr,0.1f,0,srv.p);
  ctx->DiscardResource(source.texture.p);
  ctx->ClearRenderTargetView(source.view(),gf); p.draw(ctx,source.view(),bf);
  image(device,ctx,copied,"copy_before_discard",red,red);
  image(device,ctx,sampled,"shader_read_before_discard",red,red);
  image(device,ctx,source,"reader_source_defined_overwrite",blue,blue);
  passed("intervening_read_copy");
}
static void repetition_and_boundaries(ID3D11Device *device, ID3D11DeviceContext1 *ctx, Pipeline &p) {
  Surface target(device);
  ctx->ClearRenderTargetView(target.view(),rf); p.draw(ctx,target.view(),rf);
  ctx->DiscardView(target.view()); ctx->DiscardView(target.view()); ctx->DiscardResource(target.texture.p);
  ctx->ClearRenderTargetView(target.view(),gf); p.draw(ctx,target.view(),bf);
  image(device,ctx,target,"repeated_discard_defined_overwrite",blue,blue);
  ctx->ClearRenderTargetView(target.view(),rf); p.draw(ctx,target.view(),rf);
  unbind(ctx); complete(device,ctx); // Force submission/completion before discard.
  ctx->DiscardView(target.view()); ctx->ClearRenderTargetView(target.view(),gf);
  p.draw(ctx,target.view(),bf); unbind(ctx); complete(device,ctx);
  image(device,ctx,target,"discard_across_submission_boundary",blue,blue);
  // An untouched target verifies that a pending discard cannot leak to the next pass.
  Surface untouched(device); ctx->ClearRenderTargetView(untouched.view(),rf); p.draw(ctx,untouched.view(),rf);
  ctx->DiscardView(target.view());
  p.draw(ctx,untouched.view(),bf,kSize,1);
  ctx->ClearRenderTargetView(target.view(),gf); p.draw(ctx,target.view(),bf);
  image(device,ctx,untouched,"unrelated_pass_after_discard",blue,red);
  image(device,ctx,target,"later_target_pass_defined",blue,blue);
  passed("repetition_pass_boundaries");
}
static void deferred(ID3D11Device *device, ID3D11DeviceContext1 *ctx, Pipeline &p) {
  Surface target(device), untouched(device);
  Ref<ID3D11DeviceContext> recording; Ref<ID3D11DeviceContext1> record;
  step(device->CreateDeferredContext(0,recording.out()),"deferred_create");
  step(recording->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void **>(record.out())),"deferred_context1");
  ctx->ClearRenderTargetView(target.view(),rf); p.draw(ctx,target.view(),rf);
  ctx->ClearRenderTargetView(untouched.view(),rf); p.draw(ctx,untouched.view(),rf);
  record->ClearRenderTargetView(target.view(),rf); p.draw(record.p,target.view(),rf);
  record->DiscardView(target.view()); record->ClearRenderTargetView(target.view(),gf);
  p.draw(record.p,target.view(),bf); unbind(record.p);
  Ref<ID3D11CommandList> list;
  step(record->FinishCommandList(FALSE,list.out()),"finish_command_list");
  // Record-only work must not mutate the immediate target or other resources.
  image(device,ctx,target,"deferred_record_target_not_executed",red,red);
  image(device,ctx,untouched,"deferred_record_untouched_immediate",red,red);
  for (unsigned replay=0;replay<2;++replay) {
    ctx->ClearRenderTargetView(target.view(),gf);
    ctx->ExecuteCommandList(list.p,FALSE);
    image(device,ctx,target,replay ? "deferred_second_replay" : "deferred_first_replay",blue,blue);
  }
  // Split a discard and its mandatory defining overwrite across command lists.
  record->DiscardResource(target.texture.p);
  Ref<ID3D11CommandList> discard_list, overwrite_list;
  step(record->FinishCommandList(FALSE,discard_list.out()),"finish_discard_list");
  record->ClearRenderTargetView(target.view(),gf); p.draw(record.p,target.view(),bf); unbind(record.p);
  step(record->FinishCommandList(FALSE,overwrite_list.out()),"finish_overwrite_list");
  ctx->ExecuteCommandList(discard_list.p,FALSE);
  ctx->ExecuteCommandList(overwrite_list.p,FALSE);
  image(device,ctx,target,"deferred_separate_list_overwrite",blue,blue);
  image(device,ctx,untouched,"deferred_unrelated_resource_preserved",red,red);
  passed("deferred_record_replay");
}
static void workflow() {
  Ref<IDXGIFactory1> factory;
  step(CreateDXGIFactory1(__uuidof(IDXGIFactory1),reinterpret_cast<void **>(factory.out())),"dxgi_factory");
  Ref<ID3D11Device> device; Ref<ID3D11DeviceContext> immediate; Ref<ID3D11DeviceContext1> ctx;
  D3D_FEATURE_LEVEL got{}, wanted=D3D_FEATURE_LEVEL_11_0;
  step(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,&wanted,1,D3D11_SDK_VERSION,
                         device.out(),&got,immediate.out()),"device_create");
  require(got==wanted,"feature_level");
  step(immediate->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void **>(ctx.out())),"immediate_context1");
  module(L"d3d11.dll"); module(L"dxgi.dll"); module(L"d3dcompiler_47.dll");
  // Optional DXMT identity; native Windows validation has no winemetal module.
  if (GetModuleHandleW(L"winemetal.dll")) module(L"winemetal.dll");
  // Reuse the existing paired-runner mapping acknowledgment: module-path text
  // alone does not prove the host mapped the intended native bridge generation.
  wchar_t mapping_ack[32768]{};
  const DWORD ack_length=GetEnvironmentVariableW(L"VIBEWINE_GPU_MAP_ACK",mapping_ack,32768);
  require(ack_length<32768,"module_mapping_ack_path");
  if (ack_length) {
    const auto begin=GetTickCount64();
    while (GetFileAttributesW(mapping_ack)==INVALID_FILE_ATTRIBUTES) {
      require(GetTickCount64()-begin<15000,"module_mapping_ack_deadline"); Sleep(10);
    }
  }
  Pipeline pipeline(device.p);
  basic(device.p,ctx.p,pipeline,true); basic(device.p,ctx.p,pipeline,false);
  rectangles(device.p,ctx.p,pipeline); subresources(device.p,ctx.p,pipeline);
  depth_stencil(device.p,ctx.p,pipeline); readers(device.p,ctx.p,pipeline);
  repetition_and_boundaries(device.p,ctx.p,pipeline); deferred(device.p,ctx.p,pipeline);
  require(cases==8 && images==25,"coverage_count");
  require(selected("none"),"negative_control_not_detected");
  std::printf("PASS cases=%u images=%u control=none\n",cases,images);
}
int main(int argc, char **argv) {
  std::setvbuf(stdout,nullptr,_IONBF,0);
  if (argc==2 && !std::strcmp(argv[1],"--wrong-action")) control="wrong-action";
  else if (argc==2 && !std::strcmp(argv[1],"--omit-overwrite")) control="omit-overwrite";
  else if (argc!=1) { std::printf("usage: discard_hints_gpu_fixture.exe [--wrong-action|--omit-overwrite]\n"); return 2; }
  started=GetTickCount64(); finished=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  if (!finished) return 3;
  HANDLE thread=CreateThread(nullptr,0,watchdog,nullptr,0,nullptr);
  if (!thread) { CloseHandle(finished); return 3; }
  int result=0;
  try { workflow(); } catch (const Failure &failure) { result=failure.code; }
  SetEvent(finished); WaitForSingleObject(thread,INFINITE); CloseHandle(thread); CloseHandle(finished);
  return result;
}
