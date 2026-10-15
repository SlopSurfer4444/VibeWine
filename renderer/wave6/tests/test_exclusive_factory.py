from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
s=(root/'source/src/d3d11/d3d11_swapchain.cpp').read_text()
start=s.index('HRESULT\nCreateSwapChain(');end=s.index('  Com<IMTLDXGIDevice> layer_factory;',start)
prefix=s[start:end]
assert prefix.index('wsi::ds3BorderlessEnabled')<len(prefix)
assert 'DXGI_ERROR_NOT_CURRENTLY_AVAILABLE' in prefix
fixture=r'''
#define main policy_fixture_main
#include "policy.cpp"
#undef main
namespace wsi=dxmt::wsi;
using HRESULT=int32_t;
constexpr HRESULT S_OK=0,E_FAIL=-3,DXGI_ERROR_NOT_CURRENTLY_AVAILABLE=-1,DXGI_ERROR_INVALID_CALL=-2;
#define ERR(x) ((void)0)
struct IDXGIFactory1 {};struct MTLD3D11Device {};struct IDXGISwapChain1 {};
struct DXGI_SWAP_CHAIN_DESC1 {uint32_t Width=1920,Height=1080;};
struct DXGI_SWAP_CHAIN_FULLSCREEN_DESC {bool Windowed=true;};
void InitReturnPtr(IDXGISwapChain1 **p){*p=nullptr;}
int GetCurrentProcess(){return 1;}DWORD GetProcessId(int){return GetCurrentProcessId();}
'''+prefix+r'''
  return S_OK; // stand-in for the untouched post-admission construction path
}
int main(){unsigned cases=0;IDXGIFactory1 factory;MTLD3D11Device device;DXGI_SWAP_CHAIN_DESC1 desc;DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen;IDXGISwapChain1 *out;
 auto call=[&](const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *full){out=reinterpret_cast<IDXGISwapChain1 *>(123);return CreateSwapChain(&factory,&device,1,&desc,full,&out);};
 reset();fullscreen.Windowed=false;assert(call(&fullscreen)==DXGI_ERROR_NOT_CURRENTLY_AVAILABLE&&!out&&!writes&&!positions);cases++;
 reset();fullscreen.Windowed=true;assert(call(&fullscreen)==S_OK&&!out&&!writes&&!positions);cases++;
 reset();assert(call(nullptr)==S_OK&&!out);cases++;
 reset();opt=L"";fullscreen.Windowed=false;assert(call(&fullscreen)==S_OK&&!out);cases++;
 reset();path=L"C:\\games\\Game\\Other.exe";assert(call(&fullscreen)==S_OK&&!out);cases++;
 reset();opt=L"11";assert(call(&fullscreen)==S_OK&&!out);cases++;
 reset();pid=999;assert(call(&fullscreen)==E_FAIL&&!out&&!writes&&!positions);cases++;
 printf("{\"cases\":%u,\"passed\":true,\"wineStarted\":false}\n",cases);
}
'''
p=root/'tests/exclusive-factory-generated.cpp';p.write_text(fixture);exe=root/'tests/exclusive-factory-test'
subprocess.run(['clang++','-std=c++20','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
r=subprocess.run([str(exe)],capture_output=True,text=True,check=True);print(r.stdout,end='');(root/'tests/exclusive-factory-result.json').write_text(r.stdout)
