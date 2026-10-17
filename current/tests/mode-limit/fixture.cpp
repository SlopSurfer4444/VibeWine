#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <set>
#include <tuple>
#include <vector>
#include <stdexcept>
#include <string>
#include <regex>
#include "HEADER_PATH"
using UINT=uint32_t;using HRESULT=int32_t;using DXGI_FORMAT=int;
constexpr HRESULT S_OK=0,DXGI_ERROR_INVALID_CALL=-1,DXGI_ERROR_MORE_DATA=-2;
constexpr int DXGI_FORMAT_UNKNOWN=0,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB=1,DXGI_MODE_SCANLINE_ORDER_UPPER_FIELD_FIRST=1,DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE=0,DXGI_MODE_SCALING_UNSPECIFIED=0;
constexpr UINT DXGI_ENUM_MODES_INTERLACED=1,MTL_DXGI_FORMAT_BACKBUFFER=1;
constexpr bool FALSE=false;
#define FAILED(hr) ((hr)<0)
struct DXGI_RATIONAL{UINT Numerator=0,Denominator=1;};
struct DXGI_MODE_DESC{UINT Width=0,Height=0;DXGI_RATIONAL RefreshRate;DXGI_FORMAT Format=0;int ScanlineOrdering=0,Scaling=0;};
struct DXGI_MODE_DESC1:DXGI_MODE_DESC{bool Stereo=false;};
namespace wsi{
struct WsiMode{UINT width,height,bitsPerPixel;struct{UINT numerator,denominator;}refreshRate;bool interlaced;};
static std::vector<WsiMode> modes;static WsiMode current{},desktop{};static bool hints_valid=true;static unsigned hint_queries=0;
bool getDisplayMode(int,UINT i,WsiMode *m){if(i>=modes.size())return false;*m=modes[i];return true;}
bool getCurrentDisplayMode(int,WsiMode *m){hint_queries++;if(!hints_valid)return false;*m=current;return true;}
bool getDesktopDisplayMode(int,WsiMode *m){hint_queries++;if(!hints_valid)return false;*m=desktop;return true;}
}
struct MTL_DXGI_FORMAT_DESC{UINT Flag;};
HRESULT MTLQueryDXGIFormat(int,DXGI_FORMAT f,MTL_DXGI_FORMAT_DESC &d){d.Flag=f==3?0:MTL_DXGI_FORMAT_BACKBUFFER;return f==4?-1:S_OK;}
UINT GetMonitorFormatBpp(DXGI_FORMAT f){return f==2?64:32;}
// CONVERT
using dxmt::LimitDisplayModeResolutions;
struct Base{virtual HRESULT GetDisplayModeList1(DXGI_FORMAT,UINT,UINT*,DXGI_MODE_DESC1*)=0;virtual HRESULT GetDisplayModeList(DXGI_FORMAT,UINT,UINT*,DXGI_MODE_DESC*)=0;virtual ~Base()=default;};
struct Output:Base{
 struct{bool limitDisplayModeResolutions=true;}options_;
 struct Adapter{int GetMTLDevice(){return 1;}}adapter;Adapter *adapter_=&adapter;int monitor_=1;
// METHOD1
// METHOD0
};
using Key=std::tuple<UINT,UINT,UINT,UINT,int,int,int>;
Key key(const DXGI_MODE_DESC &m){return {m.Width,m.Height,m.RefreshRate.Numerator,m.RefreshRate.Denominator,m.Format,m.ScanlineOrdering,m.Scaling};}
using Size=std::pair<UINT,UINT>;
void check(bool ok,const char *msg){if(!ok)throw std::runtime_error(msg);}
template<class M>std::vector<Key> keys(const std::vector<M>&v){std::vector<Key> r;for(auto &m:v)r.push_back(key(m));return r;}
template<class M>std::set<Size> sizes(const std::vector<M>&v){std::set<Size>r;for(auto&m:v)r.insert({m.Width,m.Height});return r;}
template<bool legacy>using Mode=std::conditional_t<legacy,DXGI_MODE_DESC,DXGI_MODE_DESC1>;
template<bool legacy>HRESULT invoke(Output&o,int fmt,UINT flags,UINT*n,Mode<legacy>*p){if constexpr(legacy)return o.GetDisplayModeList(fmt,flags,n,p);else return o.GetDisplayModeList1(fmt,flags,n,p);}
template<bool legacy>std::vector<Mode<legacy>> enumerate(Output&o,int fmt=1,UINT flags=0){UINT n=999;check(invoke<legacy>(o,fmt,flags,&n,nullptr)==S_OK,"count query failed");std::vector<Mode<legacy>> v(n+1);v[n].Width=0xdead;UINT got=n;check(invoke<legacy>(o,fmt,flags,&got,v.data())==S_OK,"full fill failed");check(got==n&&v[n].Width==0xdead,"count/fill mismatch or overrun");v.resize(n);return v;}
static const std::vector<wsi::WsiMode> synthetic111={// SYNTHETIC111
};
static unsigned cases=0,failures=0;
template<class F>void test(const std::string&name,F fn){try{wsi::modes.clear();wsi::current={};wsi::desktop={};wsi::hints_valid=true;wsi::hint_queries=0;fn();cases++;}catch(const std::exception&e){failures++;fprintf(stderr,"FAIL %s: %s\n",name.c_str(),e.what());}}
void synthetic(unsigned n,unsigned refreshes=1){for(UINT i=0;i<n;i++)for(UINT r=0;r<refreshes;r++)wsi::modes.push_back({800+i*10,600+i*5,32,{60+r*15,1},false});}
template<bool legacy>void suite(){std::string tag=legacy?"legacy ":"v1 ";
 test(tag+"null count",[]{Output o;Mode<legacy> m;check(invoke<legacy>(o,1,0,nullptr,nullptr)==DXGI_ERROR_INVALID_CALL,"null count accepted");check(invoke<legacy>(o,1,0,nullptr,&m)==DXGI_ERROR_INVALID_CALL,"null count with buffer accepted");});
 for(bool enabled:{false,true})test(tag+"buffer protocol "+std::to_string(enabled),[=](){Output o;o.options_.limitDisplayModeResolutions=enabled;wsi::modes=synthetic111;auto full=enumerate<legacy>(o);check(!full.empty(),"synthetic111 empty");for(UINT cap:{0u,1u,UINT(full.size()-1),UINT(full.size()),UINT(full.size()+3)}){std::vector<Mode<legacy>>out(cap+1);out[cap].Width=0xbeef;UINT n=cap;auto hr=invoke<legacy>(o,1,0,&n,out.data());check(hr==(cap<full.size()?DXGI_ERROR_MORE_DATA:S_OK),"capacity HRESULT changed");check(n==(cap<full.size()?cap:UINT(full.size())),"capacity count behavior changed");check(out[cap].Width==0xbeef,"write beyond capacity");for(UINT i=0;i<std::min(cap,UINT(full.size()));i++)check(key(out[i])==key(full[i]),"short fill prefix mismatch");}});
 test(tag+"unknown and unsupported formats",[]{Output o;synthetic(40);for(int f:{0,3,4}){check(enumerate<legacy>(o,f).empty(),"unsupported format returned modes");}check(!wsi::hint_queries,"unsupported format queried hints");});
 for(unsigned count:{0u,1u,31u,32u})test(tag+"at most32 groups "+std::to_string(count),[=](){synthetic(count,3);Output limited,plain;plain.options_.limitDisplayModeResolutions=false;check(keys(enumerate<legacy>(limited))==keys(enumerate<legacy>(plain)),"<=32 dimension groups changed");});
 test(tag+"refresh groups and preferred sizes",[]{synthetic(40,3);for(auto [w,h]:std::vector<Size>{{1920,1080},{2560,1440},{3000,1700},{3500,2000},{7680,4320}})for(UINT r:{60u,120u})wsi::modes.push_back({w,h,32,{r,1},false});wsi::current={3000,1700,32,{60,1},false};wsi::desktop={3500,2000,32,{60,1},false};Output o;auto out=enumerate<legacy>(o);auto chosen=sizes(out);check(chosen.size()==32,"unique groups not limited to32");for(Size size:std::vector<Size>{{1920,1080},{2560,1440},{3000,1700},{3500,2000},{7680,4320}})check(chosen.count(size),"priority group dropped");for(auto size:chosen){unsigned input=0,output=0;for(auto&m:wsi::modes)input+=Size{m.width,m.height}==size;for(auto&m:out)output+=Size{m.Width,m.Height}==size;check(input==output,"refresh variant dropped");}auto first=keys(out);std::reverse(wsi::modes.begin(),wsi::modes.end());check(keys(enumerate<legacy>(o))==first,"input ordering changed sorted output");});
 test(tag+"synthetic111111 limit and off",[]{wsi::modes=synthetic111;wsi::current={1920,1080,32,{60,1},false};wsi::desktop={3840,2160,32,{60,1},false};Output o;auto out=enumerate<legacy>(o);check(sizes(out).size()==32,"synthetic111 not reduced to32 groups");auto chosen=sizes(out);check(chosen.count({1920,1080})&&chosen.count({2560,1440}),"synthetic111 preferred sizes absent");auto largest=*std::max_element(synthetic111.begin(),synthetic111.end(),[](auto&a,auto&b){return uint64_t(a.width)*a.height<uint64_t(b.width)*b.height;});check(chosen.count({largest.width,largest.height}),"synthetic111 largest mode lost");o.options_.limitDisplayModeResolutions=false;wsi::hint_queries=0;auto all=enumerate<legacy>(o);std::vector<Key> expected;for(const auto &m:synthetic111)if(m.bitsPerPixel==32)expected.push_back(key(ConvertDisplayMode(m)));std::sort(expected.begin(),expected.end());check(all.size()==37&&keys(all)==expected&&!wsi::hint_queries,"option off changed enumeration or queried hints");});
 test(tag+"absent hints never invented",[]{synthetic(35);wsi::current={1920,1080,32,{60,1},false};wsi::desktop={2560,1440,32,{60,1},false};Output o;auto chosen=sizes(enumerate<legacy>(o));check(chosen.size()==32&&!chosen.count({1920,1080})&&!chosen.count({2560,1440}),"absent resolution invented");wsi::hints_valid=false;check(sizes(enumerate<legacy>(o))==chosen,"failed query affected selection");});
 test(tag+"bpp and interlace filtering",[]{synthetic(40);wsi::modes.push_back({7000,4000,64,{60,1},false});wsi::modes.push_back({8000,4500,32,{60,1},true});Output o;auto ordinary=sizes(enumerate<legacy>(o));check(!ordinary.count({7000,4000})&&!ordinary.count({8000,4500}),"filtered modes leaked");auto interlaced=sizes(enumerate<legacy>(o,1,DXGI_ENUM_MODES_INTERLACED));check(interlaced.count({8000,4500})&&!interlaced.count({7000,4000}),"interlace flag or bpp filtering wrong");auto high=enumerate<legacy>(o,2);check(high.size()==1&&high[0].Width==7000&&high[0].Format==2,"64bit format filtering wrong");});
}
int main(){suite<false>();suite<true>();
 test("profile exact basename case insensitive",[]{std::regex pattern(R"(PROFILE_REGEX)",std::regex::extended|std::regex::icase);for(const char*p:{R"(C:\games\Game\DarkSoulsIII.exe)",R"(c:\GAMES\darkSOULSiii.EXE)"})check(std::regex_search(p,pattern),"DS3 path profile not matched");for(const char*p:{R"(C:\games\Other.exe)",R"(C:\games\NotDarkSoulsIII.exe)",R"(C:\games\DarkSoulsIII.exe.bak)",R"(C:\games\DarkSoulsIIIaexe)"})check(!std::regex_search(p,pattern),"unrelated executable profile matched");});
 printf("{\"passed\":%s,\"cases\":%u,\"failures\":%u,\"synthetic111_modes\":111,\"wineStarted\":false}\n",failures?"false":"true",cases,failures);return failures?1:0;
}
