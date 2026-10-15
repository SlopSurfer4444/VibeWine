#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// This executable has no game assets, save access, child launch, or exclusive mode API.
struct Failure { int code; };
template<class T> struct Ref {
  T *p = NULL;
  ~Ref() { reset(); }
  void reset() { if (p) { p->Release(); p = NULL; } }
  T **out() { reset(); return &p; }
  T *operator->() const { return p; }
};
struct Options { bool borderless=false, threaded=false, closeQueued=false, controlled=false, corrupt=false; unsigned hold=120; };
static Options opt;
static HWND window;
static HANDLE destroyedEvent, closeCompletedEvent;
static volatile LONG destroyed=0, quitSeen=0, closeRequested=0, closeReturned=0, closeReturnResult=-1, closeReturnAlive=1;
static DWORD guiTid;
static int renderResult=99;
static unsigned frames=0;
static void require(bool value, const char *name, int code=10) {
  printf("CHECK %s valid=%d\n", name, value ? 1 : 0);
  if (!value) throw Failure{code};
}
static void step(HRESULT hr, const char *name) {
  printf("STEP %s hr=0x%08lx\n",name,(unsigned long)hr);
  if (FAILED(hr)) throw Failure{11};
}
static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM a, LPARAM b) {
  if (m==WM_CLOSE) {
    printf("EVENT WM_CLOSE tid=%lu tick=%lu\n",GetCurrentThreadId(),GetTickCount());
    SetLastError(ERROR_SUCCESS); BOOL result=DestroyWindow(w); DWORD error=result?ERROR_SUCCESS:GetLastError(); BOOL alive=IsWindow(w);
    InterlockedExchange(&closeReturnResult,result?1:0); InterlockedExchange(&closeReturnAlive,alive?1:0);
    InterlockedExchange(&closeReturned,1);
    printf("CLOSE_RETURN result=%d last_error=%lu alive=%d tid=%lu tick=%lu\n",result?1:0,error,alive?1:0,GetCurrentThreadId(),GetTickCount());
    SetEvent(closeCompletedEvent); // Completion means the owner's DestroyWindow call has returned.
    return 0;
  }
  if (m==WM_DESTROY) {
    printf("EVENT WM_DESTROY tid=%lu tick=%lu\n",GetCurrentThreadId(),GetTickCount());
    InterlockedExchange(&destroyed,1); SetEvent(destroyedEvent); PostQuitMessage(0); return 0;
  }
  if (m==WM_NCDESTROY) printf("EVENT WM_NCDESTROY tid=%lu tick=%lu\n",GetCurrentThreadId(),GetTickCount());
  return DefWindowProcW(w,m,a,b);
}
static void pump() {
  MSG msg;
  while (PeekMessageW(&msg,NULL,0,0,PM_REMOVE)) {
    if (msg.message==WM_QUIT) { InterlockedExchange(&quitSeen,1); continue; }
    TranslateMessage(&msg); DispatchMessageW(&msg);
  }
}
static void pauseGui(unsigned ms) {
  DWORD start=GetTickCount();
  do { if (GetCurrentThreadId()==guiTid) pump(); Sleep(2); } while (GetTickCount()-start<ms);
}
static void module(const wchar_t *name) {
  wchar_t path[32768]; HMODULE m=GetModuleHandleW(name);
  require(m && GetModuleFileNameW(m,path,32768),"module_loaded");
  printf("MODULE %ls %ls\n",name,path);
}
static void geometry(IDXGISwapChain *swap) {
  BOOL full=TRUE; Ref<IDXGIOutput> output;
  step(swap->GetFullscreenState(&full,output.out()),"fullscreen_state");
  DXGI_SWAP_CHAIN_DESC desc={}; step(swap->GetDesc(&desc),"swap_desc");
  require(!full && desc.Windowed,"windowed_no_exclusive");
  RECT client={},actual={}; require(GetClientRect(window,&client) && GetWindowRect(window,&actual),"geometry_queries");
  printf("GEOMETRY client=%ldx%ld window=%ld,%ld,%ld,%ld owner=%lu caller=%lu\n",client.right,client.bottom,
    actual.left,actual.top,actual.right,actual.bottom,GetWindowThreadProcessId(window,NULL),GetCurrentThreadId());
  if (opt.borderless) {
    MONITORINFO mi={}; mi.cbSize=sizeof(mi);
    require(GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&mi),"monitor_query");
    LONG_PTR style=GetWindowLongPtrW(window,GWL_STYLE),ex=GetWindowLongPtrW(window,GWL_EXSTYLE);
    require(EqualRect(&actual,&mi.rcMonitor) && client.right==actual.right-actual.left && client.bottom==actual.bottom-actual.top &&
      (style&WS_POPUP) && !(style&WS_OVERLAPPEDWINDOW) && !(ex&WS_EX_TOPMOST),"native_borderless_geometry");
  }
}
static void dimensions(IDXGISwapChain *swap, UINT width, UINT height, const char *phase) {
  Ref<ID3D11Texture2D> buffer; step(swap->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)buffer.out()),"get_actual_buffer");
  D3D11_TEXTURE2D_DESC td={}; buffer->GetDesc(&td);
  DXGI_SWAP_CHAIN_DESC sd={}; step(swap->GetDesc(&sd),"get_public_desc");
  printf("DIMENSIONS phase=%s expected=%ux%u texture=%ux%u public=%ux%u format=%u samples=%u\n",
    phase,width,height,td.Width,td.Height,sd.BufferDesc.Width,sd.BufferDesc.Height,(unsigned)td.Format,td.SampleDesc.Count);
  require(td.Width==width && td.Height==height && sd.BufferDesc.Width==width && sd.BufferDesc.Height==height &&
    td.Format==DXGI_FORMAT_R8G8B8A8_UNORM && td.SampleDesc.Count==1,"actual_and_public_dimensions");
}
static const char shader[]=R"(
cbuffer Size : register(b0) { float width; float height; float2 unused; };
float4 vs(uint id:SV_VertexID):SV_Position {
  return float4(id==2 ? 3.0 : -1.0, id==1 ? 3.0 : -1.0, 0.0, 1.0);
}
float4 ps(float4 pos:SV_Position):SV_Target {
  float2 uv=pos.xy/float2(width,height);
  if (uv.x>0.40 && uv.x<0.60 && uv.y>0.40 && uv.y<0.60) return float4(1,0,1,1);
  if (uv.x<0.5) return uv.y<0.5 ? float4(1,0,0,1) : float4(0,0,1,1);
  return uv.y<0.5 ? float4(0,1,0,1) : float4(1,1,0,1);
}
)";
struct Pipeline {
  Ref<ID3D11VertexShader> vs;
  Ref<ID3D11PixelShader> ps;
  Ref<ID3D11Buffer> size;
  Ref<ID3D11RasterizerState> raster;
  explicit Pipeline(ID3D11Device *device) {
    Ref<ID3DBlob> vb,pb,err;
    HRESULT hr=D3DCompile(shader,sizeof(shader)-1,"procedural-fixture",NULL,NULL,"vs","vs_4_0",D3DCOMPILE_ENABLE_STRICTNESS,0,vb.out(),err.out());
    if (FAILED(hr) && err.p) printf("SHADER_ERROR %.*s\n",(int)err->GetBufferSize(),(char*)err->GetBufferPointer());
    step(hr,"compile_vs");
    hr=D3DCompile(shader,sizeof(shader)-1,"procedural-fixture",NULL,NULL,"ps","ps_4_0",D3DCOMPILE_ENABLE_STRICTNESS,0,pb.out(),err.out());
    if (FAILED(hr) && err.p) printf("SHADER_ERROR %.*s\n",(int)err->GetBufferSize(),(char*)err->GetBufferPointer());
    step(hr,"compile_ps");
    step(device->CreateVertexShader(vb->GetBufferPointer(),vb->GetBufferSize(),NULL,vs.out()),"create_vs");
    step(device->CreatePixelShader(pb->GetBufferPointer(),pb->GetBufferSize(),NULL,ps.out()),"create_ps");
    D3D11_BUFFER_DESC bd={}; bd.ByteWidth=16; bd.Usage=D3D11_USAGE_DEFAULT; bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    step(device->CreateBuffer(&bd,NULL,size.out()),"size_cbuffer");
    D3D11_RASTERIZER_DESC rd={}; rd.FillMode=D3D11_FILL_SOLID; rd.CullMode=D3D11_CULL_NONE; rd.DepthClipEnable=TRUE;
    step(device->CreateRasterizerState(&rd,raster.out()),"raster_no_cull");
  }
  void draw(ID3D11DeviceContext *ctx,ID3D11RenderTargetView *rtv,UINT w,UINT h) {
    float values[]={float(w),float(h),0,0}; ctx->UpdateSubresource(size.p,0,NULL,values,0,0);
    D3D11_VIEWPORT vp={0,0,float(w),float(h),0,1}; ctx->RSSetViewports(1,&vp); ctx->RSSetState(raster.p);
    ctx->OMSetRenderTargets(1,&rtv,NULL); ctx->IASetInputLayout(NULL); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs.p,NULL,0); ctx->PSSetShader(ps.p,NULL,0); ctx->PSSetConstantBuffers(0,1,&size.p);
    const FLOAT poison[]={0.125f,0.25f,0.5f,1}; ctx->ClearRenderTargetView(rtv,poison); ctx->Draw(3,0);
  }
};
static void readback(ID3D11Device *device,ID3D11DeviceContext *ctx,ID3D11Texture2D *buffer,const char *phase) {
  D3D11_TEXTURE2D_DESC td={}; buffer->GetDesc(&td);
  td.Usage=D3D11_USAGE_STAGING; td.BindFlags=0; td.CPUAccessFlags=D3D11_CPU_ACCESS_READ; td.MiscFlags=0;
  Ref<ID3D11Texture2D> staging; step(device->CreateTexture2D(&td,NULL,staging.out()),"create_staging");
  ctx->CopyResource(staging.p,buffer); D3D11_MAPPED_SUBRESOURCE map={}; step(ctx->Map(staging.p,0,D3D11_MAP_READ,0,&map),"map_gpu_pattern");
  // Independent fixed spatial golden: TL red, TR green, BL blue, BR yellow, center magenta.
  const UINT xs[]={0,td.Width-1,0,td.Width-1,td.Width/2,td.Width/4,3*td.Width/4,td.Width/4,3*td.Width/4};
  const UINT ys[]={0,0,td.Height-1,td.Height-1,td.Height/2,td.Height/4,td.Height/4,3*td.Height/4,3*td.Height/4};
  const unsigned char golden[9][4]={{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,0,255},{255,0,255,255},
    {255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,0,255}};
  bool valid=true;
  for (unsigned i=0;i<9;++i) {
    const unsigned char *p=(const unsigned char*)map.pData+ys[i]*map.RowPitch+4*xs[i]; bool match=true;
    for (unsigned c=0;c<4;++c) { int wanted=golden[i][c]; if (opt.corrupt && i==0 && c==0) wanted=0; match &= abs(int(p[c])-wanted)<=1; }
    printf("PIXEL phase=%s point=%u xy=%u,%u rgba=%u,%u,%u,%u valid=%d\n",phase,i,xs[i],ys[i],p[0],p[1],p[2],p[3],match?1:0);
    valid &= match;
  }
  ctx->Unmap(staging.p,0); require(valid,"gpu_spatial_pattern",24);
}
static void phase(ID3D11Device *device,ID3D11DeviceContext *ctx,IDXGISwapChain *swap,Pipeline &pipeline,UINT w,UINT h,const char *name) {
  printf("PHASE_BEGIN %s\n",name); dimensions(swap,w,h,name);
  Ref<ID3D11Texture2D> buffer; Ref<ID3D11RenderTargetView> rtv;
  step(swap->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)buffer.out()),"get_draw_buffer");
  step(device->CreateRenderTargetView(buffer.p,NULL,rtv.out()),"create_rtv");
  pipeline.draw(ctx,rtv.p,w,h); readback(device,ctx,buffer.p,name);
  DWORD start=GetTickCount(); unsigned count=0;
  do {
    require(!InterlockedCompareExchange(&destroyed,0,0),"window_alive");
    pipeline.draw(ctx,rtv.p,w,h); HRESULT hr=swap->Present(1,0); if (hr!=S_OK) { printf("PRESENT_NOT_VISIBLE_SUCCESS hr=0x%08lx\n",(unsigned long)hr); throw Failure{11}; }
    ++frames; ++count; pauseGui(2);
  } while (GetTickCount()-start<opt.hold);
  printf("PHASE_END %s presents=%u\n",name,count); geometry(swap);
  ctx->ClearState(); ctx->Flush();
}
static void resize(ID3D11DeviceContext *ctx,IDXGISwapChain *swap,UINT rw,UINT rh,UINT ew,UINT eh,const char *name) {
  ctx->ClearState(); step(swap->ResizeBuffers(2,rw,rh,DXGI_FORMAT_R8G8B8A8_UNORM,0),name); dimensions(swap,ew,eh,name);
}
static void targetObservation(const char *stage, UINT w, UINT h, RECT &client, RECT &outer) {
  require(GetClientRect(window,&client) && GetWindowRect(window,&outer),"target_geometry_queries");
  printf("TARGET_OBSERVATION stage=%s requested=%ux%u client=%ldx%ld window=%ld,%ld,%ld,%ld maxtrack=%dx%d virtual=%dx%d frame=%dx%d\n",
    stage,w,h,client.right,client.bottom,outer.left,outer.top,outer.right,outer.bottom,
    GetSystemMetrics(SM_CXMAXTRACK),GetSystemMetrics(SM_CYMAXTRACK),
    GetSystemMetrics(SM_CXVIRTUALSCREEN),GetSystemMetrics(SM_CYVIRTUALSCREEN),
    GetSystemMetrics(SM_CXFRAME),GetSystemMetrics(SM_CYFRAME));
}
static void target(IDXGISwapChain *swap,UINT w,UINT h) {
  RECT beforeClient={},beforeOuter={},afterClient={},afterOuter={};
  targetObservation("before",w,h,beforeClient,beforeOuter);
  if (!opt.borderless) {
    LONG requestedOuterW=LONG(w)+beforeOuter.right-beforeOuter.left-beforeClient.right;
    LONG requestedOuterH=LONG(h)+beforeOuter.bottom-beforeOuter.top-beforeClient.bottom;
    require(requestedOuterW<=GetSystemMetrics(SM_CXMAXTRACK) && requestedOuterH<=GetSystemMetrics(SM_CYMAXTRACK),"target_fits_default_maxtrack");
  }
  DXGI_MODE_DESC mode={}; mode.Width=w; mode.Height=h; mode.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
  HRESULT hr=swap->ResizeTarget(&mode); printf("RESIZE_TARGET requested=%ux%u hr=0x%08lx\n",w,h,(unsigned long)hr);
  targetObservation("after",w,h,afterClient,afterOuter);
  if (opt.borderless) {
    const bool matches=UINT(beforeClient.right)==w && UINT(beforeClient.bottom)==h;
    require(matches ? hr==S_OK : hr==DXGI_ERROR_UNSUPPORTED,"target_truthful_native_client_result");
    require(EqualRect(&beforeClient,&afterClient) && EqualRect(&beforeOuter,&afterOuter),"target_preserves_native_borderless_geometry");
  } else {
    step(hr,"resize_windowed_target");
    require(hr==S_OK && UINT(afterClient.right)==w && UINT(afterClient.bottom)==h,"target_observed_client_matches_request");
  }
}
static void closeWindow() {
  if (!InterlockedCompareExchange(&closeRequested,0,0)) {
    require(PostMessageW(window,WM_CLOSE,0,0),"post_normal_close"); InterlockedExchange(&closeRequested,1);
  }
  DWORD start=GetTickCount(),waitResult=WAIT_TIMEOUT,error=ERROR_SUCCESS;
  do {
    waitResult=WaitForSingleObject(closeCompletedEvent,0);
    if (waitResult==WAIT_OBJECT_0 || waitResult==WAIT_FAILED) break;
    if (GetTickCount()-start>=3000) break;
    pauseGui(2);
  } while (true);
  if (waitResult==WAIT_FAILED) error=GetLastError();
  DWORD elapsed=GetTickCount()-start; bool completed=waitResult==WAIT_OBJECT_0;
  LONG notified=InterlockedCompareExchange(&destroyed,0,0),returned=InterlockedCompareExchange(&closeReturned,0,0);
  LONG result=InterlockedCompareExchange(&closeReturnResult,0,0),returnAlive=InterlockedCompareExchange(&closeReturnAlive,0,0);
  bool alive=IsWindow(window),notification=WaitForSingleObject(destroyedEvent,0)==WAIT_OBJECT_0;
  bool timedOut=!completed || elapsed>3000;
  printf("CLOSE_WAIT elapsed_ms=%lu completion=%d destroy_event=%d destroyed=%ld returned=%ld result=%ld return_alive=%ld alive=%d timed_out=%d wait_failed=%d last_error=%lu tid=%lu\n",
    elapsed,completed?1:0,notification?1:0,notified,returned,result,returnAlive,alive?1:0,timedOut?1:0,waitResult==WAIT_FAILED?1:0,error,GetCurrentThreadId());
  require(waitResult!=WAIT_FAILED,"close_wait_handle_valid");
  require(!timedOut,"close_wait_deadline");
  require(completed && returned,"close_return_observed");
  require(result==1,"close_return_success");
  require(returnAlive==0 && !alive,"close_return_hwnd_invalid");
  require(notification && notified && !alive,"normal_destroy_observed");
}
static Ref<ID3D11Device> device;
static Ref<ID3D11DeviceContext> ctx;
static Ref<IDXGISwapChain> swap;
static Ref<IDXGIFactory> factory;
static void initialize() {
    DXGI_SWAP_CHAIN_DESC desc={}; desc.BufferDesc.Width=1920; desc.BufferDesc.Height=1080; desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count=1; desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount=2; desc.OutputWindow=window;
    desc.Windowed=TRUE; desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    step(D3D11CreateDeviceAndSwapChain(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,0,NULL,0,D3D11_SDK_VERSION,&desc,swap.out(),device.out(),NULL,ctx.out()),"create_hardware_windowed");
    module(L"d3d11.dll"); module(L"dxgi.dll"); module(L"winemetal.dll");
    step(swap->GetParent(__uuidof(IDXGIFactory),(void**)factory.out()),"get_factory");
    step(factory->MakeWindowAssociation(window,DXGI_MWA_NO_ALT_ENTER|DXGI_MWA_NO_WINDOW_CHANGES),"disable_alt_enter");
    factory.reset();
}
static void render() {
  try {
    printf("RENDER_THREAD gui=%lu render=%lu separate=%d\n",guiTid,GetCurrentThreadId(),opt.threaded?1:0);
    require((GetCurrentThreadId()!=guiTid)==opt.threaded,"thread_topology");
    Pipeline pipeline(device.p);
    phase(device.p,ctx.p,swap.p,pipeline,1920,1080,"low_initial");
    target(swap.p,1280,720);
    UINT targetHighW=opt.borderless?1920:1280,targetHighH=opt.borderless?1080:720;
    resize(ctx.p,swap.p,0,0,targetHighW,targetHighH,"target_high_then_zero");
    phase(device.p,ctx.p,swap.p,pipeline,targetHighW,targetHighH,"target_high_zero");
    resize(ctx.p,swap.p,2560,1440,2560,1440,"explicit_high"); phase(device.p,ctx.p,swap.p,pipeline,2560,1440,"high");
    target(swap.p,960,540);
    UINT targetLowW=opt.borderless?2560:960,targetLowH=opt.borderless?1440:540;
    resize(ctx.p,swap.p,0,0,targetLowW,targetLowH,"target_low_then_zero");
    phase(device.p,ctx.p,swap.p,pipeline,targetLowW,targetLowH,"target_low_zero");
    resize(ctx.p,swap.p,1920,1080,1920,1080,"explicit_low"); phase(device.p,ctx.p,swap.p,pipeline,1920,1080,"low_return");
    RECT client={}; require(GetClientRect(window,&client),"fallback_client_query");
    UINT zeroW=opt.borderless?1920:UINT(client.right),zeroH=opt.borderless?1080:UINT(client.bottom);
    resize(ctx.p,swap.p,0,0,zeroW,zeroH,"fallback_zero_both"); phase(device.p,ctx.p,swap.p,pipeline,zeroW,zeroH,"zero_both");
    resize(ctx.p,swap.p,0,720,zeroW,720,"fallback_zero_width"); phase(device.p,ctx.p,swap.p,pipeline,zeroW,720,"zero_width");
    resize(ctx.p,swap.p,1280,0,1280,opt.borderless?720:zeroH,"fallback_zero_height");
    UINT finalH=opt.borderless?720:zeroH; phase(device.p,ctx.p,swap.p,pipeline,1280,finalH,"zero_height");
    ctx->ClearState(); HRESULT bad=swap->ResizeBuffers(2,1280,finalH,(DXGI_FORMAT)0x7fffffff,0);
    printf("CONTROLLED_API_FAILURE hr=0x%08lx\n",(unsigned long)bad); require(FAILED(bad),"unsupported_resize_rejected");
    dimensions(swap.p,1280,finalH,"after_rejected_resize"); phase(device.p,ctx.p,swap.p,pipeline,1280,finalH,"recovery_after_rejection");
    if (opt.closeQueued) {
      Ref<ID3D11Texture2D> buffer; Ref<ID3D11RenderTargetView> rtv; Ref<ID3D11Query> query;
      step(swap->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)buffer.out()),"queued_get_buffer");
      step(device->CreateRenderTargetView(buffer.p,NULL,rtv.out()),"queued_rtv");
      D3D11_QUERY_DESC qd={D3D11_QUERY_EVENT,0}; step(device->CreateQuery(&qd,query.out()),"queued_query");
      for (unsigned i=0;i<8;++i) { pipeline.draw(ctx.p,rtv.p,1280,finalH); HRESULT phr=swap->Present(0,0); step(phr,"queued_present"); require(phr==S_OK,"queued_present_success"); ++frames; }
      ctx->End(query.p); BOOL completed=FALSE; HRESULT qhr=ctx->GetData(query.p,&completed,sizeof(completed),D3D11_ASYNC_GETDATA_DONOTFLUSH);
      printf("CLOSE_QUEUED submitted=8 query_hr=0x%08lx completed=%d explicit_wait_before_close=0\n",(unsigned long)qhr,completed);
      closeWindow(); // queued submissions are real; pending GPU completion is reported, never assumed.
      ctx->ClearState();
    } else { ctx->ClearState(); ctx->Flush(); }
    puts("RELEASE_BEGIN"); pipeline.vs.reset(); pipeline.ps.reset(); pipeline.size.reset(); pipeline.raster.reset();
    swap.reset(); ctx.reset(); device.reset(); puts("RELEASE_END");
    if (!opt.closeQueued) closeWindow();
    renderResult=opt.controlled?23:0;
    if (opt.controlled) puts("CONTROLLED_FAILURE after_gpu_and_cleanup exit=23");
  } catch (const Failure &f) {
    renderResult=f.code; printf("FIXTURE_FAILURE code=%d\n",f.code); puts("RELEASE_BEGIN_FAILURE");
    if (ctx.p) { ctx->ClearState(); ctx->Flush(); }
    swap.reset(); ctx.reset(); device.reset(); factory.reset(); puts("RELEASE_END_FAILURE");
  }
}
static DWORD WINAPI worker(void*) { render(); return DWORD(renderResult); }
int main(int argc,char **argv) {
  setvbuf(stdout,NULL,_IONBF,0); guiTid=GetCurrentThreadId();
  try {
    for (int i=1;i<argc;++i) {
      if (!strcmp(argv[i],"--borderless")) opt.borderless=true;
      else if (!strcmp(argv[i],"--threaded")) opt.threaded=true;
      else if (!strcmp(argv[i],"--close-queued")) opt.closeQueued=true;
      else if (!strcmp(argv[i],"--controlled-failure")) opt.controlled=true;
      else if (!strcmp(argv[i],"--corrupt-expectation")) opt.corrupt=true;
      else if (!strncmp(argv[i],"--hold-ms=",10)) { opt.hold=unsigned(atoi(argv[i]+10)); require(opt.hold<=5000,"bounded_hold"); }
      else require(false,"known_argument");
    }
    wchar_t own[32768]; require(GetModuleFileNameW(NULL,own,32768),"fixture_identity");
    const wchar_t *base=wcsrchr(own,L'\\'); base=base?base+1:own;
    bool ds3=!_wcsicmp(base,L"DarkSoulsIII.exe"); const char *env=getenv("DXMT_DS3_BORDERLESS");
    require(opt.borderless ? (ds3 && env && !strcmp(env,"1")) : (!ds3 && !env),"protected_ds3_suffix_and_explicit_opt_in");
    printf("FIXTURE wave=7 pid=%lu gui=%lu borderless=%d threaded=%d close_queued=%d exe=%ls\n",GetCurrentProcessId(),guiTid,opt.borderless,opt.threaded,opt.closeQueued,own);
    destroyedEvent=CreateEventW(NULL,TRUE,FALSE,NULL); require(destroyedEvent!=NULL,"destroy_event");
    closeCompletedEvent=CreateEventW(NULL,TRUE,FALSE,NULL); require(closeCompletedEvent!=NULL,"close_completion_event");
    WNDCLASSW cls={}; cls.lpfnWndProc=proc; cls.hInstance=GetModuleHandleW(NULL); cls.lpszClassName=L"Wave7PrivateSpatialGPU";
    require(RegisterClassW(&cls)!=0,"register_class");
    window=CreateWindowW(cls.lpszClassName,L"Wave7 private procedural D3D11 fixture",WS_OVERLAPPEDWINDOW|WS_VISIBLE,80,80,800,600,NULL,NULL,cls.hInstance,NULL);
    require(window!=NULL,"create_window");
    initialize(); // HWND-owner initialization; all later GPU work may run on the foreign render thread.
    if (opt.threaded) {
      HANDLE thread=CreateThread(NULL,0,worker,NULL,0,NULL); require(thread!=NULL,"create_render_thread");
      while (WaitForSingleObject(thread,0)==WAIT_TIMEOUT) { pump(); MsgWaitForMultipleObjects(1,&thread,FALSE,10,QS_ALLINPUT); }
      CloseHandle(thread);
    } else render();
    if (!destroyed) closeWindow(); pump();
    require(destroyed && quitSeen && !IsWindow(window),"normal_close_destroy_quit");
    CloseHandle(destroyedEvent); destroyedEvent=NULL; CloseHandle(closeCompletedEvent); closeCompletedEvent=NULL;
    if (!renderResult) printf("SMOKE_PASS wave=7 spatial_gpu_readback=1 actual_texture_dimensions=1 frames=%u natural_return=1\n",frames);
    else printf("SMOKE_EXPECTED_OR_OBSERVED_FAILURE exit=%d frames=%u natural_return=1\n",renderResult,frames);
    return renderResult;
  } catch (const Failure &f) {
    if (ctx.p) { ctx->ClearState(); ctx->Flush(); }
    swap.reset(); ctx.reset(); device.reset(); factory.reset();
    if (window && IsWindow(window)) DestroyWindow(window);
    if (destroyedEvent) CloseHandle(destroyedEvent);
    if (closeCompletedEvent) CloseHandle(closeCompletedEvent);
    printf("FIXTURE_SETUP_FAILURE exit=%d natural_return=1\n",f.code); return f.code;
  }
}
