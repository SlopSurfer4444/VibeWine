// SPDX-License-Identifier: LGPL-2.1-or-later
// D3D11 finite benchmark. Device/window/buffer pattern and cube topology
// adapted from components/dxmt/tests/dx11/dx11_cube.cpp; see PROVENANCE.md.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tlhelp32.h>
#include <vector>
#include <windows.h>

struct Failure : std::runtime_error {
  HRESULT hr;
  Failure(const std::string &s, HRESULT h = E_FAIL)
      : std::runtime_error(s), hr(h) {}
};
static void check(HRESULT hr, const char *where) {
  if (FAILED(hr))
    throw Failure(where, hr);
}
static std::string esc(const std::string &s) {
  std::string o = "\"";
  for (unsigned char c : s) {
    if (c == '\\' || c == '\"') {
      o += '\\';
      o += char(c);
    } else if (c < 32) {
      char b[8];
      snprintf(b, sizeof b, "\\u%04x", c);
      o += b;
    } else
      o += char(c);
  }
  return o + '"';
}
static double freq;
static double clocksec() {
  LARGE_INTEGER q;
  QueryPerformanceCounter(&q);
  return double(q.QuadPart) / freq;
}
struct Options {
  std::string scene = "cube", output, runId;
  int width = 1920, height = 1080, vsync = 0;
  int windowWidth = 0, windowHeight = 0;
  double duration = 3, warmup = 1;
  unsigned frames = 0;
  unsigned presentEvery = 1;
  bool barrier = false, imageProof = false;
};
static const char *metricScope =
    "CPU wall time per rendered iteration; not GPU execution or display FPS";
