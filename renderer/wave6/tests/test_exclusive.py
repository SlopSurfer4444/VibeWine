from pathlib import Path
import subprocess,json
root=Path(__file__).resolve().parents[1]
s=(root/'source/src/d3d11/d3d11_swapchain.cpp').read_text()
start=s.index('  SetFullscreenState(BOOL Fullscreen, IDXGIOutput *pTarget) final {')
end=s.index('\n  HRESULT EnterFullscreenMode',start)
method='  HRESULT\n'+s[start:end]
assert method.index('wsi::ds3BorderlessEnabled')<method.index('Com<IDXGIOutput1> target')
fixture=r'''
#define main policy_fixture_main
#include "policy.cpp"
#undef main
#include "../api-seam.hpp"
namespace wsi=dxmt::wsi;
using BOOL=int;using HRESULT=int32_t;
constexpr HRESULT S_OK=0,DXGI_ERROR_NOT_CURRENTLY_AVAILABLE=-1,DXGI_ERROR_INVALID_CALL=-2;
#define FAILED(hr) ((hr)<0)
#define IID_PPV_ARGS(pp) (pp)
static unsigned queried,entered,left;
struct DXGI_OUTPUT_DESC{int Monitor;};
struct IDXGIOutput1 {HRESULT GetDesc(DXGI_OUTPUT_DESC *d){d->Monitor=1;return S_OK;}};
struct IDXGIOutput {IDXGIOutput1 out;HRESULT QueryInterface(IDXGIOutput1 **p){queried++;*p=&out;return S_OK;}};
template<typename T>struct Com{T *p=nullptr;T **operator&(){return &p;}T *operator->(){return p;}T *ptr(){return p;}};
struct Base {virtual HRESULT SetFullscreenState(BOOL,IDXGIOutput *)=0;};
struct Swapchain:Base {
 uint64_t trace_serial_=1;
 struct {bool Windowed=true;}fullscreen_desc_;
 int monitor_=1;
 HRESULT EnterFullscreenMode(IDXGIOutput1 *){entered++;fullscreen_desc_.Windowed=false;return S_OK;}
 HRESULT LeaveFullscreenMode(){left++;fullscreen_desc_.Windowed=true;return S_OK;}
'''+method+r'''
};
int main(){unsigned cases=0;Swapchain sc;IDXGIOutput output;
 auto restart=[&](){reset();queried=entered=left=0;sc.fullscreen_desc_.Windowed=true;};
 restart();assert(sc.SetFullscreenState(true,&output)==DXGI_ERROR_NOT_CURRENTLY_AVAILABLE);assert(!queried&&!entered&&!left&&sc.fullscreen_desc_.Windowed);cases++;
 restart();sc.fullscreen_desc_.Windowed=false;assert(sc.SetFullscreenState(true,&output)==DXGI_ERROR_NOT_CURRENTLY_AVAILABLE);assert(!queried&&!entered&&!left);cases++;
 restart();assert(sc.SetFullscreenState(false,nullptr)==S_OK);assert(left==1&&!entered&&!queried);cases++;
 restart();sc.fullscreen_desc_.Windowed=false;assert(sc.SetFullscreenState(false,nullptr)==S_OK);assert(left==1&&sc.fullscreen_desc_.Windowed);cases++;
 restart();assert(sc.SetFullscreenState(false,&output)==DXGI_ERROR_INVALID_CALL);assert(!queried&&!entered&&!left);cases++;
 restart();opt=L"";assert(sc.SetFullscreenState(true,&output)==S_OK);assert(queried==1&&entered==1&&!left&&!sc.fullscreen_desc_.Windowed);cases++;
 restart();path=L"C:\\games\\Game\\Other.exe";assert(sc.SetFullscreenState(true,&output)==S_OK);assert(queried==1&&entered==1&&!left);cases++;
 restart();opt=L"11";assert(sc.SetFullscreenState(true,&output)==S_OK);assert(queried==1&&entered==1);cases++;
 printf("{\"cases\":%u,\"passed\":true,\"wineStarted\":false}\n",cases);
}
'''
p=root/'tests/exclusive-generated.cpp';p.write_text(fixture)
exe=root/'tests/exclusive-test'
subprocess.run(['clang++','-std=c++20','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
r=subprocess.run([str(exe)],capture_output=True,text=True,check=True);print(r.stdout,end='');(root/'tests/exclusive-result.json').write_text(r.stdout)
