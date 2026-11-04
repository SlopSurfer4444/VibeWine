#include <atomic>
#include <functional>
#include <thread>
#include <mutex>
#include <memory>
#include <vector>
#include <string>
#include <stdexcept>
#include <utility>
#include <algorithm>
#include <variant>
#include <concepts>
static unsigned client_queries=0,owner_queries=0;
// WIN32_POLICY
using UINT=unsigned;using HRESULT=int32_t;using DXGI_FORMAT=int;
constexpr HRESULT S_OK=0,E_FAIL=-1,DXGI_ERROR_INVALID_CALL=-2,DXGI_ERROR_NOT_CURRENTLY_AVAILABLE=-3,DXGI_STATUS_MODE_CHANGE_IN_PROGRESS=1,DXGI_ERROR_UNSUPPORTED=-4,E_INVALIDARG=-5;
constexpr UINT D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION=16384;
constexpr DXGI_FORMAT DXGI_FORMAT_UNKNOWN=0;
constexpr int WMTPixelFormatInvalid=0;
#define STDMETHODCALLTYPE
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
#define __uuidof(x) 0
#define WARN(...) ((void)0)
#define D3D11_ASSERT(x) assert(x)
#include "SOURCE_ROOT/src/util/com/com_pointer.hpp"
#include "SOURCE_ROOT/src/util/rc/util_rc_ptr.hpp"
using dxmt::Com;using dxmt::Rc;
namespace cs2trace {struct Record{uint32_t gen,type;uint64_t start,end,object;uint32_t flags;uint64_t a,b;};static std::vector<Record> records;uint64_t now(){return 99;}void record(uint32_t gen,uint32_t type,uint64_t start,uint64_t end,uint64_t object,uint32_t flags,uint64_t a,uint64_t b){records.push_back({gen,type,start,end,object,flags,a,b});}struct ApiScope {uint32_t gen=0;template<class... T>ApiScope(T...){}template<class F>HRESULT invoke(F f){return f();}};}
struct DXGI_RATIONAL{UINT Numerator=60,Denominator=1;};
struct DXGI_MODE_DESC{UINT Width=0,Height=0;DXGI_RATIONAL RefreshRate;DXGI_FORMAT Format=1;int ScanlineOrdering=0,Scaling=0;};using DXGI_MODE_DESC1=DXGI_MODE_DESC;
struct Desc{UINT Width=1280,Height=720;DXGI_FORMAT Format=1;UINT BufferCount=2,Flags=0;struct{UINT Count=1,Quality=0;}SampleDesc;};
struct D3D11_TEXTURE2D_DESC1{UINT Width=1280,Height=720;DXGI_FORMAT Format=1;};
static bool fail_scaler=false,fail_fence=false;
static int live_resources=0,live_scalers=0,allocations=0,fail_allocation=0,next_id=0;
struct D3D11ResourceCommon{int id=++next_id,public_refs=1,private_refs=0;D3D11_TEXTURE2D_DESC1 desc;explicit D3D11ResourceCommon(D3D11_TEXTURE2D_DESC1 d):desc(d){live_resources++;}~D3D11ResourceCommon(){live_resources--;}void AddRef(){public_refs++;}void AddRefPrivate(){private_refs++;}void Release(){assert(public_refs>0);public_refs--;collect();}void ReleasePrivate(){assert(private_refs>0);private_refs--;collect();}void collect(){if(!public_refs&&!private_refs)delete this;}D3D11ResourceCommon *texture(){return this;}int pixelFormat(){return desc.Format;}UINT width(){return desc.Width;}UINT height(){return desc.Height;}};
using ID3D11Texture2D1=D3D11ResourceCommon;
struct WMTFXSpatialScalerInfo{UINT input_height,input_width,output_height,output_width;int color_format,output_format;};
struct SpatialScaler{int refs=0,id=++next_id;SpatialScaler(int,WMTFXSpatialScalerInfo){live_scalers++;}~SpatialScaler(){live_scalers--;}int scaler(){return fail_scaler?0:1;}int fence(){return fail_fence?0:1;}void incRef(){refs++;}void decRef(){if(!--refs)delete this;}};
struct Device{std::mutex mutex;int GetMTLDevice(){return 1;}};
using d3d11_device_mutex=std::mutex;
namespace dxmt {using mutex=std::mutex;HRESULT CreateDeviceTexture2D(Device *,const D3D11_TEXTURE2D_DESC1 *d,void *,ID3D11Texture2D1 **out){allocations++;if(allocations==fail_allocation)return E_FAIL;*out=new D3D11ResourceCommon(*d);return S_OK;}}
static std::function<void()> resize_callback;
static bool fail_resize_window=false,fail_fullscreen_window=false;
namespace dxmt::wsi {bool isWindow(HWND h){return IsWindow(h);}void getWindowSize(HWND h,uint32_t *w,uint32_t *hh){RECT r{};GetClientRect(h,&r);*w=r.right-r.left;*hh=r.bottom-r.top;}template<class T>bool resizeWindow(HWND,T *,UINT w,UINT h){positions++;if(fail_resize_window)return false;client_area={0,0,LONG(w),LONG(h)};if(resize_callback)resize_callback();return true;}bool updateFullscreenWindow(int,HWND,bool){positions++;return !fail_fullscreen_window;}}
namespace wsi=dxmt::wsi;
int ConvertSwapChainFormat(int f){return f==1||f==2?f:WMTPixelFormatInvalid;}
// MODESET_GUARD
struct Base{virtual HRESULT ResizeBuffers(UINT,UINT,UINT,DXGI_FORMAT,UINT)=0;virtual HRESULT ResizeTarget(const DXGI_MODE_DESC *)=0;virtual ~Base()=default;};
template<bool EnableMetalFX>struct Swapchain:Base{
 Desc desc_;D3D11_TEXTURE2D_DESC1 backbuffer_desc_;Com<D3D11ResourceCommon,false> backbuffer_;
 std::conditional_t<EnableMetalFX,Com<D3D11ResourceCommon>,std::monostate> upscaled_backbuffer_;
 std::conditional_t<EnableMetalFX,Rc<SpatialScaler>,std::monostate> metalfx_scaler;
 float scale_factor=EnableMetalFX?2.f:1.f;Device dev;Device *device_=&dev;HWND hWnd=1;int monitor_=1,trace_serial_=1;
 struct{bool Windowed=true;DXGI_RATIONAL RefreshRate;int ScanlineOrdering=0,Scaling=0;}fullscreen_desc_;
 struct{bool captured=false;}window_state_;char pending_display_name_[32]={};ModeSetGuard modeset_guard_;std::mutex mutex_,buffer_mutex_;
 struct{void *ptr(){return nullptr;}}target_;HRESULT ChangeDisplayMode(void *,DXGI_MODE_DESC1 *){return S_OK;}
 int layer_updates=0;UINT presenter_width=1280,presenter_height=720;int presenter_format=1;
 // TRACE_STATE
 void ApplyLayerProps(){layer_updates++;presenter_width=desc_.Width;presenter_height=desc_.Height;presenter_format=desc_.Format;}
 Swapchain(){backbuffer_=new D3D11ResourceCommon(backbuffer_desc_);backbuffer_->Release();if constexpr(EnableMetalFX){upscaled_backbuffer_=Com<D3D11ResourceCommon>::transfer(new D3D11ResourceCommon(backbuffer_desc_));metalfx_scaler=new SpatialScaler(1,{});}}
// RESIZE_BUFFERS
// RESIZE_TARGET
};
void check(bool x,const std::string &msg){if(!x)throw std::runtime_error(msg);}
static unsigned cases=0,failures=0;
template<class F>void test(const std::string &name,F f){try{reset();allocations=fail_allocation=0;fail_scaler=fail_fence=false;fail_resize_window=fail_fullscreen_window=false;resize_callback={};cs2trace::records.clear();client_queries=owner_queries=0;f();resize_callback={};check(live_resources==0&&live_scalers==0,"leaked allocated resources");cases++;}catch(const std::exception &e){resize_callback={};failures++;fprintf(stderr,"FAIL %s: %s\n",name.c_str(),e.what());}}
template<bool FX>void transaction(int fail_at){opt=L"";Swapchain<FX> sc;auto old=sc.backbuffer_.ptr();D3D11ResourceCommon *up=nullptr;SpatialScaler *scaler=nullptr;if constexpr(FX){up=sc.upscaled_backbuffer_.ptr();scaler=sc.metalfx_scaler.ptr();}int live=live_resources;fail_allocation=fail_at;check(FAILED(sc.ResizeBuffers(3,1024,576,2,0)),"allocation failure must propagate");check(sc.desc_.Width==1280&&sc.desc_.Height==720&&sc.desc_.Format==1,"descriptor changed on failure");check(sc.backbuffer_desc_.Width==1280&&sc.backbuffer_desc_.Height==720&&sc.backbuffer_desc_.Format==1,"texture descriptor changed on failure");check(sc.backbuffer_.ptr()==old,"backbuffer identity lost on failure");check(sc.layer_updates==0&&sc.presenter_width==1280&&sc.presenter_height==720&&sc.presenter_format==1,"presenter changed before allocation success");if constexpr(FX){check(sc.upscaled_backbuffer_.ptr()==up&&sc.metalfx_scaler.ptr()==scaler,"MetalFX state lost on failure");}check(live_resources==live,"failed transaction leaked temporary texture");fail_allocation=0;allocations=0;check(sc.ResizeBuffers(3,1024,576,2,0)==S_OK,"retry failed");check(sc.desc_.Width==1024&&sc.desc_.Height==576&&sc.desc_.Format==2,"retry descriptor not committed");check(sc.backbuffer_.ptr()!=old&&sc.layer_updates==1&&sc.presenter_width==1024&&sc.presenter_height==576,"retry not committed to backbuffer/presenter");}
int main(){
 for(bool ds3:{false,true})for(bool owner:{false,true})for(unsigned w:{0u,1024u})for(unsigned h:{0u,576u}){
 std::string name="dimensions ds3="+std::to_string(ds3)+" owner="+std::to_string(owner)+" w="+std::to_string(w)+" h="+std::to_string(h);
 test(name,[=](){if(!ds3)opt=L"";if(!owner)self_tid++;Swapchain<false> sc;auto r=sc.ResizeBuffers(0,w,h,0,0);check(r==S_OK,"ResizeBuffers did not succeed");auto ew=w?w:(ds3?1280u:1600u),eh=h?h:(ds3?720u:900u);check(sc.desc_.Width==ew&&sc.desc_.Height==eh,"zero axis fallback or explicit axis preservation wrong");check(sc.backbuffer_desc_.Width==ew&&sc.backbuffer_desc_.Height==eh,"texture dimensions wrong");check(!writes&&!positions,"ResizeBuffers mutated window geometry/style");check(sc.presenter_width==ew&&sc.presenter_height==eh,"presenter dimensions wrong");});}
 test("ordinary allocation failure and retry",[]{transaction<false>(1);});
 test("MetalFX first allocation failure and retry",[]{transaction<true>(1);});
 test("MetalFX second allocation failure and retry",[]{transaction<true>(2);});
 for(bool owner:{false,true})test(std::string("DS3 unsupported target owner=")+std::to_string(owner),[=](){if(!owner)self_tid++;Swapchain<false> sc;auto old=sc.backbuffer_.ptr();DXGI_MODE_DESC target;target.Width=3840;target.Height=2160;check(FAILED(sc.ResizeTarget(&target)),"DS3 ResizeTarget falsely reported success");check(sc.desc_.Width==1280&&sc.desc_.Height==720&&sc.backbuffer_.ptr()==old&&!sc.layer_updates,"ResizeTarget changed render buffer state");check(!writes&&!positions,"unsupported target mutated geometry");});

 for(bool ds3:{false,true})for(unsigned w:{0u,1024u})for(unsigned h:{0u,576u})test("uninitialized axes fallback "+std::to_string(ds3)+"/"+std::to_string(w)+"/"+std::to_string(h),[=](){if(!ds3)opt=L"";Swapchain<false> sc;sc.desc_.Width=sc.desc_.Height=0;check(sc.ResizeBuffers(0,w,h,0,0)==S_OK,"initial resize failed");check(sc.desc_.Width==(w?w:1600u)&&sc.desc_.Height==(h?h:900u),"initial per-axis fallback wrong");check(!writes&&!positions,"initial resolution changed geometry");});
 for(int failure:{0,1,2})test("invalid input preserves resources "+std::to_string(failure),[=](){opt=L"";Swapchain<false> sc;auto old=sc.backbuffer_.ptr();if(failure==0)client_error=true;if(failure==1)client_area.right=-1;auto hr=sc.ResizeBuffers(0,0,576,failure==2?999:0,0);check(FAILED(hr),"invalid input accepted");check(sc.desc_.Width==1280&&sc.desc_.Height==720&&sc.desc_.Format==1&&sc.backbuffer_.ptr()==old&&!sc.layer_updates&&!allocations,"invalid input changed committed state");});
 for(int failure:{0,1})test("MetalFX scaler failure and retry "+std::to_string(failure),[=](){opt=L"";Swapchain<true> sc;auto old=sc.backbuffer_.ptr();auto oldup=sc.upscaled_backbuffer_.ptr();auto oldscaler=sc.metalfx_scaler.ptr();fail_scaler=failure==0;fail_fence=failure==1;check(FAILED(sc.ResizeBuffers(0,1024,576,2,0)),"incomplete scaler accepted");check(sc.desc_.Width==1280&&sc.desc_.Height==720&&sc.desc_.Format==1&&sc.backbuffer_.ptr()==old&&sc.upscaled_backbuffer_.ptr()==oldup&&sc.metalfx_scaler.ptr()==oldscaler&&!sc.layer_updates,"scaler failure changed state");check(live_resources==2&&live_scalers==1,"scaler failure leaked candidate resources");fail_scaler=fail_fence=false;check(sc.ResizeBuffers(0,1024,576,2,0)==S_OK,"scaler failure retry failed");});
 test("MetalFX scaled size overflow",[]{opt=L"";Swapchain<true> sc;auto old=sc.backbuffer_.ptr();check(FAILED(sc.ResizeBuffers(0,10000,576,2,0)),"oversized upscaled resource accepted");check(sc.backbuffer_.ptr()==old&&sc.desc_.Width==1280&&!sc.layer_updates&&live_resources==2,"oversize candidate changed state or leaked");});
 test("zero client axis texture clamp",[]{opt=L"";client_area.right=0;Swapchain<false> sc;check(sc.ResizeBuffers(0,0,576,0,0)==S_OK,"zero client resize failed");check(sc.desc_.Width==0&&sc.desc_.Height==576&&sc.backbuffer_desc_.Width==1&&sc.backbuffer_desc_.Height==576,"zero axis clamped both axes");});
 test("already satisfied DS3 target",[]{Swapchain<false> sc;auto old=sc.backbuffer_.ptr();DXGI_MODE_DESC target;target.Width=1600;target.Height=900;check(sc.ResizeTarget(&target)==S_OK,"matching client target rejected");check(sc.backbuffer_.ptr()==old&&sc.desc_.Width==1280&&sc.desc_.Height==720&&!sc.layer_updates&&!writes&&!positions,"matching target mutated state");});
 test("unsupported target does not seed future zero buffers",[]{Swapchain<false> sc;DXGI_MODE_DESC target;target.Width=3840;target.Height=2160;check(FAILED(sc.ResizeTarget(&target)),"unsupported target accepted");check(sc.ResizeBuffers(0,0,0,0,0)==S_OK,"resize after target failure failed");check(sc.desc_.Width==1280&&sc.desc_.Height==720,"failed target seeded dimensions");});

 test("synchronous target callback resizes buffers and refuses recursive target",[]{opt=L"";Swapchain<false> sc;DXGI_MODE_DESC target;target.Width=1024;target.Height=576;unsigned callbacks=0;resize_callback=[&](){callbacks++;bool unlocked=false;std::thread observer([&](){unlocked=sc.mutex_.try_lock();if(unlocked)sc.mutex_.unlock();});observer.join();check(unlocked,"renderer mutex held during synchronous geometry callback");check(sc.ResizeBuffers(0,0,0,0,0)==S_OK,"callback ResizeBuffers failed");check(sc.ResizeTarget(&target)==DXGI_STATUS_MODE_CHANGE_IN_PROGRESS,"recursive target did not return mode-in-progress");};check(sc.ResizeTarget(&target)==S_OK,"outer target failed");check(callbacks==1&&positions==1&&sc.desc_.Width==1024&&sc.desc_.Height==576,"callback dimensions or mutation count wrong");resize_callback={};});

 test("window helper failure propagates without committing target metadata",[]{opt=L"";Swapchain<false> sc;auto old=sc.backbuffer_.ptr();DXGI_MODE_DESC target;target.Width=1024;target.Height=576;target.RefreshRate={75,1};target.ScanlineOrdering=2;target.Scaling=3;fail_resize_window=true;check(sc.ResizeTarget(&target)==DXGI_ERROR_NOT_CURRENTLY_AVAILABLE,"window helper failure swallowed");check(sc.fullscreen_desc_.RefreshRate.Numerator==60&&sc.fullscreen_desc_.ScanlineOrdering==0&&sc.fullscreen_desc_.Scaling==0,"target metadata committed despite rejected window operation");check(sc.backbuffer_.ptr()==old&&sc.desc_.Width==1280&&sc.desc_.Height==720&&!sc.layer_updates,"failed target changed buffers/presenter");fail_resize_window=false;check(sc.ResizeTarget(&target)==S_OK,"failed target retry failed");check(sc.fullscreen_desc_.RefreshRate.Numerator==75&&sc.fullscreen_desc_.ScanlineOrdering==2&&sc.fullscreen_desc_.Scaling==3,"successful target metadata not committed");});
 test("fullscreen window helper failure propagates and retry succeeds",[]{opt=L"";Swapchain<false> sc;sc.fullscreen_desc_.Windowed=false;auto old=sc.backbuffer_.ptr();DXGI_MODE_DESC target;target.Width=1024;target.Height=576;fail_fullscreen_window=true;check(sc.ResizeTarget(&target)==DXGI_ERROR_NOT_CURRENTLY_AVAILABLE,"fullscreen helper failure swallowed");check(sc.backbuffer_.ptr()==old&&sc.desc_.Width==1280&&sc.desc_.Height==720&&!sc.layer_updates,"fullscreen target failure changed buffers/presenter");fail_fullscreen_window=false;check(sc.ResizeTarget(&target)==S_OK,"fullscreen failed target retry failed");});

 test("resize diagnostics report committed texture and current client",[]{Swapchain<false> sc;self_tid=9;sc.desc_.Width=1024;sc.desc_.Height=576;sc.backbuffer_->desc.Width=1000;sc.backbuffer_->desc.Height=500;sc.TraceResizeState(1,44|(1u<<16));check(cs2trace::records.size()==2,"expected GPU and client records");const auto &gpu=cs2trace::records[0],&window=cs2trace::records[1];check(gpu.gen==1&&gpu.type==61&&gpu.object==1&&gpu.flags==(44u|(1u<<16)),"GPU record identity wrong");check(gpu.a==((uint64_t(1024)<<32)|576)&&gpu.b==((uint64_t(1000)<<32)|500),"descriptor/texture diagnostics confused");check(window.type==62&&window.a==((uint64_t(7)<<32)|9)&&window.b==((uint64_t(1600)<<32)|900),"thread/client diagnostics wrong");check(client_queries==1&&owner_queries==1,"unexpected geometry query count");});
 test("disabled resize diagnostics perform no queries",[]{Swapchain<false> sc;sc.TraceResizeState(0,44);check(cs2trace::records.empty()&&!client_queries&&!owner_queries,"disabled diagnostics queried state");});
 test("failed client diagnostic query marked invalid",[]{Swapchain<false> sc;client_error=true;sc.TraceResizeState(1,45);check(cs2trace::records.size()==2&&cs2trace::records[1].flags==(45u|(1u<<17)),"invalid client query not marked");});
 test("null target",[]{Swapchain<false> sc;check(sc.ResizeTarget(nullptr)==DXGI_ERROR_INVALID_CALL,"null target accepted");});

 test("DS3 explicit low high low changes committed resources",[]{Swapchain<false> sc;for(auto wh:std::vector<std::pair<UINT,UINT>>{{1280,720},{2560,1440},{1280,720}}){check(sc.ResizeBuffers(0,wh.first,wh.second,0,0)==S_OK,"explicit menu buffers rejected");check(sc.desc_.Width==wh.first&&sc.desc_.Height==wh.second&&sc.backbuffer_->desc.Width==wh.first&&sc.backbuffer_->desc.Height==wh.second,"explicit menu not committed");check(client_area.right==1600&&client_area.bottom==900&&!writes&&!positions,"explicit menu changed physical client");}});
 test("DS3 target then zero cannot currently realize menu size",[]{Swapchain<false> sc;DXGI_MODE_DESC target;target.Width=2560;target.Height=1440;check(sc.ResizeTarget(&target)==DXGI_ERROR_UNSUPPORTED,"unsupported logical target must not fake success");check(sc.ResizeBuffers(0,0,0,0,0)==S_OK,"compatibility zero failed");check(sc.desc_.Width==1280&&sc.desc_.Height==720&&sc.backbuffer_->desc.Width==1280&&sc.backbuffer_->desc.Height==720,"rejected target unexpectedly changed R");check(client_area.right==1600&&client_area.bottom==900&&!writes&&!positions,"rejected target changed C");});
 test("general target then zero supports real logical client low high low",[]{opt=L"";Swapchain<false> sc;for(auto wh:std::vector<std::pair<UINT,UINT>>{{1280,720},{2560,1440},{1280,720}}){DXGI_MODE_DESC target;target.Width=wh.first;target.Height=wh.second;check(sc.ResizeTarget(&target)==S_OK,"general target rejected");check(client_area.right==long(wh.first)&&client_area.bottom==long(wh.second),"general target failed client change");check(sc.ResizeBuffers(0,0,0,0,0)==S_OK,"general zero resize failed");check(sc.desc_.Width==wh.first&&sc.desc_.Height==wh.second&&sc.backbuffer_->desc.Width==wh.first&&sc.backbuffer_->desc.Height==wh.second,"general target+zero not committed");}});
 printf("{\"passed\":%s,\"cases\":%u,\"failures\":%u,\"wineStarted\":false}\n",failures?"false":"true",cases,failures);return failures?1:0;
}
