#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <tuple>
#include "HEADER_PATH"
using UINT=uint32_t; using HRESULT=int32_t; using DXGI_FORMAT=int; struct IUnknown{};
constexpr HRESULT S_OK=0,DXGI_ERROR_INVALID_CALL=-1,DXGI_ERROR_MORE_DATA=-2,DXGI_ERROR_NOT_FOUND=-3,E_FAIL=-4;
constexpr int DXGI_FORMAT_UNKNOWN=0,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB=1;
constexpr int DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED=0,DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE=1,DXGI_MODE_SCANLINE_ORDER_UPPER_FIELD_FIRST=2,DXGI_MODE_SCALING_UNSPECIFIED=0;
constexpr UINT DXGI_ENUM_MODES_INTERLACED=1,DXGI_ENUM_MODES_SCALING=2,MTL_DXGI_FORMAT_BACKBUFFER=1;
constexpr bool FALSE=false;
#define FAILED(hr) ((hr)<0)
#define ERR(...) ((void)0)
struct DXGI_RATIONAL{UINT Numerator=0,Denominator=0;};
struct DXGI_MODE_DESC1{UINT Width=0,Height=0;DXGI_RATIONAL RefreshRate;DXGI_FORMAT Format=0;int ScanlineOrdering=0,Scaling=0;bool Stereo=false;};
namespace wsi {
struct WsiMode{UINT width,height,bitsPerPixel;struct{UINT numerator,denominator;}refreshRate;bool interlaced;};
static std::vector<WsiMode> modes;static WsiMode current{1920,1080,32,{60,1},false};
bool getDisplayMode(int,UINT i,WsiMode *m){if(i>=modes.size())return false;*m=modes[i];return true;}
bool getCurrentDisplayMode(int,WsiMode *m){*m=current;return true;}
bool getDesktopDisplayMode(int,WsiMode *m){*m=current;return true;}
}
struct MTL_DXGI_FORMAT_DESC{UINT Flag;};
HRESULT MTLQueryDXGIFormat(int,DXGI_FORMAT,MTL_DXGI_FORMAT_DESC &d){d.Flag=MTL_DXGI_FORMAT_BACKBUFFER;return S_OK;}
UINT GetMonitorFormatBpp(DXGI_FORMAT){return 32;}
namespace str{template<class... T> int format(T&&...){return 0;}}
struct Logger{static void debug(int){}};
// CONVERT
// FILTER
using dxmt::LimitDisplayModeResolutions;
using Catalog=std::vector<wsi::WsiMode>;
struct Step{bool fill; Catalog modes; HRESULT result=S_OK; std::optional<UINT> count=std::nullopt;};
void check(bool ok,const char*why){if(!ok)throw std::runtime_error(why);}
struct Base{virtual HRESULT FindClosestMatchingMode1(const DXGI_MODE_DESC1*,DXGI_MODE_DESC1*,IUnknown*)=0;virtual ~Base()=default;};
struct Output:Base{
 struct{bool limitDisplayModeResolutions=false;}options_;
 struct Adapter{int GetMTLDevice(){return 1;}}adapter;Adapter*adapter_=&adapter;int monitor_=1;
 std::vector<Step> steps;size_t cursor=0;std::vector<UINT> counts;
 HRESULT GetDisplayModeList1(DXGI_FORMAT format,UINT flags,UINT*n,DXGI_MODE_DESC1*p){
  check(cursor<steps.size(),"unexpected extra enumeration call");auto&s=steps[cursor++];check(s.fill==(p!=nullptr),"count/fill call order changed");
  wsi::modes=s.modes;HRESULT hr=GetDisplayModeList1Impl(format,flags,n,p);counts.push_back(*n);
  if(s.count)*n=*s.count;
  return FAILED(s.result)?s.result:hr;
 }
// LIST
// CLOSEST
};
auto key(const DXGI_MODE_DESC1&m){return std::tuple{m.Width,m.Height,m.RefreshRate.Numerator,m.RefreshRate.Denominator,m.Format,m.ScanlineOrdering,m.Scaling,m.Stereo};}
wsi::WsiMode mode(UINT w,UINT h,UINT n=60,UINT d=1){return {w,h,32,{n,d},false};}
Catalog catalog(UINT n){Catalog r;for(UINT i=0;i<n;i++)r.push_back(mode(800+i*10,600+i*5));return r;}
int main(int argc,char**argv){try{
 check(argc==3,"usage scenario cap");std::string scenario=argv[1];bool cap=std::atoi(argv[2]);Output o;o.options_.limitDisplayModeResolutions=cap;
 Catalog a{mode(1920,1080)},b{mode(1920,1080),mode(2560,1440)};
 DXGI_MODE_DESC1 target=ConvertDisplayMode(mode(2560,1440));HRESULT expected=S_OK;DXGI_MODE_DESC1 wanted=target;
 if(scenario=="stable"){o.steps={{false,b},{true,b}};}
 else if(scenario=="shrink"){o.steps={{false,b},{true,a}};wanted=ConvertDisplayMode(a[0]);}
 else if(scenario=="shrink-empty"){o.steps={{false,b},{true,{}}};expected=DXGI_ERROR_NOT_FOUND;}
 else if(scenario=="count-empty"){o.steps={{false,{}}};expected=DXGI_ERROR_NOT_FOUND;}
 else if(scenario=="growth"){o.steps={{false,a},{true,b},{false,b},{true,b}};}
 else if(scenario=="growth-shrink"){o.steps={{false,a},{true,b},{false,b},{true,a}};wanted=ConvertDisplayMode(a[0]);}
 else if(scenario=="growth-retry-error"){o.steps={{false,a},{true,b},{false,b,E_FAIL,2}};expected=E_FAIL;}
 else if(scenario=="growth-retry-empty"){o.steps={{false,a},{true,b},{false,{}}};expected=DXGI_ERROR_NOT_FOUND;}
 else if(scenario=="count-error"){o.steps={{false,b,E_FAIL,2}};expected=E_FAIL;}
 else if(scenario=="count-more-data"){o.steps={{false,b,DXGI_ERROR_MORE_DATA,2}};expected=DXGI_ERROR_MORE_DATA;}
 else if(scenario=="fill-error"){o.steps={{false,b},{true,a,E_FAIL,1}};expected=E_FAIL;}
 else if(scenario=="persistent-growth"){
  o.steps={{false,catalog(1)},{true,catalog(2)},{false,catalog(2)},{true,catalog(3)},{false,catalog(3)},{true,catalog(4)}};expected=DXGI_ERROR_MORE_DATA;
 }
 else if(scenario=="success-overcapacity"){o.steps={{false,a},{true,a,S_OK,2}};expected=DXGI_ERROR_MORE_DATA;}
 else if(scenario=="fractional"){
  Catalog rates{mode(1920,1080,60),mode(1920,1080,60000,1001),mode(1920,1080,120)};target=ConvertDisplayMode(rates[1]);wanted=target;o.steps={{false,rates},{true,rates}};
 }
 else if(scenario=="fractional-shrink"){
  Catalog rates{mode(1920,1080,60000,1001),mode(1920,1080,120)},one{rates[0]};target=ConvertDisplayMode(rates[0]);wanted=target;o.steps={{false,rates},{true,one}};
 }
 else if(scenario=="cap-growth"){
  auto before=catalog(31),after=catalog(37);target=ConvertDisplayMode(after.back());wanted=target;o.steps={{false,before},{true,after},{false,after},{true,after}};
 }
 else if(scenario=="stereo-empty"){o.steps={{false,b},{true,b}};target.Stereo=true;expected=DXGI_ERROR_NOT_FOUND;}
 else if(scenario=="invalid-pair"){target.Width=0;expected=DXGI_ERROR_INVALID_CALL;}
 else if(scenario=="unknown-format"){target.Format=DXGI_FORMAT_UNKNOWN;expected=DXGI_ERROR_INVALID_CALL;}
 else throw std::runtime_error("unknown scenario");
 DXGI_MODE_DESC1 result{0xdead,0xbeef,{0xcafe,0xbabe},99,98,97,true};auto original=key(result);
 HRESULT hr=o.FindClosestMatchingMode1(&target,&result,nullptr);
 check(hr==expected,"unexpected HRESULT");check(o.cursor==o.steps.size(),"enumeration stopped before required result");
 if(expected==S_OK)check(key(result)==key(wanted),"incorrect closest mode (or phantom/truncated entry)");
 else check(key(result)==original,"failure changed caller output");
 if(scenario=="cap-growth")check(o.counts.back()==(cap?32u:37u),"profile cap not preserved");
 printf("PASS %s cap=%d calls=%zu\n",scenario.c_str(),cap,o.cursor);return 0;
 }catch(const std::exception&e){fprintf(stderr,"FAIL %s cap=%s: %s\n",argc>1?argv[1]:"?",argc>2?argv[2]:"?",e.what());return 1;}}
