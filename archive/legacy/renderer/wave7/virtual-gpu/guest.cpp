// Isolated Win32 virtual-mode probe. Compile only in this branch; root owns launch.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <cmath>
#include <algorithm>

static WCHAR device[CCHDEVICENAME] = {};
static DEVMODEW initial_mode = {};
static HWND window_handle;
static std::string gates;
static bool changed;
struct Failure { const char *reason; };
template<class T> struct Ref {
  T *p = nullptr;
  ~Ref() { reset(); }
  void reset() { if (p) { p->Release(); p = nullptr; } }
  T **out() { reset(); return &p; }
  T *operator->() const { return p; }
};
static Ref<ID3D11Device> device_obj;
static Ref<ID3D11DeviceContext> context_obj;
static Ref<IDXGISwapChain> swapchain;
static bool normal_close;
static bool quit_seen;
static unsigned total_frames;
static void need(bool ok,const char *why) { if (!ok) throw Failure{why}; }
static void ok(HRESULT result,const char *why) { need(result == S_OK,why); }
static LRESULT CALLBACK window_proc(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
  if (message == WM_CLOSE) { DestroyWindow(hwnd); return 0; }
  if (message == WM_DESTROY) { normal_close = true; PostQuitMessage(0); return 0; }
  return DefWindowProcW(hwnd,message,w,l);
}