static bool presents(const Options &o, unsigned n) {
  return (n + 1) % o.presentEvery == 0;
}
static std::string path(const Options &o, const char *n) {
  return o.output + "\\" + n;
}
static void writeAtomic(const Options &o, const char *n, const std::string &s) {
  auto p = path(o, n), tmp = p + ".tmp";
  FILE *f = fopen(tmp.c_str(), "wb");
  if (!f)
    throw Failure("open output " + p, HRESULT_FROM_WIN32(GetLastError()));
  bool ok = fwrite(s.data(), 1, s.size(), f) == s.size();
  if (fclose(f))
    ok = false;
  if (!ok)
    throw Failure("write output " + p);
  if (!MoveFileExA(tmp.c_str(), p.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    throw Failure("publish output " + p, HRESULT_FROM_WIN32(GetLastError()));
}
static std::string identity(const Options &o) {
  return "\"schema\":1,\"runId\":" + esc(o.runId) +
         ",\"pid\":" + std::to_string(GetCurrentProcessId());
}
static Options parse(int argc, char **argv) {
  Options o;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--image-proof") {
      o.imageProof = true;
      continue;
    }
    if (a == "--barrier") {
      o.barrier = true;
      continue;
    }
    if (i + 1 == argc)
      throw Failure("missing option value");
    const char *v = argv[++i];
    char *e = nullptr;
    auto number = [&]() {
      double n = strtod(v, &e);
      if (!e || *e || !std::isfinite(n))
        throw Failure("invalid numeric option");
      return n;
    };
    if (a == "--scene")
      o.scene = v;
    else if (a == "--output")
      o.output = v;
    else if (a == "--run-id")
      o.runId = v;
    else if (a == "--duration")
      o.duration = number();
    else if (a == "--warmup")
      o.warmup = number();
    else if (a == "--present-every") {
      // Digits only: reject signs, whitespace, exponents, fractions and zero.
      unsigned n = 0;
      if (!*v)
        throw Failure("invalid present-every (expected digits 1..64)");
      for (const char *p = v; *p; ++p) {
        if (*p < '0' || *p > '9')
          throw Failure("invalid present-every (expected digits 1..64)");
        n = n * 10 + unsigned(*p - '0');
        if (n > 64)
          throw Failure("present-every outside bounded range 1..64");
      }
      if (!n)
        throw Failure("present-every outside bounded range 1..64");
      o.presentEvery = n;
    }
    else if (a == "--width" || a == "--height" || a == "--window-width" ||
             a == "--window-height" || a == "--vsync" ||
             a == "--frames") {
      double n = number();
      if (n != floor(n) || n < 0 || n > 1000000)
        throw Failure("invalid integer option");
      if (a == "--width")
        o.width = int(n);
      else if (a == "--height")
        o.height = int(n);
      else if (a == "--window-width")
        o.windowWidth = int(n);
      else if (a == "--window-height")
        o.windowHeight = int(n);
      else if (a == "--vsync")
        o.vsync = int(n);
      else
        o.frames = unsigned(n);
    } else
      throw Failure("unknown option " + a);
  }
  if (o.output.size() < 3 || o.output[1] != ':' ||
      (o.output[2] != '\\' && o.output[2] != '/'))
    throw Failure("absolute Windows output path required");
  if (o.output.empty() || o.output.size() > 220 || o.runId.empty() ||
      o.runId.size() > 64)
    throw Failure("output and run-id required (bounded lengths)");
  for (char c : o.runId)
    if (!std::isalnum((unsigned char)c) && c != '-')
      throw Failure("invalid run-id");
  if ((o.windowWidth == 0) != (o.windowHeight == 0))
    throw Failure("window dimensions must be supplied together");
  if (!o.windowWidth) { o.windowWidth = o.width; o.windowHeight = o.height; }
  if (o.windowWidth < 64 || o.windowWidth > 4096 || o.windowHeight < 64 || o.windowHeight > 2160 ||
      o.width < 64 || o.width > 4096 || o.height < 64 || o.height > 2160 ||
      o.vsync > 1 || o.duration < 0.1 || o.duration > 60 || o.warmup < 0 ||
      o.warmup > 30)
    throw Failure("option outside bounded range");
  if (o.imageProof && o.width > 3840)
    throw Failure("image proof render width exceeds 3840");
  if (o.scene != "cube" && o.scene != "drawstress" && o.scene != "instanced" &&
      o.scene != "dynamicupload" && o.scene != "pixelheavy" &&
      o.scene != "textured")
    throw Failure("unsupported scene");
  return o;
}
static std::atomic<ULONGLONG> heartbeat;
static HANDLE stopEvent;
static std::string watchdogPath;
static DWORD WINAPI guard(LPVOID) {
  while (WaitForSingleObject(stopEvent, 250) == WAIT_TIMEOUT) {
    if (GetTickCount64() - heartbeat.load() > 15000) {
      FILE *f = fopen(watchdogPath.c_str(), "wb");
      if (f) {
        fputs("{\"status\":\"timeout\",\"reason\":\"guest heartbeat stalled "
              "for 15 seconds\"}\n",
              f);
        fclose(f);
      }
      TerminateProcess(GetCurrentProcess(), 124);
    }
  }
  return 0;
}
static void beat() { heartbeat = GetTickCount64(); }
static bool closed = false;
static LRESULT CALLBACK wndproc(HWND w, UINT m, WPARAM a, LPARAM b) {
  if (m == WM_CLOSE) {
    closed = true;
    return 0;
  }
  if (m == WM_DESTROY) {
    closed = true;
    return 0;
  }
  return DefWindowProcA(w, m, a, b);
}
static void messages() {
  MSG m;
  while (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE)) {
    if (m.message == WM_QUIT)
      closed = true;
    TranslateMessage(&m);
    DispatchMessageA(&m);
  }
  if (closed)
    throw Failure("window closed before completion",
                  HRESULT_FROM_WIN32(ERROR_CANCELLED));
}
static std::string compact(const std::string &s) {
  std::string r;
  bool quoted = false, escape = false;
  for (char c : s) {
    if (quoted) {
      r += c;
      if (escape)
        escape = false;
      else if (c == '\\')
        escape = true;
      else if (c == '"')
        quoted = false;
    } else if (c == '"') {
      quoted = true;
      r += c;
    } else if (!std::isspace((unsigned char)c))
      r += c;
  }
  return r;
}
// Barrier files are exact three-field JSON objects in any key order, no extras.
static bool barrierMatches(const std::string &s, const Options &o) {
  std::string c = compact(s);
  std::string a = "\"schema\":1", b = "\"runId\":" + esc(o.runId),
              d = "\"pid\":" + std::to_string(GetCurrentProcessId());
  std::vector<std::string> p = {a, b, d};
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      if (i != j) {
        int k = 3 - i - j;
        if (c == "{" + p[i] + "," + p[j] + "," + p[k] + "}")
          return true;
      }
  return false;
}
static void barrier(const Options &o, const char *name) {
  double deadline = clocksec() + 30;
  for (;;) {
    messages();
    beat();
    std::ifstream f(path(o, name), std::ios::binary);
    if (f) {
      f.seekg(0, std::ios::end);
      auto size = f.tellg();
      if (size < 0 || size > 1024)
        throw Failure("barrier exceeds bounded size");
      f.seekg(0);
      std::string s((std::istreambuf_iterator<char>(f)), {});
      if (!barrierMatches(s, o))
        throw Failure(std::string("misbound/malformed barrier ") + name);
      return;
    }
    if (clocksec() > deadline)
      throw Failure(std::string("barrier timeout ") + name,
                    HRESULT_FROM_WIN32(WAIT_TIMEOUT));
    Sleep(20);
  }
}
static std::string modules() {
  HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                      GetCurrentProcessId());
  if (h == INVALID_HANDLE_VALUE)
    throw Failure("module snapshot", HRESULT_FROM_WIN32(GetLastError()));
  MODULEENTRY32 e{};
  e.dwSize = sizeof e;
  std::string s = "[";
  if (!Module32First(h, &e)) {
    CloseHandle(h);
    throw Failure("module enumeration");
  }
  do {
    if (s.size() > 1)
      s += ',';
    s += "{\"name\":" + esc(e.szModule) + ",\"path\":" + esc(e.szExePath) + "}";
  } while (Module32Next(h, &e));
  DWORD err = GetLastError();
  CloseHandle(h);
  if (err != ERROR_NO_MORE_FILES)
    throw Failure("module enumeration incomplete", HRESULT_FROM_WIN32(err));
  return s + "]";
}
template <class T> struct Com {
  T *p = nullptr;
  ~Com() { reset(); }
  void reset() {
    if (p) {
      p->Release();
      p = nullptr;
    }
  }
  T **out() { return &p; }
  T *operator->() { return p; }
  operator T *() { return p; }
  Com() = default;
  Com(const Com &) = delete;
};
static const char *shader = R"HLSL(
cbuffer Constants:register(b0){float time;float aspect;uint mode;uint objectIndex;};
struct O{float4 pos:SV_Position;float3 col:COLOR;float2 uv:TEXCOORD;};
O vs_main(float3 pos:POS,uint instance:SV_InstanceID){O o;float c=cos(time*.3),s=sin(time*.3);float3 p=float3(pos.x*c+pos.z*s,pos.y,pos.z*c-pos.x*s);float scale=.7;float2 off=0;if(mode==1||mode==2){uint id=mode==1?objectIndex:instance;scale=.025;off=float2((id%32+.5)/32*2-1,(id/32+.5)/32*2-1)*.94;}p*=scale;o.pos=float4((p.x+off.x*aspect)/aspect,p.y+off.y,p.z*.1+.5,1);o.col=pos+.5;o.uv=pos.xy+.5;return o;}
Texture2D tex:register(t0);SamplerState samp:register(s0);
float4 ps_main(O i):SV_Target{return float4(abs(i.col),1);}
float4 ps_texture(O i):SV_Target{return tex.Sample(samp,i.uv)*float4(.4+abs(i.col),1);}
O vs_full(uint id:SV_VertexID){O o;o.uv=float2((id<<1)&2,id&2);o.pos=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);o.col=0;return o;}
float4 ps_heavy(O i):SV_Target{float2 uv=(i.uv*2-1)*float2(aspect,1);float3 ro=float3(0,0,-3.6),rd=normalize(float3(uv,1.5));float t=0,light=0;[loop]for(int j=0;j<96;j++){float3 p=ro+rd*t;float c=cos(time*.2),s=sin(time*.2);p.xz=float2(c*p.x-s*p.z,s*p.x+c*p.z);float d=length(p)-.9;d+=sin(p.x*9+time)*sin(p.y*9)*sin(p.z*9)*.055;light+=exp(-abs(d)*32)*.012;t+=max(abs(d)*.4,.008);}return float4(light*.6,light*.85,light,1);}
)HLSL";
struct Constant {
  float time, aspect;
  unsigned mode, index;
};
struct Renderer {
  HWND window = nullptr;
  HMODULE compiler = nullptr;
  Com<ID3D11Device> dev;
  Com<ID3D11DeviceContext> ctx;
  Com<IDXGISwapChain> swap;
  Com<ID3D11RenderTargetView> rtv;
  Com<ID3D11DepthStencilView> dsv;
  Com<ID3D11VertexShader> vs, fullvs;
  Com<ID3D11PixelShader> ps, texps, heavyps;
  Com<ID3D11InputLayout> layout;
  Com<ID3D11Buffer> vb, ib, cb, upload;
  Com<ID3D11RasterizerState> rs;
  Com<ID3D11DepthStencilState> depth;
  Com<ID3D11Texture2D> texture;
  Com<ID3D11ShaderResourceView> srv;
  Com<ID3D11SamplerState> sampler;
  D3D_FEATURE_LEVEL feature{};
  std::string geometry = "null";
  std::vector<float> dynamic;
  ~Renderer() {
    if (ctx.p) {
      ctx->ClearState();
      ctx->Flush();
    }
    sampler.reset();
    srv.reset();
    texture.reset();
    depth.reset();
    rs.reset();
    upload.reset();
    cb.reset();
    ib.reset();
    vb.reset();
    layout.reset();
    heavyps.reset();
    texps.reset();
    ps.reset();
    fullvs.reset();
    vs.reset();
    dsv.reset();
    rtv.reset();
    swap.reset();
    ctx.reset();
    dev.reset();
    if (window)
      DestroyWindow(window);
    if (compiler)
      FreeLibrary(compiler);
  }
  void init(const Options &o) {
    SetProcessDPIAware();
    WNDCLASSA wc{};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "FiniteD3D11Benchmark";
    if (!RegisterClassA(&wc))
      throw Failure("RegisterClass", HRESULT_FROM_WIN32(GetLastError()));
    DWORD style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r = {0, 0, o.windowWidth, o.windowHeight};
    if (!AdjustWindowRect(&r, style, FALSE))
      throw Failure("AdjustWindowRect");
    window = CreateWindowA(
        wc.lpszClassName, ("D3D11 benchmark: " + o.scene).c_str(),
        style | WS_VISIBLE, 0, 0, r.right - r.left, r.bottom - r.top, nullptr,
        nullptr, wc.hInstance, nullptr);
    if (!window)
      throw Failure("CreateWindow", HRESULT_FROM_WIN32(GetLastError()));
    RECT actual;
    if (!GetClientRect(window, &actual) || actual.right != o.windowWidth ||
        actual.bottom != o.windowHeight)
      throw Failure("actual client size mismatch");
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferDesc.Width = o.width;
    sd.BufferDesc.Height = o.height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.OutputWindow = window;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL want = D3D_FEATURE_LEVEL_11_0;
    check(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE,
                                        nullptr, 0, &want, 1, D3D11_SDK_VERSION,
                                        &sd, swap.out(), dev.out(), &feature,
                                        ctx.out()),
          "D3D11CreateDeviceAndSwapChain");
    Com<ID3D11Texture2D> back;
    check(swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)back.out()),
          "GetBuffer");
    D3D11_TEXTURE2D_DESC backDesc{};
    back->GetDesc(&backDesc);
    if (backDesc.Width != UINT(o.width) || backDesc.Height != UINT(o.height) ||
        backDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM)
      throw Failure("actual backbuffer dimensions or format mismatch");
    geometry = "{\"renderWidth\":" + std::to_string(backDesc.Width) +
               ",\"renderHeight\":" + std::to_string(backDesc.Height) +
               ",\"windowWidth\":" + std::to_string(actual.right) +
               ",\"windowHeight\":" + std::to_string(actual.bottom) + "}";
    check(dev->CreateRenderTargetView(back, nullptr, rtv.out()), "CreateRTV");
    D3D11_TEXTURE2D_DESC dd{};
    dd.Width = o.width;
    dd.Height = o.height;
    dd.MipLevels = 1;
    dd.ArraySize = 1;
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.SampleDesc.Count = 1;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    Com<ID3D11Texture2D> dt;
    check(dev->CreateTexture2D(&dd, nullptr, dt.out()), "CreateDepthTexture");
    check(dev->CreateDepthStencilView(dt, nullptr, dsv.out()), "CreateDSV");
    compiler = LoadLibraryA("d3dcompiler_47.dll");
    if (!compiler)
      throw Failure("d3dcompiler_47.dll required",
                    HRESULT_FROM_WIN32(GetLastError()));
    auto compile = (pD3DCompile)GetProcAddress(compiler, "D3DCompile");
    if (!compile)
      throw Failure("D3DCompile export missing");
    auto blob = [&](const char *entry, const char *target, Com<ID3DBlob> &b) {
      Com<ID3DBlob> errors;
      HRESULT hr =
          compile(shader, strlen(shader), "embedded-procedural.hlsl", nullptr,
                  nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                  b.out(), errors.out());
      if (FAILED(hr))
        throw Failure(std::string("compile ") + entry + ": " +
                          (errors.p ? (char *)errors->GetBufferPointer() : ""),
                      hr);
    };
    Com<ID3DBlob> b;
    blob("vs_main", "vs_5_0", b);
    check(dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(),
                                  nullptr, vs.out()),
          "CreateVS");
    D3D11_INPUT_ELEMENT_DESC ie = {"POS", 0, DXGI_FORMAT_R32G32B32_FLOAT,
                                   0,     0, D3D11_INPUT_PER_VERTEX_DATA,
                                   0};
    check(dev->CreateInputLayout(&ie, 1, b->GetBufferPointer(),
                                 b->GetBufferSize(), layout.out()),
          "CreateInputLayout");
    {
      Com<ID3DBlob> x;
      blob("vs_full", "vs_5_0", x);
      check(dev->CreateVertexShader(x->GetBufferPointer(), x->GetBufferSize(),
                                    nullptr, fullvs.out()),
            "CreateFullVS");
    }
    auto pixel = [&](const char *name, Com<ID3D11PixelShader> &out) {
      Com<ID3DBlob> x;
      blob(name, "ps_5_0", x);
      check(dev->CreatePixelShader(x->GetBufferPointer(), x->GetBufferSize(),
                                   nullptr, out.out()),
            "CreatePS");
    };
    pixel("ps_main", ps);
    pixel("ps_texture", texps);
    pixel("ps_heavy", heavyps);
    const float vertices[] = {-.5f, -.5f, -.5f, -.5f, -.5f, .5f,  -.5f, .5f,
                              -.5f, -.5f, .5f,  .5f,  .5f,  -.5f, -.5f, .5f,
                              -.5f, .5f,  .5f,  .5f,  -.5f, .5f,  .5f,  .5f};
    const unsigned short indices[] = {0, 6, 4, 0, 2, 6, 0, 3, 2, 0, 1, 3,
                                      2, 7, 6, 2, 3, 7, 4, 6, 7, 4, 7, 5,
                                      0, 4, 5, 0, 5, 1, 1, 5, 7, 1, 7, 3};
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof vertices;
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = vertices;
    check(dev->CreateBuffer(&bd, &data, vb.out()), "CreateVB");
    bd.ByteWidth = sizeof indices;
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    data.pSysMem = indices;
    check(dev->CreateBuffer(&bd, &data, ib.out()), "CreateIB");
    bd.ByteWidth = sizeof(Constant);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    check(dev->CreateBuffer(&bd, nullptr, cb.out()), "CreateCB");
    dynamic.resize(1024 * 36 * 3);
    bd.ByteWidth = UINT(dynamic.size() * sizeof(float));
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    check(dev->CreateBuffer(&bd, nullptr, upload.out()), "CreateUploadBuffer");
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    check(dev->CreateRasterizerState(&rd, rs.out()), "CreateRasterizer");
    D3D11_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D11_COMPARISON_LESS;
    check(dev->CreateDepthStencilState(&ds, depth.out()), "CreateDepthState");
    std::vector<unsigned> pixels(256 * 256);
    for (unsigned y = 0; y < 256; y++)
      for (unsigned x = 0; x < 256; x++)
        pixels[y * 256 + x] = ((x / 16 + y / 16) & 1) ? 0xff40c0ff : 0xffe03060;
    dd.Width = dd.Height = 256;
    dd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    dd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    dd.Usage = D3D11_USAGE_IMMUTABLE;
    data.pSysMem = pixels.data();
    data.SysMemPitch = 256 * 4;
    check(dev->CreateTexture2D(&dd, &data, texture.out()), "CreateTexture");
    check(dev->CreateShaderResourceView(texture, nullptr, srv.out()),
          "CreateSRV");
    D3D11_SAMPLER_DESC ss{};
    ss.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    ss.AddressU = ss.AddressV = ss.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    ss.MaxLOD = D3D11_FLOAT32_MAX;
    check(dev->CreateSamplerState(&ss, sampler.out()), "CreateSampler");
  }
  void constants(const Options &o, unsigned frame, unsigned mode,
                 unsigned index) {
    D3D11_MAPPED_SUBRESOURCE m{};
    check(ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m), "MapCB");
    Constant c = {float(frame % 3600) / 60.0f, float(o.width) / o.height, mode,
                  index};
    memcpy(m.pData, &c, sizeof c);
    ctx->Unmap(cb, 0);
  }
  // Probe-only boundary, outside all measured frames. D3D11 EVENT proves GPU
  // ordering, but DXMT GetData observes a GPU event before CompletionThread may
  // publish trace 23/24. The subsequent staging READ also waits CPU coherence:
  // CopyResource/CopyBuffer -> useCopyDestination(CurrentSeqId), tryMap(READ)
  // -> Flush/WaitCPUFence; CompletionThread records 23/24 before
  // cpu_coherent.signal. This is specific to the inspected DXMT implementation;
  // the host must still bind its exact runtime and reject incomplete native
  // traces/display claims. EVENT polling has a five-second deadline.
  // Synchronous staging Map has no D3D11 timeout parameter; the own-PID
  // 15-second heartbeat guard and host's absolute watchdog bound a blocked Map,
  // not that five-second poll deadline.
  void drainProbe() {
    Com<ID3D11Query> completion;
    D3D11_QUERY_DESC desc{};
    desc.Query = D3D11_QUERY_EVENT;
    check(dev->CreateQuery(&desc, completion.out()), "CreateProbeEventQuery");
    check(dev->GetDeviceRemovedReason(), "device removed before probe drain");
    const double deadline = clocksec() + 5.0;
    ctx->End(completion);
    ctx->Flush();
    for (;;) {
      messages();
      beat();
      check(dev->GetDeviceRemovedReason(), "device removed during probe drain");
      if (clocksec() >= deadline)
        throw Failure("probe GPU event drain timeout",
                      HRESULT_FROM_WIN32(WAIT_TIMEOUT));
      BOOL done = FALSE;
      const HRESULT status = ctx->GetData(completion, &done, sizeof(done),
                                          D3D11_ASYNC_GETDATA_DONOTFLUSH);
      check(status, "GetData probe event");
      check(dev->GetDeviceRemovedReason(),
            "device removed after probe event poll");
      if (status == S_OK) {
        if (done != TRUE)
          throw Failure("probe event returned S_OK without completion");
        break;
      }
      if (status != S_FALSE)
        throw Failure("unexpected probe event status", status);
      Sleep(1);
    }
    // Copy a known value in a new ordered chunk, then wait for that chunk's
    // CPU-visible retirement instead of assuming the GPU event drained traces.
    Com<ID3D11Buffer> source, readback;
    const UINT sentinel = 0x64786d74u;
    D3D11_BUFFER_DESC sourceDesc{};
    sourceDesc.ByteWidth = sizeof(sentinel);
    sourceDesc.Usage = D3D11_USAGE_DEFAULT;
    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = &sentinel;
    check(dev->CreateBuffer(&sourceDesc, &initial, source.out()),
          "CreateProbeReadbackSource");
    D3D11_BUFFER_DESC readbackDesc = sourceDesc;
    readbackDesc.Usage = D3D11_USAGE_STAGING;
    readbackDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    check(dev->CreateBuffer(&readbackDesc, nullptr, readback.out()),
          "CreateProbeReadbackStaging");
    ctx->CopyResource(readback, source);
    check(dev->GetDeviceRemovedReason(),
          "device removed before probe readback");
    messages();
    beat();
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(ctx->Map(readback, 0, D3D11_MAP_READ, 0, &mapped),
          "MapProbeReadbackForCpuRetirement");
    UINT observed = 0;
    const bool validData = mapped.pData != nullptr;
    if (validData)
      memcpy(&observed, mapped.pData, sizeof(observed));
    ctx->Unmap(readback, 0);
    check(dev->GetDeviceRemovedReason(), "device removed after probe readback");
    if (!validData || observed != sentinel)
      throw Failure("probe readback sentinel mismatch");
    beat();
  }
  // Optional correctness capture: unchanged shader/frame path, before Present,
  // warmup, readiness and both trace generations. Never part of CPU samples.
  std::string captureImage(const Options &o) {
    constexpr unsigned frameId = 120;
    frame(o, frameId, false, true);
    Com<ID3D11Texture2D> back, staging;
    check(swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)back.out()),
          "GetImageBackBuffer");
    D3D11_TEXTURE2D_DESC desc{};
    back->GetDesc(&desc);
    if (desc.Width != UINT(o.width) || desc.Height != UINT(o.height) ||
        desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM ||
        desc.SampleDesc.Count != 1 || desc.SampleDesc.Quality != 0 ||
        desc.MipLevels != 1 || desc.ArraySize != 1)
      throw Failure("image backbuffer geometry/format mismatch");
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    check(dev->CreateTexture2D(&desc, nullptr, staging.out()), "CreateImageStaging");
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    ctx->CopyResource(staging, back);
    Com<ID3D11Query> completion;
    D3D11_QUERY_DESC query{};
    query.Query = D3D11_QUERY_EVENT;
    check(dev->CreateQuery(&query, completion.out()), "CreateImageEvent");
    ctx->End(completion);
    ctx->Flush();
    const double deadline = clocksec() + 5.0;
    for (;;) {
      messages();
      beat();
      check(dev->GetDeviceRemovedReason(), "device removed during image wait");
      if (clocksec() >= deadline)
        throw Failure("image GPU event timeout", HRESULT_FROM_WIN32(WAIT_TIMEOUT));
      BOOL done = FALSE;
      HRESULT status = ctx->GetData(completion, &done, sizeof(done),
                                   D3D11_ASYNC_GETDATA_DONOTFLUSH);
      check(status, "GetData image event");
      if (status == S_OK) {
        if (done != TRUE)
          throw Failure("image event returned S_OK without completion");
        break;
      }
      if (status != S_FALSE)
        throw Failure("unexpected image event status", status);
      Sleep(1);
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    // READ Map has no timeout parameter; own-PID watchdog and host deadline
    // bound it, just as for drainProbe's existing staging readback.
    check(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &mapped), "MapImageStaging");
    struct Unmap {
      ID3D11DeviceContext *context;
      ID3D11Resource *resource;
      ~Unmap() { context->Unmap(resource, 0); }
    } unmap{ctx, staging};
    const size_t rowBytes = size_t(o.width) * 4;
    // Bound both address arithmetic and driver-reported staging padding.
    if (!mapped.pData || mapped.RowPitch < rowBytes ||
        mapped.RowPitch > 65536 || size_t(mapped.RowPitch) * o.height > 128u * 1024 * 1024)
      throw Failure("invalid image staging row pitch/data");
    std::string bytes;
    bytes.reserve(rowBytes * o.height);
    uint64_t fnv = UINT64_C(14695981039346656037);
    for (int y = 0; y < o.height; ++y) {
      const auto *row = static_cast<const unsigned char *>(mapped.pData) +
                        size_t(y) * mapped.RowPitch;
      bytes.append(reinterpret_cast<const char *>(row), rowBytes);
      for (size_t x = 0; x < rowBytes; ++x)
        fnv = (fnv ^ row[x]) * UINT64_C(1099511628211);
      beat();
    }
    check(dev->GetDeviceRemovedReason(), "device removed after image readback");
    writeAtomic(o, "shader-image.rgba", bytes);
    std::ostringstream hash;
    hash << std::hex << std::setfill('0') << std::setw(16) << fnv;
    return "{\"schema\":1,\"artifact\":\"shader-image.rgba\",\"scene\":" + esc(o.scene) +
           ",\"frameId\":120,\"width\":" + std::to_string(o.width) +
           ",\"height\":" + std::to_string(o.height) +
           ",\"format\":\"RGBA8_UNORM\",\"rows\":" + std::to_string(o.height) +
           ",\"rowBytes\":" + std::to_string(rowBytes) +
           ",\"byteCount\":" + std::to_string(bytes.size()) +
           ",\"fnv1a64\":" + esc(hash.str()) +
           ",\"gpuEventComplete\":true,\"cpuReadbackComplete\":true}";
  }
  double frame(const Options &o, unsigned n, bool forcePresent = false,
               bool withoutPresent = false) {
    messages();
    RECT rect;
    if (!GetClientRect(window, &rect) || rect.right != o.windowWidth ||
        rect.bottom != o.windowHeight)
      throw Failure("client size changed during benchmark");
    if (IsIconic(window))
      throw Failure("window minimized during benchmark");
    const float clear[] = {.06f, .09f, .14f, 1};
    ctx->ClearRenderTargetView(rtv, clear);
    ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1, 0);
    ID3D11RenderTargetView *r = rtv;
    ctx->OMSetRenderTargets(1, &r, dsv);
    D3D11_VIEWPORT vp = {0, 0, float(o.width), float(o.height), 0, 1};
    ctx->RSSetViewports(1, &vp);
    ctx->RSSetState(rs);
    ctx->OMSetDepthStencilState(depth, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetInputLayout(layout);
    ctx->VSSetShader(vs, nullptr, 0);
    ctx->PSSetShader(ps, nullptr, 0);
    ID3D11Buffer *c = cb;
    ctx->VSSetConstantBuffers(0, 1, &c);
    ctx->PSSetConstantBuffers(0, 1, &c);
    UINT stride = 12, offset = 0;
    ID3D11Buffer *v = vb;
    ctx->IASetVertexBuffers(0, 1, &v, &stride, &offset);
    ctx->IASetIndexBuffer(ib, DXGI_FORMAT_R16_UINT, 0);
    constants(o, n, 0, 0);
    if (o.scene == "pixelheavy") {
      ctx->IASetInputLayout(nullptr);
      ctx->VSSetShader(fullvs, nullptr, 0);
      ctx->PSSetShader(heavyps, nullptr, 0);
      ctx->Draw(3, 0);
    } else if (o.scene == "instanced") {
      constants(o, n, 2, 0);
      ctx->DrawIndexedInstanced(36, 1024, 0, 0, 0);
    } else if (o.scene == "drawstress") {
      for (unsigned i = 0; i < 1024; i++) {
        constants(o, n, 1, i);
        ctx->DrawIndexed(36, 0, 0);
      }
    } else if (o.scene == "dynamicupload") {
      float phase = float(n % 3600) / 60;
      for (unsigned i = 0; i < 1024; i++) {
        float x = ((i % 32 + .5f) / 32 * 2 - 1) * .95f,
              y = ((i / 32 + .5f) / 32 * 2 - 1) * .95f;
        for (unsigned j = 0; j < 36; j++) {
          size_t at = (i * 36 + j) * 3;
          float a = float(j % 3) * 2.094395f;
          dynamic[at] = x + cosf(a + phase) * .025f;
          dynamic[at + 1] = y + sinf(a + phase) * .025f;
          dynamic[at + 2] = float(j / 3) * .001f;
        }
      }
      D3D11_MAPPED_SUBRESOURCE m{};
      check(ctx->Map(upload, 0, D3D11_MAP_WRITE_DISCARD, 0, &m), "MapUpload");
      memcpy(m.pData, dynamic.data(), dynamic.size() * sizeof(float));
      ctx->Unmap(upload, 0);
      v = upload;
      ctx->IASetVertexBuffers(0, 1, &v, &stride, &offset);
      ctx->Draw(1024 * 36, 0);
    } else {
      if (o.scene == "textured") {
        ID3D11ShaderResourceView *s = srv;
        ID3D11SamplerState *p = sampler;
        ctx->PSSetShaderResources(0, 1, &s);
        ctx->PSSetSamplers(0, 1, &p);
        ctx->PSSetShader(texps, nullptr, 0);
      }
      ctx->DrawIndexed(36, 0, 0);
    }
    double presentMs = 0;
    if (!withoutPresent && (forcePresent || presents(o, n))) {
      double before = clocksec();
      HRESULT hr = swap->Present(o.vsync, 0);
      double after = clocksec();
      if (hr != S_OK)
        throw Failure("Present failed or did not present", hr);
      presentMs = (after - before) * 1000;
    } else {
      // Submit this iteration's unchanged rendering even without a drawable.
      // Flush does not prove GPU completion; cpuFrameMs includes its wall time.
      ctx->Flush();
    }
    check(dev->GetDeviceRemovedReason(), "device removed");
    beat();
    return presentMs;
  }
};
int main(int argc, char **argv) {
  Options o;
  HANDLE thread = nullptr;
  unsigned frames = 0;
  unsigned measuredPresentCalls = 0;
  double elapsed = 0;
  int code = 1;
  std::string error;
  std::string geometry = "null", imageProof;
  HRESULT hr = S_OK;
  FILE *csv = nullptr;
  FILE *events = nullptr;
  bool parsed = false, owned = false;
  HANDLE owner = INVALID_HANDLE_VALUE;
  LARGE_INTEGER q;
  QueryPerformanceFrequency(&q);
  freq = double(q.QuadPart);
  try {
    o = parse(argc, argv);
    parsed = true;
    if (!CreateDirectoryA(o.output.c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS)
      throw Failure("output directory unavailable");
    for (const char *name :
         {"results.json", "ready.json", "perframe.csv", "phases.jsonl",
          "probe-done.json", "watchdog.json", "shader-image.rgba",
          "shader-image.rgba.tmp"})
      if (GetFileAttributesA(path(o, name).c_str()) != INVALID_FILE_ATTRIBUTES)
        throw Failure("output directory already has run artifacts");
    owner = CreateFileA(path(o, ".guest-owner").c_str(), GENERIC_WRITE, 0,
                        nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (owner == INVALID_HANDLE_VALUE)
      throw Failure("output ownership marker already exists or unavailable");
    owned = true;
    watchdogPath = path(o, "watchdog.json");
    stopEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent)
      throw Failure("CreateEvent watchdog");
    beat();
    thread = CreateThread(nullptr, 0, guard, nullptr, 0, nullptr);
    if (!thread)
      throw Failure("CreateThread watchdog");
    events = fopen(path(o, "phases.jsonl").c_str(), "wb");
    if (!events)
      throw Failure("open phases");
    auto phase = [&](const char *s) {
      if (fprintf(events, "{%s,\"phase\":%s,\"qpcSeconds\":%.6f}\n",
                  identity(o).c_str(), esc(s).c_str(), clocksec()) < 0 ||
          fflush(events))
        throw Failure("write phases");
    };
    phase("initializing");
    {
      Renderer r;
      r.init(o);
      geometry = r.geometry;
      if (o.imageProof) {
        phase("image-proof");
        imageProof = ",\"imageProof\":" + r.captureImage(o);
      }
      phase("warmup");
      double warmstart = clocksec();
      unsigned warmframes = 0;
      do {
        r.frame(o, warmframes++);
      } while (clocksec() - warmstart < o.warmup);
      writeAtomic(o, "ready.json",
                  "{" + identity(o) + ",\"scene\":" + esc(o.scene) +
                      ",\"width\":" + std::to_string(o.width) +
                      ",\"height\":" + std::to_string(o.height) +
                      ",\"geometry\":" + geometry + imageProof +
                      ",\"presentEvery\":" + std::to_string(o.presentEvery) +
                      ",\"metricScope\":" + esc(metricScope) +
                      ",\"featureLevel\":" + std::to_string(r.feature) +
                      ",\"modules\":" + modules() + "}\n");
      phase("ready");
      if (o.barrier) {
        barrier(o, "probe.json");
        phase("probe");
        for (unsigned i = 0; i < 3; i++)
          r.frame(o, i, true);
        phase("probe-drain");
        r.drainProbe();
        writeAtomic(
            o, "probe-done.json",
            "{" + identity(o) +
                ",\"gpuEventComplete\":true,\"cpuReadbackComplete\":true,"
                "\"readbackValue\":1685613940}\n");
        barrier(o, "go.json");
      }
      phase("measuring");
      csv = fopen(path(o, "perframe.csv").c_str(), "wb");
      if (!csv)
        throw Failure("open perframe");
      if (fputs("frame,cpuFrameMs,presentCallMs\n", csv) < 0)
        throw Failure("write CSV header");
      double start = clocksec();
      do {
        double a = clocksec();
        double present = r.frame(o, frames);
        double total = (clocksec() - a) * 1000;
        if (presents(o, frames))
          measuredPresentCalls++;
        if (fprintf(csv, "%u,%.9f,%.9f\n", frames, total, present) < 0)
          throw Failure("write CSV row");
        frames++;
        elapsed = clocksec() - start;
        if (frames >= 1000000)
          throw Failure("frame sample cap reached");
      } while (elapsed < o.duration && (!o.frames || frames < o.frames));
      if (fflush(csv) || ferror(csv))
        throw Failure("flush CSV");
      int closeStatus = fclose(csv);
      csv = nullptr;
      if (closeStatus)
        throw Failure("close CSV");
      phase("teardown");
    }
    phase("complete");
    code = 0;
  } catch (const Failure &e) {
    error = e.what();
    hr = e.hr;
    fprintf(stderr, "benchmark failed: %s (0x%08lx)\n", e.what(),
            (unsigned long)(unsigned)hr);
  } catch (const std::exception &e) {
    error = e.what();
    hr = E_FAIL;
    fprintf(stderr, "benchmark failed: %s\n", e.what());
  }
  if (csv)
    fclose(csv);
  if (events)
    fclose(events);
  if (parsed && owned) {
    try {
      std::ostringstream s;
      s << "{" << identity(o) << ",\"scene\":" << esc(o.scene)
        << ",\"status\":" << esc(code ? "failed" : "complete")
        << ",\"frames\":" << frames << ",\"warmupSeconds\":" << o.warmup
        << ",\"durationSeconds\":" << elapsed
        << ",\"requestedDurationSeconds\":" << o.duration
        << ",\"width\":" << o.width << ",\"height\":" << o.height
        << ",\"geometry\":" << geometry << imageProof
        << ",\"vsync\":" << o.vsync
        << ",\"presentEvery\":" << o.presentEvery
        << ",\"measuredPresentCalls\":" << measuredPresentCalls
        << ",\"metricScope\":" << esc(metricScope)
        << ",\"sceneClock\":\"frameIndex modulo 3600 / 60; reset after warmup "
           "and probe\",\"seed\":0,\"error\":"
        << esc(error) << ",\"hresult\":" << (unsigned)hr << "}\n";
      writeAtomic(o, "results.json", s.str());
    } catch (const std::exception &e) {
      fprintf(stderr, "result write failed: %s\n", e.what());
      code = 2;
    }
  }
  if (stopEvent)
    SetEvent(stopEvent);
  if (thread) {
    WaitForSingleObject(thread, 2000);
    CloseHandle(thread);
  }
  if (stopEvent)
    CloseHandle(stopEvent);
  if (owner != INVALID_HANDLE_VALUE)
    CloseHandle(owner);
  return code;
}
