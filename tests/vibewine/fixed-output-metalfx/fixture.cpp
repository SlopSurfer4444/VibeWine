// Production-bound CPU fixture; generated methods replace the markers below.
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include "production_geometry.hpp"
using namespace dxmt;
using UINT = uint32_t;
using HRESULT = int;
using DXGI_FORMAT = int;
constexpr HRESULT S_OK=0, E_FAIL=-1, E_INVALIDARG=-2, DXGI_ERROR_INVALID_CALL=-3;
inline constexpr int DXGI_FORMAT_UNKNOWN=0, DXGI_FORMAT_R8G8B8A8_UNORM=1,
 DXGI_FORMAT_R8G8B8A8_UNORM_SRGB=2, DXGI_FORMAT_B8G8R8A8_UNORM=3,
 DXGI_FORMAT_B8G8R8A8_UNORM_SRGB=4, DXGI_FORMAT_R16G16B16A16_FLOAT=5;
constexpr int DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709=0,
 DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709=1, WMTPixelFormatInvalid=-1;
constexpr uint32_t D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION=16384;
#define FAILED(hr) ((hr)<0)
#define WARN(...) ((void)0)
using d3d11_device_mutex=std::mutex;
namespace dxmt { using mutex=std::mutex; }
struct Sample { UINT Count=1, Quality=0; };
struct Desc { UINT Width=1280, Height=720; int Format=1; Sample SampleDesc; };
using D3D11_TEXTURE2D_DESC1=Desc;
struct RECT { int left=0, top=0, right=1920, bottom=1080; };
static bool client_ok=true;
static RECT client_rect;
static bool GetClientRect(int,RECT *r) { *r=client_rect; return client_ok; }
namespace wsi {
enum class DS3BorderlessResult { Disabled, Refused, Unchanged };
static bool ds3=true, refused=false;
static int resolver_calls=0;
static DS3BorderlessResult resolveDS3RenderSize(int,bool windowed,uint32_t &w,uint32_t &h,uint32_t oldw,uint32_t oldh) {
 ++resolver_calls;
 if(!ds3||!windowed) return DS3BorderlessResult::Disabled;
 if(refused) return DS3BorderlessResult::Refused;
 if(!w)w=oldw;if(!h)h=oldh;
 return DS3BorderlessResult::Unchanged;
}
}
namespace cs2trace {
struct ApiScope { uint32_t gen=0; template<class...T> ApiScope(T...){} template<class F> HRESULT invoke(F f){return f();} };
}
template<class T> struct Rc {
 std::shared_ptr<T> value;
 Rc()=default; Rc(T*p):value(p){} Rc(std::nullptr_t){} Rc(std::shared_ptr<T>p):value(std::move(p)){}
 Rc&operator=(T*p){value.reset(p);return *this;}
 Rc&operator=(std::nullptr_t){value.reset();return *this;}
 T*operator->()const{return value.get();} explicit operator bool()const{return bool(value);}
};
struct Texture { int id, format; UINT width_value,height_value; int pixelFormat()const{return format;} UINT width()const{return width_value;} UINT height()const{return height_value;} };
struct D3D11ResourceCommon {
 std::shared_ptr<Texture> value;
 Rc<Texture> texture() {return value;}
};
using ID3D11Texture2D1=D3D11ResourceCommon;
template<class T,bool Public=true> struct Com {
 std::shared_ptr<T> value;
 Com()=default; Com(std::nullptr_t){} Com(std::shared_ptr<T> p):value(std::move(p)){}
 Com &operator=(std::shared_ptr<T> p){value=std::move(p);return *this;}
 Com &operator=(std::nullptr_t){value.reset();return *this;}
 T*operator->()const{return value.get();} explicit operator bool()const{return bool(value);}
 auto prvRef(){return value;}
};
static int allocation_calls=0, fail_allocation=0, next_id=1;
static bool scaler_ok=true,fence_ok=true;
static std::vector<Desc> allocations;
struct Device { d3d11_device_mutex mutex; int GetMTLDevice(){return 1;} };
namespace dxmt {
static HRESULT CreateDeviceTexture2D(Device*,const Desc*d,void*,ID3D11Texture2D1**out) {
 allocations.push_back(*d);++allocation_calls;
 if(allocation_calls==fail_allocation)return E_FAIL;
 auto object=std::make_shared<D3D11ResourceCommon>();
 object->value=std::make_shared<Texture>(Texture{next_id++,d->Format,d->Width,d->Height});
 reinterpret_cast<Com<D3D11ResourceCommon>*>(out)->value=object;
 return S_OK;
}
}
struct WMTFXSpatialScalerInfo { UINT input_height,input_width,output_height,output_width; int color_format,output_format; };
struct SpatialScaler {
 WMTFXSpatialScalerInfo info; int identity=next_id++;
 SpatialScaler(int,const WMTFXSpatialScalerInfo&i):info(i){}
 bool scaler()const{return scaler_ok;} bool fence()const{return fence_ok;}
};
static int ConvertSwapChainFormat(int f){return f>=1&&f<=5?f:WMTPixelFormatInvalid;}
static int ConvertColorSpace(int f,bool){return f;}
struct Presenter {
 double width=0,height=0; bool aspect=false; int calls=0;
 bool changeLayerProperties(int,int,double w,double h,int,bool preserve=false){width=w;height=h;aspect=preserve;++calls;return false;}
};
struct Context { int waits=0; void WaitUntilGPUIdle(){++waits;} };
enum class ScalerType { None, Spatial };
struct ScalerInfo { ScalerType type=ScalerType::None; UINT input_width=0,input_height=0,output_width=0,output_height=0; };
struct ArgumentEncodingContext {
 struct Statistics {ScalerInfo last_scaler_info;} statistics;
 struct Queue {int statistics=0;} owned_queue;
 int upscales=0,presented_id=0,presented_state=0,scaler_identity=0;
 template<class F>void updateFrameStatistics(F f){f(statistics);}
 void upscale(Rc<Texture>&input,Rc<Texture>&output,Rc<SpatialScaler>&scaler){
  if(!input||!output||!scaler||!scaler->scaler()||!scaler->fence())std::abort();
  ++upscales;scaler_identity=scaler->identity;
 }
 void present(Rc<Texture>&texture,Presenter*,double,int state){if(!texture)std::abort();presented_id=texture->id;presented_state=state;}
 Queue&queue(){return owned_queue;} uint64_t currentFrameId(){return 1;}
};
struct Chunk {
 std::vector<std::function<void(ArgumentEncodingContext&)>> pending;
 template<class F>void emitcc(F f){pending.push_back(std::move(f));}
 void run(ArgumentEncodingContext&ctx){for(auto &f:pending)f(ctx);pending.clear();}
};
static int released_semaphores=0;
static void ReleaseSemaphore(int,int,void*){++released_semaphores;}
struct Config {
 static bool enabled;
 static bool provided;
 static Config&getInstance(){static Config c;return c;}
 template<class T>T getOption(const char*key,T fallback){
  if(std::string(key)=="d3d11.metalfxPerformanceUpscale")return provided?T(enabled):fallback;
  if(std::string(key)=="d3d11.metalfxOutputWidth")return T(1920);
  if(std::string(key)=="d3d11.metalfxOutputHeight")return T(1080);
  return fallback;
 }
};
bool Config::enabled=false;bool Config::provided=false;
template<bool EnableMetalFX> struct Swapchain {
 Device owned_device; Device*device_=&owned_device;
 std::mutex buffer_mutex_; Desc desc_,backbuffer_desc_;
 struct {bool Windowed=true;} fullscreen_desc_;
 int hWnd=1; uint64_t trace_serial_=1;
 float scale_factor=2;
 bool fixed_output_=true;
 int fixed_output_width_=1920,fixed_output_height_=1080;
 SpatialOutputGeometry output_geometry_;
 int colorspace_=DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
 Com<D3D11ResourceCommon,false> backbuffer_;
 Com<D3D11ResourceCommon> upscaled_backbuffer_;
 Rc<SpatialScaler> metalfx_scaler;
 Presenter owned_presenter; Presenter*presenter=&owned_presenter;
 Context owned_context; Context*device_context_=&owned_context;
 int present_semaphore_=1,statistics_updates=0;
 void UpdateStatistics(int,uint64_t){++statistics_updates;}
 bool LayerSupportEDR(){return false;}
 void TraceResizeState(uint32_t,uint32_t){}
 void Configure(){/* CONFIGURE */}
 void QueuePresent(Chunk*chunk,int state=77){double vsync_duration=0;auto sync_state=std::make_shared<int>(1);/* PRESENT_BRANCH */}
 /* RESIZE_METHOD */
 /* LAYER_METHOD */
};
static int checks=0;
static void check(bool condition,const char *message){++checks;if(!condition){std::cerr<<"FAIL: "<<message<<"\n";std::exit(1);}}
static void reset(){allocation_calls=fail_allocation=0;allocations.clear();scaler_ok=fence_ok=true;wsi::ds3=true;wsi::refused=false;client_ok=true;client_rect={0,0,1920,1080};}
template<class S> static int buffer_id(S&s){return s.backbuffer_?s.backbuffer_->texture()->id:0;}
template<class S> static int scaler_id(S&s){return s.metalfx_scaler?s.metalfx_scaler->identity:0;}
template<class S> static HRESULT resize(S&s,UINT w=1280,UINT h=720,int format=0){return s.ResizeBuffers(0,w,h,format,0);}
template<class S> static void fixed(S&s){
 s.colorspace_=0;s.desc_.SampleDesc.Count=s.backbuffer_desc_.SampleDesc.Count=1;
 check(resize(s,1280,720,1)==S_OK,"initial fixed resize succeeds");
 check(s.desc_.Width==1280&&s.desc_.Height==720&&s.backbuffer_desc_.Width==1280&&s.backbuffer_desc_.Height==720,"public render dimensions stay low");
 check(s.backbuffer_->texture()->width()==1280&&s.backbuffer_->texture()->height()==720,"public texture stays low");
 check(s.upscaled_backbuffer_&&s.upscaled_backbuffer_->texture()->width()==1920&&s.upscaled_backbuffer_->texture()->height()==1080,"fixed output texture dimensions");
 check(s.metalfx_scaler&&s.metalfx_scaler->info.input_width==1280&&s.metalfx_scaler->info.output_width==1920,"scaler descriptor separates input/output");
 check(s.presenter->width==1920&&s.presenter->height==1080&&s.presenter->aspect,"layer uses independent output with aspect preservation");
}
int main(){
 reset();Swapchain<true>s;fixed(s);
 Config::provided=false;Swapchain<true>config_default;config_default.fixed_output_=false;config_default.Configure();
 check(!config_default.fixed_output_,"performance mode defaults disabled");
 Config::provided=true;Config::enabled=true;Swapchain<true>config_enabled;config_enabled.fixed_output_=false;config_enabled.Configure();
#if defined(DXMT_FIXED_OUTPUT_METALFX) && DXMT_FIXED_OUTPUT_METALFX
 check(config_enabled.fixed_output_&&config_enabled.fixed_output_width_==1920&&config_enabled.fixed_output_height_==1080,"feature compiled and configured on");
#else
 check(!config_enabled.fixed_output_,"feature compile gate ignores configured opt-in");
#endif
 Swapchain<false>config_ordinary;config_ordinary.fixed_output_=false;config_ordinary.Configure();
 check(!config_ordinary.fixed_output_,"non-MetalFX template ignores opt-in");
 check(resize(s,0,0)==S_OK&&s.desc_.Width==1280&&s.desc_.Height==720,"DS3 zero axes preserve render size after client enlargement");
 check(resize(s,0,600)==S_OK&&s.desc_.Width==1280&&s.desc_.Height==600,"DS3 zero width retains previous width");
 check(resize(s,800,0)==S_OK&&s.desc_.Width==800&&s.desc_.Height==600,"DS3 zero height retains previous height");
 check(resize(s,1600,900)==S_OK&&s.desc_.Width==1600&&s.presenter->width==1920&&s.upscaled_backbuffer_->texture()->width()==1920,"explicit render resize keeps fixed output");
 // Invalid output configuration is rejected before any candidate allocation/publication.
 for(auto pair:std::vector<std::pair<int,int>>{{0,1080},{1920,0},{-1,1080},{1920,-1},{16385,1080},{1920,16385}}){
  int old=buffer_id(s),old_scaler=scaler_id(s),calls=allocation_calls,layers=s.presenter->calls;
  s.fixed_output_width_=pair.first;s.fixed_output_height_=pair.second;
  check(resize(s,640,360)==E_INVALIDARG,"invalid pair rejected");
  check(buffer_id(s)==old&&scaler_id(s)==old_scaler&&s.desc_.Width==1600&&allocation_calls==calls&&s.presenter->calls==layers,"invalid pair has no allocation or publication");
 }
 s.fixed_output_width_=1920;s.fixed_output_height_=1080;
 for(int failure:{1,2}){
  reset();fixed(s);int old=buffer_id(s),old_scaler=scaler_id(s),layers=s.presenter->calls;
  fail_allocation=allocation_calls+failure;
  check(resize(s,640,360)==E_FAIL,"candidate allocation failure reported");
  check(buffer_id(s)==old&&scaler_id(s)==old_scaler&&s.desc_.Width==1280&&s.output_geometry_.width==1920&&s.presenter->calls==layers,"allocation failure retains committed resources/geometry");
 }
 for(bool fail_scaler:{true,false}){
  reset();fixed(s);if(fail_scaler)scaler_ok=false;else fence_ok=false;
  check(resize(s,640,360)==S_OK,"nil optional scaler/fence falls back successfully");
  check(!s.metalfx_scaler&&!s.upscaled_backbuffer_&&s.desc_.Width==640&&s.presenter->width==1920,"fallback publishes new input and clears old scaler/output");
 }
 struct Case {UINT w,h,samples;int format,space;};
 for(auto c:std::vector<Case>{{800,600,1,1,0},{1920,1080,1,1,0},{2560,1440,1,1,0},{1280,720,4,1,0},{1280,720,1,5,0},{1280,720,1,1,1}}){
  reset();fixed(s);s.desc_.SampleDesc.Count=s.backbuffer_desc_.SampleDesc.Count=c.samples;s.colorspace_=c.space;
  int before=allocation_calls;
  check(resize(s,c.w,c.h,c.format)==S_OK,"unsupported scaler case uses ordinary presentation");
  check(!s.metalfx_scaler&&!s.upscaled_backbuffer_&&allocation_calls==before+1,"unsupported case allocates only public input and clears scaler");
  check(s.presenter->width==1920&&s.presenter->height==1080&&s.presenter->aspect,"ordinary fallback retains fixed presentation geometry");
 }
 reset();s.colorspace_=0;s.desc_.SampleDesc.Count=s.backbuffer_desc_.SampleDesc.Count=1;wsi::ds3=false;
 check(resize(s,0,0,1)==S_OK&&s.desc_.Width==1920&&s.desc_.Height==1080,"non-DS3 zero axes follow client");
 wsi::refused=true;wsi::ds3=true;int old=buffer_id(s),calls=allocation_calls;
 check(resize(s)==DXGI_ERROR_INVALID_CALL&&buffer_id(s)==old&&allocation_calls==calls,"resolver refusal prevents publication");
 reset();wsi::ds3=false;client_ok=false;
 check(resize(s,0,0)==DXGI_ERROR_INVALID_CALL,"client query failure rejected");
 reset();Swapchain<true>legacy;legacy.fixed_output_=false;
 check(resize(legacy,800,600)==S_OK&&legacy.desc_.Width==800&&legacy.upscaled_backbuffer_->texture()->width()==1600&&legacy.upscaled_backbuffer_->texture()->height()==1200,"legacy factor output unchanged");
 check(legacy.presenter->width==1600&&legacy.presenter->height==1200&&!legacy.presenter->aspect,"legacy layer dimensions/aspect flag unchanged");
 old=buffer_id(legacy);int old_scaler=scaler_id(legacy);scaler_ok=false;
 check(resize(legacy,640,480)==E_FAIL&&buffer_id(legacy)==old&&scaler_id(legacy)==old_scaler&&legacy.desc_.Width==800,"legacy nil scaler remains failure with old state");
 reset();Swapchain<false>ordinary;ordinary.fixed_output_=false;ordinary.scale_factor=1;
 check(resize(ordinary,800,600)==S_OK&&!ordinary.metalfx_scaler&&!ordinary.upscaled_backbuffer_&&ordinary.presenter->width==800,"ordinary template remains unscaled");
 // Execute the production Present branch after resources have been replaced.
 reset();fixed(s);Chunk chunk;ArgumentEncodingContext encoding;
 auto previous_input=s.backbuffer_->texture();auto previous_output=s.upscaled_backbuffer_->texture();auto previous_scaler=s.metalfx_scaler;
 int output_id=previous_output->id,scale_id=previous_scaler->identity;
 std::weak_ptr<Texture> input_lifetime=previous_input.value,output_lifetime=previous_output.value;
 std::weak_ptr<SpatialScaler> scaler_lifetime=previous_scaler.value;
 s.QueuePresent(&chunk);previous_input=nullptr;previous_output=nullptr;previous_scaler=nullptr;
 check(resize(s,640,360)==S_OK&&!input_lifetime.expired()&&!output_lifetime.expired()&&!scaler_lifetime.expired(),"queued frame retains old input/output/scaler across resize");
 chunk.run(encoding);
 check(encoding.upscales==1&&encoding.presented_id==output_id&&encoding.scaler_identity==scale_id&&encoding.presented_state==77,"queued frame encodes captured output/scaler/state");
 check(input_lifetime.expired()&&output_lifetime.expired()&&scaler_lifetime.expired(),"captured old resources retire after queued callback");
 check(encoding.statistics.last_scaler_info.type==ScalerType::Spatial&&encoding.statistics.last_scaler_info.input_width==1280&&encoding.statistics.last_scaler_info.output_width==1920,"spatial statistics describe captured frame");
 for(bool nil_scaler:{true,false}){
  reset();fixed(s);if(nil_scaler)scaler_ok=false;else fence_ok=false;
  check(resize(s,640,360)==S_OK,"prepare nil resource Present fallback");
  int chosen=buffer_id(s);ArgumentEncodingContext fallback;Chunk pending;s.QueuePresent(&pending);pending.run(fallback);
  check(fallback.upscales==0&&fallback.presented_id==chosen&&fallback.statistics.last_scaler_info.type==ScalerType::None,"nil scaler/fence never upscale and present ordinary input");
 }
 reset();fixed(s);s.colorspace_=1;ArgumentEncodingContext hdr;Chunk hdr_chunk;s.QueuePresent(&hdr_chunk);hdr_chunk.run(hdr);
 check(hdr.upscales==0&&hdr.presented_id==buffer_id(s),"post-resize HDR color-space change bypasses existing scaler");
 ArgumentEncodingContext direct;Chunk direct_chunk;ordinary.QueuePresent(&direct_chunk);direct_chunk.run(direct);
 check(direct.upscales==0&&direct.presented_id==buffer_id(ordinary),"ordinary template Present selects public input");
 check(released_semaphores==5&&s.statistics_updates==4,"all queued branches release semaphore and update statistics");
 std::cout<<"PASS: "<<checks<<" production configuration/ResizeBuffers/ApplyLayerProps/Present assertions\n";
}