static void pump() {
  MSG m;
  while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
    if (m.message == WM_QUIT) { quit_seen = true; continue; }
    TranslateMessage(&m); DispatchMessageW(&m);
  }
}
static bool gate(unsigned id) {
  std::string name = gates + "\\gate-" + std::to_string(id) + ".go";
  const auto start = GetTickCount64();
  while (GetFileAttributesA(name.c_str()) == INVALID_FILE_ATTRIBUTES) {
    if (GetTickCount64() - start >= 7000) return false;
    pump(); Sleep(10);
  }
  return true;
}
static bool option(const wchar_t *key, const wchar_t *value, bool yes) {
  wchar_t b[16] = {}; DWORD bytes = sizeof(b), type = 0;
  if (RegGetValueW(HKEY_CURRENT_USER, key, value, RRF_RT_REG_SZ, &type, b, &bytes) != ERROR_SUCCESS)
    return false;
  const bool enabled = b[0] == L'Y' || b[0] == L'y' || b[0] == L'1' || b[0] == L'T' || b[0] == L't';
  return enabled == yes;
}
static bool current(DEVMODEW &mode, DWORD which = ENUM_CURRENT_SETTINGS) {
  mode = {}; mode.dmSize = sizeof(mode);
  return EnumDisplaySettingsExW(device, which, &mode, 0);
}
static std::vector<DEVMODEW> enumerate_modes(DWORD flags, bool &truncated) {
  std::vector<DEVMODEW> result;
  truncated = false;
  for (DWORD n = 0; n <= 512; ++n) {
    DEVMODEW mode = {}; mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsExW(device,n,&mode,flags)) break;
    if (n == 512) { truncated = true; break; }
    result.push_back(mode);
  }
  return result;
}
static void print_modes(const std::vector<DEVMODEW> &modes) {
  std::putchar('[');
  for (size_t n = 0; n != modes.size(); ++n) {
    const auto &m = modes[n];
    std::printf("%s{\"width\":%lu,\"height\":%lu,\"bits_per_pixel\":%lu,\"refresh_hz\":%lu,\"orientation\":%lu,\"fields\":%lu}",
      n ? "," : "",m.dmPelsWidth,m.dmPelsHeight,m.dmBitsPerPel,m.dmDisplayFrequency,m.dmDisplayOrientation,m.dmFields);
  }
  std::putchar(']');
}
static LONG restore() {
  if (!changed) return 0;
  // All normal and failure restoration is through the real DXGI contract.
  // There is no direct ChangeDisplaySettings or physical-mode fallback here.
  if (!swapchain.p) return -1;
  const HRESULT result = swapchain->SetFullscreenState(FALSE,nullptr);
  if (result == S_OK) changed = false;
  return LONG(result);
}
static int fail(const char *reason) {
  LONG restored = restore();
  if (context_obj.p) context_obj->ClearState();
  swapchain.reset(); context_obj.reset(); device_obj.reset();
  if (window_handle && IsWindow(window_handle)) DestroyWindow(window_handle);
  std::printf("{\"stage\":\"failure\",\"reason\":\"%s\",\"restore_result\":%ld}\n",reason,long(restored));
  std::fflush(stdout);
  return 2;
}
static const char shader[] = R"(
cbuffer Size : register(b0) { float width; float height; float2 unused; };
float4 vs(uint id:SV_VertexID):SV_Position {
 return float4(id==2 ? 3.0 : -1.0, id==1 ? 3.0 : -1.0, 0.0, 1.0);
}
float4 ps(float4 pos:SV_Position):SV_Target {
 float2 delta=pos.xy-float2(width,height)*0.5;
 float radius=min(width,height)*0.15;
 if (dot(delta,delta)<radius*radius) return float4(1,0,1,1);
 float2 uv=pos.xy/float2(width,height);
 if (uv.x<0.5) return uv.y<0.5 ? float4(1,0,0,1) : float4(0,0,1,1);
 return uv.y<0.5 ? float4(0,1,0,1) : float4(1,1,0,1);
})";
struct Pipeline {
 Ref<ID3D11VertexShader> vs;
 Ref<ID3D11PixelShader> ps;
 Ref<ID3D11Buffer> size;
 Ref<ID3D11RasterizerState> raster;
 Pipeline() {
  Ref<ID3DBlob> vb,pb,errors;
  ok(D3DCompile(shader,sizeof(shader)-1,"virtual-fullscreen-pattern",nullptr,nullptr,"vs","vs_4_0",D3DCOMPILE_ENABLE_STRICTNESS,0,vb.out(),errors.out()),"compile_vs_failed");
  ok(D3DCompile(shader,sizeof(shader)-1,"virtual-fullscreen-pattern",nullptr,nullptr,"ps","ps_4_0",D3DCOMPILE_ENABLE_STRICTNESS,0,pb.out(),errors.out()),"compile_ps_failed");
  ok(device_obj->CreateVertexShader(vb->GetBufferPointer(),vb->GetBufferSize(),nullptr,vs.out()),"create_vs_failed");
  ok(device_obj->CreatePixelShader(pb->GetBufferPointer(),pb->GetBufferSize(),nullptr,ps.out()),"create_ps_failed");
  D3D11_BUFFER_DESC bd={}; bd.ByteWidth=16; bd.Usage=D3D11_USAGE_DEFAULT; bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
  ok(device_obj->CreateBuffer(&bd,nullptr,size.out()),"create_size_buffer_failed");
  D3D11_RASTERIZER_DESC rd={}; rd.FillMode=D3D11_FILL_SOLID; rd.CullMode=D3D11_CULL_NONE; rd.DepthClipEnable=TRUE;
  ok(device_obj->CreateRasterizerState(&rd,raster.out()),"create_raster_failed");
 }
 void draw(ID3D11RenderTargetView *rtv,UINT width,UINT height) {
  float data[]={float(width),float(height),0,0};
  context_obj->UpdateSubresource(size.p,0,nullptr,data,0,0);
  D3D11_VIEWPORT viewport={0,0,float(width),float(height),0,1};
  context_obj->RSSetViewports(1,&viewport); context_obj->RSSetState(raster.p);
  context_obj->OMSetRenderTargets(1,&rtv,nullptr);
  context_obj->IASetInputLayout(nullptr); context_obj->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_obj->VSSetShader(vs.p,nullptr,0); context_obj->PSSetShader(ps.p,nullptr,0); context_obj->PSSetConstantBuffers(0,1,&size.p);
  context_obj->Draw(3,0);
 }
};
struct Readback {
 unsigned char samples[9][4] = {};
 unsigned horizontal = 0, vertical = 0;
 bool pattern = false, ratio = false;
};
static Readback readback(ID3D11Texture2D *buffer) {
 D3D11_TEXTURE2D_DESC desc={}; buffer->GetDesc(&desc);
 desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ; desc.MiscFlags=0;
 Ref<ID3D11Texture2D> staging;
 ok(device_obj->CreateTexture2D(&desc,nullptr,staging.out()),"create_staging_failed");
 context_obj->CopyResource(staging.p,buffer);
 D3D11_MAPPED_SUBRESOURCE mapped={};
 ok(context_obj->Map(staging.p,0,D3D11_MAP_READ,0,&mapped),"map_readback_failed");
 Readback result; result.pattern=true;
 const UINT xs[]={0,desc.Width-1,0,desc.Width-1,desc.Width/2,desc.Width/4,3*desc.Width/4,desc.Width/4,3*desc.Width/4};
 const UINT ys[]={0,0,desc.Height-1,desc.Height-1,desc.Height/2,desc.Height/4,desc.Height/4,3*desc.Height/4,3*desc.Height/4};
 const unsigned char golden[9][4]={{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,0,255},{255,0,255,255},{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,0,255}};
 for(unsigned i=0;i<9;++i) {
  const auto *pixel=static_cast<const unsigned char *>(mapped.pData)+ys[i]*mapped.RowPitch+xs[i]*4;
  for(unsigned c=0;c<4;++c) { result.samples[i][c]=pixel[c]; result.pattern &= std::abs(int(pixel[c])-int(golden[i][c]))<=1; }
 }
 auto magenta=[&](UINT x,UINT y) {
  const auto *pixel=static_cast<const unsigned char *>(mapped.pData)+y*mapped.RowPitch+x*4;
  return pixel[0]>=254 && pixel[1]<=1 && pixel[2]>=254 && pixel[3]>=254;
 };
 for(UINT x=0;x<desc.Width;++x) result.horizontal+=magenta(x,desc.Height/2);
 for(UINT y=0;y<desc.Height;++y) result.vertical+=magenta(desc.Width/2,y);
 const int diameter=int(std::lround(0.30*std::min(desc.Width,desc.Height)));
 result.ratio=std::abs(int(result.horizontal)-int(result.vertical))<=2 &&
  std::abs(int(result.horizontal)-diameter)<=2 && std::abs(int(result.vertical)-diameter)<=2;
 context_obj->Unmap(staging.p,0);
 need(result.pattern,"gpu_pattern_failed"); need(result.ratio,"gpu_circle_ratio_failed");
 return result;
}
static void print_samples(const Readback &result) {
 std::putchar('[');
 for(unsigned i=0;i<9;++i) std::printf("%s{\"point\":%u,\"rgba\":[%u,%u,%u,%u],\"valid\":true}",i?",":"",i,result.samples[i][0],result.samples[i][1],result.samples[i][2],result.samples[i][3]);
 std::putchar(']');
}
int main(int argc, char **argv) {
  if (argc != 3 || std::strcmp(argv[1], "--root-authorized-isolated-probe")) return 64;
  char prefix[4096] = {}, authorize[4] = {};
  GetEnvironmentVariableA("WINEPREFIX",prefix,sizeof(prefix));
  GetEnvironmentVariableA("W7VM_ROOT_PROBE",authorize,sizeof(authorize));
  if (!std::strstr(prefix,"/wsi-query-impl/virtual-mode/d3d/") || std::strcmp(authorize,"1")) return 65;
  wchar_t executable[4096] = {};
  if (!GetModuleFileNameW(nullptr,executable,4096)) return 66;
  const wchar_t *basename = std::wcsrchr(executable,L'\\');
  basename = basename ? basename + 1 : executable;
  if (_wcsicmp(basename,L"VirtualFullscreenProbe.exe")) return 66;
  gates = argv[2];
  if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
    return fail("dpi_awareness_setup_failed");
  // Read-only checks of the exact per-executable settings already installed by
  // the root in its private prefix. No registry write occurs in this program.
  const bool emulate = option(L"Software\\Wine\\AppDefaults\\virtualfullscreenprobe.exe\\X11 Driver",L"EmulateModeset",true);
  const bool global_emulate = option(L"Software\\Wine\\X11 Driver",L"EmulateModeset",true);
  const bool capture_off = option(L"Software\\Wine\\AppDefaults\\virtualfullscreenprobe.exe\\Mac Driver",L"CaptureDisplaysForFullscreen",false);
  const bool retina = option(L"Software\\Wine\\Mac Driver",L"RetinaMode",true);
  if (!emulate || !global_emulate || !capture_off) return fail("missing_isolated_mode_or_capture_configuration");
  DISPLAY_DEVICEW display = {}; display.cb = sizeof(display);
  bool found = false;
  for (DWORD n = 0; EnumDisplayDevicesW(nullptr,n,&display,0); ++n) {
    if ((display.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) && (display.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)) {
      std::wcsncpy(device,display.DeviceName,CCHDEVICENAME-1); found = true; break;
    }
    display = {}; display.cb = sizeof(display);
  }
  DEVMODEW registry = {};
  if (!found || !current(initial_mode) || !current(registry,ENUM_REGISTRY_SETTINGS)) return fail("mode_baseline_query_failed");
  WNDCLASSW wc={}; wc.lpfnWndProc=window_proc; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"Wave7VirtualFullscreenProbe";
  if (!RegisterClassW(&wc)) return fail("window_class_failed");
  window_handle=CreateWindowW(wc.lpszClassName,L"Wave7VirtualFullscreenProbe",WS_POPUP,0,0,1920,1080,nullptr,nullptr,wc.hInstance,nullptr);
  if (window_handle) { ShowWindow(window_handle,SW_SHOW); pump(); Sleep(80); pump(); }
  RECT initial_client={},initial_window={};
  if (!window_handle || !GetClientRect(window_handle,&initial_client) || !GetWindowRect(window_handle,&initial_window)) return fail("initial_client_query_failed");
  bool truncated = false, raw_truncated = false;
  const auto normal_modes = enumerate_modes(0,truncated);
  const auto raw_available = enumerate_modes(EDS_RAWMODE,raw_truncated);
  std::printf("{\"stage\":\"baseline\",\"width\":%lu,\"height\":%lu,\"registry_width\":%lu,\"registry_height\":%lu,\"initial_client_width\":%ld,\"initial_client_height\":%ld,\"initial_client_rect\":[%ld,%ld,%ld,%ld],\"initial_window_rect\":[%ld,%ld,%ld,%ld],\"bits_per_pixel\":%lu,\"refresh_hz\":%lu,\"dm_fields\":%lu,\"app_emulate\":true,\"global_emulate\":true,\"capture_disabled\":true,\"retina_enabled\":%s,\"available_modes_truncated\":%s,\"available_modes\":",
    initial_mode.dmPelsWidth,initial_mode.dmPelsHeight,registry.dmPelsWidth,registry.dmPelsHeight,
    initial_client.right-initial_client.left,initial_client.bottom-initial_client.top,
    initial_client.left,initial_client.top,initial_client.right,initial_client.bottom,
    initial_window.left,initial_window.top,initial_window.right,initial_window.bottom,initial_mode.dmBitsPerPel,initial_mode.dmDisplayFrequency,initial_mode.dmFields,retina?"true":"false",truncated?"true":"false");
  print_modes(normal_modes);
  std::printf(",\"raw_available_modes_truncated\":%s,\"raw_available_modes\":",raw_truncated?"true":"false");
  print_modes(raw_available);
  std::puts("}");
  std::fflush(stdout);
  const DWORD widths[] = {1920,2560,1920}, heights[] = {1080,1440,1080};
  if (truncated) return fail("available_mode_enumeration_truncated");
  for (unsigned seq = 0; seq != 3; ++seq) {
    bool found_mode = false;
    for (const auto &mode : normal_modes)
      if (mode.dmPelsWidth == widths[seq] && mode.dmPelsHeight == heights[seq] && mode.dmBitsPerPel == initial_mode.dmBitsPerPel)
        found_mode = true;
    if (!found_mode) return fail("requested_virtual_mode_not_enumerated");
  }
  if (!gate(0)) return fail("baseline_gate_timeout");
  // These wrappers are Wine-internal inline functions, not DLL exports.
  // Exact installed macdrv disassembly uses NtUserCallHwndParam codes28/14
  // for the raw monitor DPI and client rect (installed-native-window.asm).
  using HwndParam = ULONG_PTR (WINAPI *)(HWND, ULONG_PTR, DWORD);
  auto win32u = GetModuleHandleW(L"win32u.dll");
  auto hwnd_param = reinterpret_cast<HwndParam>(GetProcAddress(win32u,"NtUserCallHwndParam"));
  if (!hwnd_param) return fail("raw_dpi_query_seam_missing");
  struct WindowRectsParams { RECT *rect; UINT dpi; };
  static_assert(sizeof(WindowRectsParams) == 16, "pinned x64 Wine rect-query ABI");
  try {
   DXGI_SWAP_CHAIN_DESC desc={}; desc.BufferDesc.Width=1920; desc.BufferDesc.Height=1080; desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
   desc.SampleDesc.Count=1; desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount=2;
   desc.OutputWindow=window_handle; desc.Windowed=TRUE; desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
   desc.Flags=DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
   ok(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,swapchain.out(),device_obj.out(),nullptr,context_obj.out()),"create_device_swapchain_failed");
   Ref<IDXGIFactory> factory;
   ok(swapchain->GetParent(__uuidof(IDXGIFactory),reinterpret_cast<void **>(factory.out())),"get_factory_failed");
   ok(factory->MakeWindowAssociation(window_handle,DXGI_MWA_NO_ALT_ENTER|DXGI_MWA_NO_WINDOW_CHANGES),"window_association_failed");
   factory.reset();
   ShowWindow(window_handle,SW_SHOW); pump(); Sleep(80); pump();
   RECT preentry_client={},preentry_window={};
   need(GetClientRect(window_handle,&preentry_client) && GetWindowRect(window_handle,&preentry_window) && EqualRect(&preentry_window,&initial_window) &&
        preentry_client.right-preentry_client.left==initial_client.right-initial_client.left &&
        preentry_client.bottom-preentry_client.top==initial_client.bottom-initial_client.top,
        "preentry_client_changed_before_fullscreen");
   changed=true; // The real entry can leave a partial obligation on failure.
   const HRESULT fullscreen_result=swapchain->SetFullscreenState(TRUE,nullptr);
   ok(fullscreen_result,"actual_fullscreen_entry_failed");
   {
    Pipeline pipeline;
  for (unsigned seq = 0; seq != 3; ++seq) {
    context_obj->ClearState();
    DXGI_MODE_DESC target={}; target.Width=widths[seq]; target.Height=heights[seq]; target.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    const HRESULT target_result=swapchain->ResizeTarget(&target);
    ok(target_result,"actual_fullscreen_resize_target_failed");
    const HRESULT buffers_result=swapchain->ResizeBuffers(0,0,0,DXGI_FORMAT_UNKNOWN,DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
    ok(buffers_result,"actual_zero_resize_buffers_failed");
    DEVMODEW observed={}; RECT client={},raw_client={},logical_window={};
    UINT dpi=UINT(hwnd_param(window_handle,2,28)); WindowRectsParams raw_params={&raw_client,dpi};
    need(current(observed) && GetClientRect(window_handle,&client) && hwnd_param(window_handle,reinterpret_cast<ULONG_PTR>(&raw_params),14),"mode_window_query_failed");
    BOOL fullscreen=FALSE; Ref<IDXGIOutput> output;
    ok(swapchain->GetFullscreenState(&fullscreen,output.out()),"get_fullscreen_state_failed");
    DXGI_SWAP_CHAIN_DESC public_desc={}; ok(swapchain->GetDesc(&public_desc),"get_public_desc_failed");
    Ref<ID3D11Texture2D> buffer; ok(swapchain->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void **>(buffer.out())),"get_actual_buffer_failed");
    D3D11_TEXTURE2D_DESC buffer_desc={}; buffer->GetDesc(&buffer_desc);
    need(fullscreen && output.p && !public_desc.Windowed,"fullscreen_state_not_truthful");
    need(observed.dmPelsWidth==widths[seq] && observed.dmPelsHeight==heights[seq] &&
         DWORD(client.right-client.left)==widths[seq] && DWORD(client.bottom-client.top)==heights[seq],"actual_mode_client_mismatch");
    need(buffer_desc.Width==widths[seq] && buffer_desc.Height==heights[seq] &&
         public_desc.BufferDesc.Width==widths[seq] && public_desc.BufferDesc.Height==heights[seq] &&
         buffer_desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM && buffer_desc.SampleDesc.Count==1,"actual_public_buffer_mismatch");
    Ref<ID3D11RenderTargetView> rtv;
    ok(device_obj->CreateRenderTargetView(buffer.p,nullptr,rtv.out()),"create_rtv_failed");
    pipeline.draw(rtv.p,widths[seq],heights[seq]);
    const Readback checked=readback(buffer.p);
    unsigned presents=0;
    for(unsigned frame=0;frame<3;++frame) {
     pipeline.draw(rtv.p,widths[seq],heights[seq]);
     ok(swapchain->Present(1,0),"present_failed"); ++presents; ++total_frames; pump(); Sleep(10);
    }
    // Native window callbacks are asynchronous. Reobserve after presentation,
    // so the JSON cannot reuse a client/descriptor snapshot taken before clamp.
    pump(); Sleep(80); pump();
    dpi=UINT(hwnd_param(window_handle,2,28)); raw_params.dpi=dpi;
    need(current(observed) && GetClientRect(window_handle,&client) && GetWindowRect(window_handle,&logical_window) &&
         hwnd_param(window_handle,reinterpret_cast<ULONG_PTR>(&raw_params),14),"post_present_geometry_query_failed");
    output.reset(); ok(swapchain->GetFullscreenState(&fullscreen,output.out()),"post_present_fullscreen_query_failed");
    ok(swapchain->GetDesc(&public_desc),"post_present_public_desc_failed");
    buffer->GetDesc(&buffer_desc);
    std::printf("{\"stage\":\"mode\",\"seq\":%u,\"width\":%lu,\"height\":%lu,\"current_width\":%lu,\"current_height\":%lu,\"client_width\":%ld,\"client_height\":%ld,\"raw_client_width\":%ld,\"raw_client_height\":%ld,\"logical_client_rect\":[%ld,%ld,%ld,%ld],\"raw_client_rect\":[%ld,%ld,%ld,%ld],\"logical_window_rect\":[%ld,%ld,%ld,%ld],\"raw_dpi_x\":%u,\"raw_dpi_y\":%u,\"change_result\":0,\"full\":%s,\"desc_windowed\":%s,\"buffer_width\":%u,\"buffer_height\":%u,\"public_width\":%u,\"public_height\":%u,\"resize_target_result\":%ld,\"resize_buffers_result\":%ld,\"fullscreen_result\":%ld,\"gpu_pattern_valid\":true,\"gpu_ratio_valid\":true,\"horizontal_span\":%u,\"vertical_span\":%u,\"presents\":%u,\"frame_count\":%u,\"samples\":",
      seq,widths[seq],heights[seq],observed.dmPelsWidth,observed.dmPelsHeight,client.right-client.left,client.bottom-client.top,raw_client.right-raw_client.left,raw_client.bottom-raw_client.top,
      client.left,client.top,client.right,client.bottom,raw_client.left,raw_client.top,raw_client.right,raw_client.bottom,
      logical_window.left,logical_window.top,logical_window.right,logical_window.bottom,dpi,dpi,
      fullscreen?"true":"false",public_desc.Windowed?"true":"false",buffer_desc.Width,buffer_desc.Height,public_desc.BufferDesc.Width,public_desc.BufferDesc.Height,long(target_result),long(buffers_result),long(fullscreen_result),checked.horizontal,checked.vertical,presents,total_frames);
    print_samples(checked); std::puts("}"); std::fflush(stdout);
    need(fullscreen && output.p && !public_desc.Windowed &&
         observed.dmPelsWidth==widths[seq] && observed.dmPelsHeight==heights[seq] &&
         DWORD(client.right-client.left)==widths[seq] && DWORD(client.bottom-client.top)==heights[seq] &&
         buffer_desc.Width==widths[seq] && buffer_desc.Height==heights[seq] &&
         public_desc.BufferDesc.Width==widths[seq] && public_desc.BufferDesc.Height==heights[seq],
         "post_present_actual_state_mismatch");
    // Release every public backbuffer/view reference before the next resize.
    context_obj->ClearState(); rtv.reset(); buffer.reset(); output.reset();
    if (!gate(seq+1)) return fail("mode_gate_timeout");
   }
  }
  LONG result=restore(); pump(); Sleep(80); pump();
  DEVMODEW restored={}; RECT restored_client={},restored_window={};
  BOOL full=TRUE; DXGI_SWAP_CHAIN_DESC restored_desc={};
  if (result!=0 || !current(restored) || !GetClientRect(window_handle,&restored_client) || !GetWindowRect(window_handle,&restored_window)) return fail("explicit_dxgi_restore_failed");
  ok(swapchain->GetFullscreenState(&full,nullptr),"get_restored_fullscreen_state_failed");
  ok(swapchain->GetDesc(&restored_desc),"get_restored_public_desc_failed");
  need(!full && restored_desc.Windowed,"restored_descriptor_not_windowed");
  need(restored.dmPelsWidth==initial_mode.dmPelsWidth && restored.dmPelsHeight==initial_mode.dmPelsHeight,"observed_restore_mode_wrong");
  need(EqualRect(&restored_client,&initial_client) && EqualRect(&restored_window,&initial_window),"observed_restore_client_wrong");
  std::printf("{\"stage\":\"restored\",\"current_width\":%lu,\"current_height\":%lu,\"restore_result\":%ld,\"fullscreen\":false,\"desc_windowed\":true,\"client_width\":%ld,\"client_height\":%ld,\"logical_client_rect\":[%ld,%ld,%ld,%ld],\"logical_window_rect\":[%ld,%ld,%ld,%ld]}\n",restored.dmPelsWidth,restored.dmPelsHeight,long(result),restored_client.right-restored_client.left,restored_client.bottom-restored_client.top,
    restored_client.left,restored_client.top,restored_client.right,restored_client.bottom,
    restored_window.left,restored_window.top,restored_window.right,restored_window.bottom);
  std::fflush(stdout);
  if (!gate(4)) return fail("restore_gate_timeout");
  context_obj->ClearState(); swapchain.reset(); context_obj.reset(); device_obj.reset();
  need(PostMessageW(window_handle,WM_CLOSE,0,0),"normal_close_post_failed");
  const auto close_start=GetTickCount64();
  while (IsWindow(window_handle) && GetTickCount64()-close_start<2000) { pump(); Sleep(5); }
  need(normal_close && quit_seen && !IsWindow(window_handle),"normal_close_not_observed");
  window_handle=nullptr;
  std::puts("{\"stage\":\"natural_exit\",\"exit_code\":0,\"normal_close\":true,\"com_released\":true}"); std::fflush(stdout);
  return 0;
  } catch (const Failure &failure) { return fail(failure.reason); }
}
