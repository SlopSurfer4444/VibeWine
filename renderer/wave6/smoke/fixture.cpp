#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool destroyed = false, quit = false;
static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM a, LPARAM b) {
  if (m == WM_CLOSE) { puts("NORMAL_WM_CLOSE"); DestroyWindow(w); return 0; }
  if (m == WM_DESTROY) { destroyed = true; PostQuitMessage(0); return 0; }
  return DefWindowProcW(w, m, a, b);
}
static void require(bool ok, const char *name) {
  printf("CHECK %s valid=%d\n", name, ok);
  if (!ok) ExitProcess(10);
}
static void check(HRESULT hr, const char *name) {
  printf("STEP %s hr=0x%08lx\n", name, (unsigned long)hr);
  if (FAILED(hr)) ExitProcess(11);
}
static void pump() {
  MSG msg;
  while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT) { quit = true; continue; }
    TranslateMessage(&msg); DispatchMessageW(&msg);
  }
}
static void settle(unsigned milliseconds) {
  DWORD end = GetTickCount() + milliseconds;
  do { pump(); Sleep(5); } while ((LONG)(GetTickCount() - end) < 0);
}
static void module(const wchar_t *name) {
  wchar_t path[32768]; HMODULE m = GetModuleHandleW(name);
  require(m && GetModuleFileNameW(m, path, 32768), "module_loaded");
  printf("MODULE %ls %ls\n", name, path);
}
static void windowed(IDXGISwapChain *swap) {
  BOOL full = TRUE; IDXGIOutput *output = NULL;
  check(swap->GetFullscreenState(&full, &output), "get_fullscreen_state");
  if (output) output->Release();
  require(!full, "dxgi_windowed_no_exclusive");
  DXGI_SWAP_CHAIN_DESC desc = {};
  check(swap->GetDesc(&desc), "get_desc");
  require(desc.Windowed, "desc_windowed");
  require(GetCapture() == NULL, "no_win32_capture");
}
static void layout(HWND window) {
  RECT actual, client; MONITORINFO monitor = {}; monitor.cbSize = sizeof(monitor);
  require(GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor) &&
      GetWindowRect(window, &actual) && GetClientRect(window, &client), "monitor_geometry");
  LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE), ex = GetWindowLongPtrW(window, GWL_EXSTYLE);
  printf("BORDERLESS window=%ld,%ld,%ld,%ld monitor=%ld,%ld,%ld,%ld client=%ld,%ld style=0x%llx ex=0x%llx\n",
    actual.left, actual.top, actual.right, actual.bottom, monitor.rcMonitor.left, monitor.rcMonitor.top,
    monitor.rcMonitor.right, monitor.rcMonitor.bottom, client.right, client.bottom,
    (unsigned long long)style, (unsigned long long)ex);
  require(EqualRect(&actual, &monitor.rcMonitor) && client.right == actual.right - actual.left &&
      client.bottom == actual.bottom - actual.top && !(style & WS_OVERLAPPEDWINDOW) &&
      (style & WS_POPUP) && !(ex & WS_EX_TOPMOST), "monitor_sized_native_borderless");
}
static void readback(ID3D11Device *device, ID3D11DeviceContext *context,
                     ID3D11Texture2D *buffer, unsigned phase, UINT width, UINT height) {
  D3D11_TEXTURE2D_DESC td = {}; buffer->GetDesc(&td);
  require(td.Width == width && td.Height == height, "buffer_dimensions");
  td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; td.MiscFlags = 0;
  ID3D11Texture2D *stage = NULL; check(device->CreateTexture2D(&td, NULL, &stage), "create_readback");
  context->CopyResource(stage, buffer); D3D11_MAPPED_SUBRESOURCE mapped = {};
  check(context->Map(stage, 0, D3D11_MAP_READ, 0, &mapped), "map_gpu_clear");
  UINT xs[] = {0, width - 1, width / 2, 0, width - 1};
  UINT ys[] = {0, 0, height / 2, height - 1, height - 1}; bool valid = true;
  for (unsigned i = 0; i < 5; ++i) {
    unsigned char *p = (unsigned char *)mapped.pData + ys[i] * mapped.RowPitch + 4 * xs[i];
    bool pixel = abs((int)p[0] - 32) <= 1 && abs((int)p[1] - 64) <= 1 && abs((int)p[2] - 128) <= 1 && p[3] == 255;
    printf("READBACK phase=%u point=%u rgba=%u,%u,%u,%u valid=%d\n", phase, i, p[0], p[1], p[2], p[3], pixel);
    valid = valid && pixel;
  }
  context->Unmap(stage, 0); stage->Release(); require(valid, "gpu_clear_five_points");
}
int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);
  bool borderless = argc == 2 && !strcmp(argv[1], "--borderless");
  require(argc == 1 || borderless, "fixture_arguments");
  wchar_t own[32768]; require(GetModuleFileNameW(NULL, own, 32768), "fixture_identity");
  printf("FIXTURE pid=%lu thread=%lu borderless=%d exe=%ls\n", GetCurrentProcessId(), GetCurrentThreadId(), borderless, own);
  WNDCLASSW cls = {}; cls.lpfnWndProc = proc; cls.hInstance = GetModuleHandleW(NULL); cls.lpszClassName = L"Wave6PrivateGPUClear";
  require(RegisterClassW(&cls), "register_class");
  HWND window = CreateWindowW(cls.lpszClassName, L"Wave6 private DXMT smoke fixture", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
      80, 80, 640, 480, NULL, NULL, cls.hInstance, NULL); require(window != NULL, "create_hwnd");
  DXGI_SWAP_CHAIN_DESC desc = {}; desc.BufferDesc.Width = borderless ? 1920 : 640;
  desc.BufferDesc.Height = borderless ? 1080 : 480; desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount = 2;
  desc.OutputWindow = window; desc.Windowed = TRUE; desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
  ID3D11Device *device = NULL; ID3D11DeviceContext *context = NULL; IDXGISwapChain *swap = NULL;
  check(D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION,
      &desc, &swap, &device, NULL, &context), "create_windowed_hardware");
  module(L"d3d11.dll"); module(L"dxgi.dll"); module(L"winemetal.dll");
  IDXGIFactory *factory = NULL; check(swap->GetParent(__uuidof(IDXGIFactory), (void **)&factory), "factory");
  check(factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES), "disable_mode_switch"); factory->Release();
  windowed(swap); if (borderless) layout(window);
  unsigned frames = 0;
  for (unsigned phase = 0; phase < (borderless ? 3u : 2u); ++phase) {
    bool reduce = phase > 0 && (!borderless || phase == 2);
    UINT width = reduce ? (borderless ? 1280 : 800) : desc.BufferDesc.Width;
    UINT height = reduce ? (borderless ? 720 : 600) : desc.BufferDesc.Height;
    if (phase) {
      if (!borderless) require(SetWindowPos(window, NULL, 0, 0, 800, 600, SWP_NOMOVE | SWP_NOZORDER), "window_resize");
      bool zero = borderless && phase == 1;
      check(swap->ResizeBuffers(2, zero ? 0 : width, zero ? 0 : height, DXGI_FORMAT_R8G8B8A8_UNORM, 0),
          zero ? "resize_zero_preserve_render_resolution" : "resize_windowed_buffers");
      DXGI_SWAP_CHAIN_DESC resized = {}; check(swap->GetDesc(&resized), "resized_desc");
      require(resized.Windowed && resized.BufferDesc.Width == width && resized.BufferDesc.Height == height,
          zero ? "zero_resize_keeps_1920x1080" : "explicit_resize_dimensions");
    }
    ID3D11Texture2D *buffer = NULL; ID3D11RenderTargetView *view = NULL;
    check(swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&buffer), "get_buffer");
    check(device->CreateRenderTargetView(buffer, NULL, &view), "create_rtv");
    const FLOAT color[] = {0.125f, 0.25f, 0.5f, 1.0f}; context->ClearRenderTargetView(view, color);
    readback(device, context, buffer, phase, width, height);
    DWORD until = GetTickCount() + 2000; unsigned first = frames;
    while ((LONG)(GetTickCount() - until) < 0) {
      pump(); require(!quit && !destroyed, "render_window_alive");
      context->ClearRenderTargetView(view, color); check(swap->Present(1, 0), "present"); ++frames; Sleep(5);
    }
    require(frames > first, "phase_presented"); windowed(swap); if (borderless) layout(window);
    context->ClearState(); context->Flush(); view->Release(); buffer->Release();
    if (borderless && phase == 0) {
      ShowWindow(window, SW_MINIMIZE); settle(150); require(IsIconic(window), "minimized");
      ShowWindow(window, SW_RESTORE); settle(250); require(!IsIconic(window), "restored");
      SetFocus(window); layout(window); windowed(swap);
    }
  }
  printf("PRESENTED frames=%u\n", frames); context->ClearState(); context->Flush(); puts("RELEASE_BEGIN");
  swap->Release(); context->Release(); device->Release();
  SendMessageW(window, WM_CLOSE, 0, 0); pump();
  require(destroyed && quit && !IsWindow(window), "normal_close_destroy_quit");
  printf("SMOKE_PASS mode=%s gpu_readback_resize_present_normal_close=1\n", borderless ? "borderless" : "windowed");
  return 0;
}
