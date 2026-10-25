// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright 2026 VibeWine contributors.
// Source-only adjunct: reuse the existing bounded guest support, without
// running its main/workflow. Keep the include in build/source identity receipts.
#define main gpu_wave_reference_main
#include "../../gpu-wave/guest.cpp"
#undef main

static const char *adjacency_path = "both";
static const char green_compute[] = R"(
RWTexture2D<float4> destination : register(u0);
[numthreads(8,8,1)] void cs_green(uint3 id : SV_DispatchThreadID) {
  destination[id.xy] = float4(0,1,0,1);
}
)";

struct AdjacencyResources {
  ColorTarget source;
  Ref<ID3D11Texture2D> destination, green, readback;
  Ref<ID3D11ComputeShader> compute;
  Ref<ID3D11UnorderedAccessView> uav;
  Ref<ID3D11RasterizerState> raster;
  Ref<ID3D11Query> done;
  explicit AdjacencyResources(ID3D11Device *device, bool use_compute) : source(device, 4) {
    create_texture(device, DXGI_FORMAT_R8G8B8A8_UNORM,
                   use_compute ? D3D11_BIND_UNORDERED_ACCESS : 0, 1, destination);
    D3D11_TEXTURE2D_DESC desc{}; destination->GetDesc(&desc);
    desc.BindFlags = 0;
    std::array<unsigned char, kWidth * kHeight * 4> pixels{};
    for (UINT i = 0; i < kWidth * kHeight; ++i) {
      pixels[i * 4 + 1] = 255; pixels[i * 4 + 3] = 255;
    }
    D3D11_SUBRESOURCE_DATA initial{pixels.data(), kWidth * 4, 0};
    step(device->CreateTexture2D(&desc, &initial, green.out()), "green_texture_create");
    desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    step(device->CreateTexture2D(&desc, nullptr, readback.out()), "adjacency_staging_create");
    if (use_compute) {
      Ref<ID3DBlob> code;
      compile(green_compute, sizeof(green_compute) - 1, "cs_green", "cs_5_0", code);
      step(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                      compute.out()), "green_compute_shader_create");
      step(device->CreateUnorderedAccessView(destination.p, nullptr, uav.out()), "rgba8_uav_create");
    }
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE; rd.MultisampleEnable = TRUE;
    step(device->CreateRasterizerState(&rd, raster.out()), "multisample_raster_create");
    D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT, 0};
    step(device->CreateQuery(&qd, done.out()), "adjacency_event_create");
  }
};

