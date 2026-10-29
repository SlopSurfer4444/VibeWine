// SPDX-License-Identifier: MIT
#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <unordered_map>
#include <iostream>
#define WINAPI
#define IID_PPV_ARGS(out) 0,out
#define FAILED(hr) ((hr)<0)
#define ERR(...) ((void)0)
#define WARN(...) ((void)0)
namespace dxmt {
using HRESULT=int32_t;using UINT=unsigned;using HMODULE=void*;
constexpr HRESULT S_OK=0,S_FALSE=1,E_FAIL=int32_t(0x80004005),E_INVALIDARG=int32_t(0x80070057),E_OUTOFMEMORY=int32_t(0x8007000e);
enum D3D_FEATURE_LEVEL{D3D_FEATURE_LEVEL_12_1=0xc100,D3D_FEATURE_LEVEL_12_0=0xc000,D3D_FEATURE_LEVEL_11_1=0xb100,D3D_FEATURE_LEVEL_11_0=0xb000,D3D_FEATURE_LEVEL_10_1=0xa100,D3D_FEATURE_LEVEL_10_0=0xa000,D3D_FEATURE_LEVEL_9_3=0x9300,D3D_FEATURE_LEVEL_9_2=0x9200,D3D_FEATURE_LEVEL_9_1=0x9100};
enum D3D_DRIVER_TYPE{D3D_DRIVER_TYPE_UNKNOWN,D3D_DRIVER_TYPE_HARDWARE};
constexpr int WMTGPUFamilyApple7=7;
struct MTLD3DError{};
struct Ref{unsigned refs=1;virtual~Ref()=default;void AddRef(){++refs;}void Release(){if(!--refs)delete this;}};
template<class T>struct Com{
 T*p=nullptr;Com()=default;Com(std::nullptr_t){}Com(T*ptr,bool add=true):p(ptr){if(p&&add)p->AddRef();}~Com(){if(p)p->Release();}
 Com(const Com&c):Com(c.p){}Com(Com&&c):p(c.p){c.p=nullptr;}
 T*operator->()const{return p;}T**operator&(){assert(!p);return &p;}T*ptr()const{return p;}T*ref(){if(p)p->AddRef();return p;}
};
template<class T>void InitReturnPtr(T**p){if(p)*p=nullptr;}
struct Logger{template<class...T>static void info(T...){}template<class...T>static void err(T...){}};
namespace str{template<class...T>const char*format(T...){return "";}}
struct Config{static Config&getInstance(){static Config c;return c;}template<class T>T getOption(const char*,const char*){return{};}};
struct MetalDevice{bool supportsFamily(int){return true;}};
struct IDXGIFactory;
struct IDXGIAdapter:Ref{
 template<class T>HRESULT QueryInterface(int,T**out){*out=static_cast<T*>(this);AddRef();return S_OK;}
 HRESULT GetParent(int,IDXGIFactory**);
};
struct IMTLDXGIAdapter:IDXGIAdapter{MetalDevice GetMTLDevice(){return{};}};
struct ID3D11DeviceContext:Ref{};
static bool fail_low=false,fail_high=false,fail_swap=false;static unsigned low_live=0,device_live=0;
struct Low:Ref{struct Guard{Guard(){++low_live;}~Guard(){--low_live;}}guard;Low(){if(fail_low)throw std::bad_alloc{};}};
struct ID3D11Device:Ref{
 Com<Low>low;ID3D11Device(Com<Low>l):low(std::move(l)){++device_live;}~ID3D11Device(){--device_live;}
 HRESULT QueryInterface(int,ID3D11Device**out){*out=this;AddRef();return S_OK;}
 D3D_FEATURE_LEVEL GetFeatureLevel(){return D3D_FEATURE_LEVEL_11_0;}
 void GetImmediateContext(ID3D11DeviceContext**out){*out=new ID3D11DeviceContext;}
};
struct IDXGISwapChain:Ref{};struct DXGI_SWAP_CHAIN_DESC{};
struct IDXGIFactory:Ref{
 HRESULT EnumAdapters(unsigned,IDXGIAdapter**);
 HRESULT CreateSwapChain(ID3D11Device*,DXGI_SWAP_CHAIN_DESC*,IDXGISwapChain**out){if(fail_swap)throw std::bad_alloc{};*out=new IDXGISwapChain;return S_OK;}
};
static IDXGIFactory factory;static IMTLDXGIAdapter adapter;
HRESULT IDXGIFactory::EnumAdapters(unsigned,IDXGIAdapter**out){*out=&adapter;adapter.AddRef();return S_OK;}
HRESULT IDXGIAdapter::GetParent(int,IDXGIFactory**out){*out=&factory;factory.AddRef();return S_OK;}
static HRESULT CreateDXGIFactory1(int,IDXGIFactory**out){*out=&factory;factory.AddRef();return S_OK;}
struct LowArgs{MetalDevice device;};
static Com<Low>CreateDXMTDevice(LowArgs){return Com<Low>(new Low,false);}
static Com<ID3D11Device>CreateD3D11Device(Com<Low>low,IMTLDXGIAdapter*,D3D_FEATURE_LEVEL,unsigned){if(fail_high)throw std::bad_alloc{};return Com<ID3D11Device>(new ID3D11Device(std::move(low)),false);}
#include "creation.hpp"
}
int main(){using namespace dxmt;
 for(unsigned stage=0;stage<2;++stage){fail_low=stage==0;fail_high=stage==1;ID3D11Device*device=(ID3D11Device*)1;auto hr=D3D11CoreCreateDevice(&factory,&adapter,0,nullptr,0,&device);assert(hr==E_OUTOFMEMORY&&device==nullptr&&low_live==0&&device_live==0);fail_low=fail_high=false;}
 {fail_swap=true;IDXGISwapChain*swap=(IDXGISwapChain*)1;ID3D11Device*device=(ID3D11Device*)1;ID3D11DeviceContext*context=(ID3D11DeviceContext*)1;D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_12_1;DXGI_SWAP_CHAIN_DESC desc;auto hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,0,&desc,&swap,&device,&level,&context);assert(hr==E_OUTOFMEMORY&&!swap&&!device&&!context&&level==D3D_FEATURE_LEVEL(0)&&low_live==0&&device_live==0);fail_swap=false;}
 {ID3D11Device*device=nullptr;assert(D3D11CoreCreateDevice(&factory,&adapter,0,nullptr,0,&device)==S_OK&&device);device->Release();assert(low_live==0&&device_live==0);}
 std::cout<<"PASS actual extern-C CoreCreateDevice/DeviceAndSwapChain OOM HRESULT/null/feature sentinels and partial member/device cleanup, healthy creation\\n";
}
