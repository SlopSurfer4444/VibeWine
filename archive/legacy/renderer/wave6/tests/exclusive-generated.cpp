
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
  HRESULT
  SetFullscreenState(BOOL Fullscreen, IDXGIOutput *pTarget) final {
    cs2trace::ApiScope api(46,trace_serial_,uint64_t(Fullscreen),(uint64_t)pTarget);
    return api.invoke([&]() -> HRESULT {
    // This admitted DS3 route uses geometric borderless with DXGI windowed.
    // Reject an accidental menu/Alt+Enter request before querying outputs or
    // changing modes. FALSE still takes the normal WSI cleanup path.
    if (Fullscreen && wsi::ds3BorderlessEnabled())
      return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;

    Com<IDXGIOutput1> target;

    if (!Fullscreen && pTarget)
      return DXGI_ERROR_INVALID_CALL;

    if (pTarget) {
      DXGI_OUTPUT_DESC desc;

      HRESULT hr = pTarget->QueryInterface(IID_PPV_ARGS(&target));
      if (FAILED(hr))
        return hr;
      hr = target->GetDesc(&desc);
      if (FAILED(hr))
        return hr;

      if (!fullscreen_desc_.Windowed && Fullscreen && monitor_ != desc.Monitor) {
        HRESULT hr = LeaveFullscreenMode();
        if (FAILED(hr))
          return hr;
      }
    }

    if (fullscreen_desc_.Windowed && Fullscreen)
      return EnterFullscreenMode(target.ptr());
    else if (!Fullscreen)
      // A prior exit can be logically windowed with physical cleanup pending.
      return LeaveFullscreenMode();

    return S_OK;

    });
  };

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
