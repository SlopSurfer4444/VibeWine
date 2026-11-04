
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
HRESULT
CreateSwapChain(
    IDXGIFactory1 *pFactory, MTLD3D11Device *pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1 *pDesc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc, IDXGISwapChain1 **ppSwapChain
) {
  if (ppSwapChain == NULL)
    return DXGI_ERROR_INVALID_CALL;
  InitReturnPtr(ppSwapChain);
  if (pDesc == NULL)
    return DXGI_ERROR_INVALID_CALL;

  DWORD window_process_id;
  GetWindowThreadProcessId(hWnd, &window_process_id);
  if (GetProcessId(GetCurrentProcess()) != window_process_id) {
    ERR("CreateSwapChain: cross-process swapchain not supported yet");
    return E_FAIL;
  }

  // Initial fullscreen descriptors bypass SetFullscreenState. The admitted
  // DS3 borderless route must refuse these before constructing an exclusive
  // swapchain or performing any output/mode/window mutation.
  if (pFullscreenDesc && !pFullscreenDesc->Windowed && wsi::ds3BorderlessEnabled())
    return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;


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