static void adjacency_complete(ID3D11DeviceContext *context, ID3D11Query *done) {
  // First explicit submission/query boundary is AFTER the entire hazard and
  // its staging copy; no event is inserted between Rdraw, Xwrite and Resolve.
  context->End(done); context->Flush();
  const auto begin = GetTickCount64();
  for (;;) {
    BOOL ready = FALSE;
    const HRESULT hr = context->GetData(done, &ready, sizeof(ready), D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if (FAILED(hr)) step(hr, "adjacency_event_getdata");
    if (hr == S_OK && ready) { ++checks; return; }
    require(GetTickCount64() - begin < kWaitMs && GetTickCount64() - started < kBudgetMs,
            "adjacency_event_deadline");
    Sleep(1);
  }
}

static void adjacency_pixels(ID3D11DeviceContext *context, ID3D11Texture2D *readback,
                             std::array<unsigned char, 4> expected,
                             const char *detail, bool negative = false) {
  Mapping map(context, readback);
  require(map.value.pData && map.value.RowPitch >= kWidth * 4, "adjacency_readback_layout");
  bool exact = true;
  for (UINT y = 0; y < kHeight; ++y) for (UINT x = 0; x < kWidth; ++x)
    for (UINT c = 0; c < 4; ++c)
      exact &= static_cast<const unsigned char *>(map.value.pData)[y * map.value.RowPitch + x * 4 + c] == expected[c];
  require(exact, detail, negative);
}

static void adjacency_record(ID3D11DeviceContext *context, Pipeline &pipeline,
                              AdjacencyResources &r, bool use_compute, bool resolve) {
  // Setup values/resources/shaders exist before the hazard. Flush inherited
  // setup/promotion state before binding and drawing R in a fresh current chunk.
  pipeline.values(context, {1, 0, 0, 1}, 0.5f);
  const FLOAT blue[4] = {0, 0, 1, 1};
  context->ClearRenderTargetView(r.source.view.p, blue);
  context->Flush();
  pipeline.bind(context, 1, &r.source.view.p);
  context->RSSetState(r.raster.p);
  if (use_compute) {
    context->CSSetShader(r.compute.p, nullptr, 0);
    context->CSSetUnorderedAccessViews(0, 1, &r.uav.p, nullptr);
  }
  // BEGIN hazard: full-screen R writes every MSAA sample red. X writes the
  // single-sample resolve destination green. Resolve must run AFTER X.
  context->Draw(3, 0);
  // Explicitly close/unbind R before X, retaining the same command chunk.
  context->OMSetRenderTargets(0, nullptr, nullptr);
  if (use_compute) {
    context->Dispatch(kWidth / 8, kHeight / 8, 1);
    ID3D11UnorderedAccessView *none = nullptr;
    context->CSSetUnorderedAccessViews(0, 1, &none, nullptr);
  } else context->CopyResource(r.destination.p, r.green.p);
  if (resolve)
    context->ResolveSubresource(r.destination.p, 0, r.source.image.p, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
  // END hazard. No Flush/GetData/Map/query/wait/resource creation in this block.
  context->CopyResource(r.readback.p, r.destination.p);
  adjacency_complete(context, r.done.p);
}

static void adjacency_case(ID3D11Device *device, ID3D11DeviceContext *context,
                           Pipeline &pipeline, bool use_compute) {
  stage_name = use_compute ? "compute_destination_write" : "blit_destination_write";
  stage_start_checks = checks;
  UINT quality = 0, support = 0;
  step(device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, 4, &quality), "rgba8_msaa_query");
  step(device->CheckFormatSupport(DXGI_FORMAT_R8G8B8A8_UNORM, &support), "rgba8_format_query");
  const UINT required = D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_RENDER_TARGET |
                        D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET | D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE |
                        (use_compute ? D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW : 0);
  if (!quality || (support & required) != required) {
    ++stages_unsupported; report_stage("unsupported", "required_rgba8_msaa_or_typed_uav_unavailable");
    stage_name = nullptr; return;
  }
  AdjacencyResources resources(device, use_compute);
  budget();
  adjacency_record(context, pipeline, resources, use_compute, true);
  adjacency_pixels(context, resources.readback.p, {255, 0, 0, 255}, "resolved_destination_all_red");
  if (selected("wrong-golden"))
    adjacency_pixels(context, resources.readback.p, {0, 255, 0, 255}, "negative_wrong_golden", true);
  // Mandatory diagnostic control: prove X really writes green when resolve is
  // omitted. Readback occurs after its own entire R/X sequence.
  adjacency_record(context, pipeline, resources, use_compute, false);
  adjacency_pixels(context, resources.readback.p, {0, 255, 0, 255}, "omitted_resolve_destination_all_green");
  if (selected("omit-resolve"))
    adjacency_pixels(context, resources.readback.p, {255, 0, 0, 255}, "negative_omitted_resolve", true);
  ++stages_passed; report_stage("pass", "red_resolve_and_green_omission_verified");
  stage_name = nullptr; context->ClearState(); context->Flush();
}

static void adjacency_workflow() {
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
  wchar_t ack[32768]{};
  const DWORD length = GetEnvironmentVariableW(L"VIBEWINE_GPU_MAP_ACK", ack, 32768);
  require(length && length < 32768, "module_mapping_ack_required");
  const auto begin = GetTickCount64();
  while (GetFileAttributesW(ack) == INVALID_FILE_ATTRIBUTES) {
    require(GetTickCount64() - begin < 15000, "module_mapping_ack_deadline"); Sleep(10);
  }
  Pipeline pipeline(device.p);
  if (std::strcmp(adjacency_path, "blit")) adjacency_case(device.p, context.p, pipeline, true);
  if (std::strcmp(adjacency_path, "compute")) adjacency_case(device.p, context.p, pipeline, false);
  step(device->GetDeviceRemovedReason(), "device_not_removed");
  budget();
}

int main(int argc, char **argv) {
  setvbuf(stdout, nullptr, _IONBF, 0); started = GetTickCount64();
  for (int i = 1; i < argc; i += 2) {
    if (i + 1 >= argc) return 64;
    if (!std::strcmp(argv[i], "--path") && (!std::strcmp(argv[i + 1], "both") ||
        !std::strcmp(argv[i + 1], "compute") || !std::strcmp(argv[i + 1], "blit"))) adjacency_path = argv[i + 1];
    else if (!std::strcmp(argv[i], "--negative-control") &&
             (!std::strcmp(argv[i + 1], "wrong-golden") || !std::strcmp(argv[i + 1], "omit-resolve")))
      negative_control = argv[i + 1];
    else return 64;
  }
  std::printf("{\"schema\":1,\"type\":\"start\",\"probe\":\"resolve-adjacency\",\"path\":");
  json_string(adjacency_path); std::printf(",\"negative_control\":"); json_string(negative_control); std::printf("}\n");
  finished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!finished) { summary("fail", 0, "watchdog_event_create"); return 10; }
  HANDLE guard = CreateThread(nullptr, 0, watchdog, nullptr, 0, nullptr);
  if (!guard) { CloseHandle(finished); summary("fail", 0, "watchdog_thread_create"); return 10; }
  int result = 0;
  try {
    adjacency_workflow();
    if (stages_unsupported) { summary("unsupported", 0, "required_path_unsupported"); result = 2; }
    else if (!selected("none")) { summary("fail", 0, "negative_control_not_reached"); result = 10; }
    else summary("pass");
  } catch (const Failure &failure) {
    if (stage_name) report_stage(failure.unsupported ? "unsupported" : "fail", failure.detail, failure.hr);
    const bool rejected = failure.negative && !selected("none");
    summary(rejected ? "negative_control_rejected" : failure.unsupported ? "unsupported" : "fail",
            rejected ? 1 : 0, failure.detail, failure.hr);
    result = rejected ? 20 : failure.unsupported ? 2 : 10;
  } catch (...) {
    if (stage_name) report_stage("fail", "unexpected_exception");
    summary("fail", 0, "unexpected_exception"); result = 10;
  }
  SetEvent(finished); WaitForSingleObject(guard, 1000); CloseHandle(guard); CloseHandle(finished);
  return result;
}
