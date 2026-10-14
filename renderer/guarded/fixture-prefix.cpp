#include <atomic>
#include <mutex>
#include <functional>
#include <stdexcept>
#include <iostream>
#include <thread>
#include <cassert>
using HRESULT=int; using HMONITOR=int; using HWND=int; using BOOL=int;
constexpr int S_OK=0, DXGI_ERROR_INVALID_CALL=-1, DXGI_ERROR_NOT_CURRENTLY_AVAILABLE=-2, DXGI_STATUS_MODE_CHANGE_IN_PROGRESS=2, TRUE=1;
#define FAILED(x) ((x)<0)
#define WARN(...) ((void)0)
#define ERR(...) ((void)0)
#define STDMETHODCALLTYPE
struct Ratio {unsigned Numerator=0, Denominator=1;};
struct DXGI_MODE_DESC {unsigned Width=0, Height=0; Ratio RefreshRate; int Format=0, ScanlineOrdering=0, Scaling=0;};
using DXGI_MODE_DESC1=DXGI_MODE_DESC;
struct Base {virtual HRESULT ResizeTarget(const DXGI_MODE_DESC*)=0;};
namespace dxmt {struct mutex {std::atomic<bool> locked=false; void lock(){if(locked.exchange(true))throw std::runtime_error("nonrecursive reacquire");} void unlock(){locked=false;}};}
namespace wsi {std::function<void()> callback; bool valid=true, success=true; int calls=0; bool isWindow(int){return valid;} int getWindowMonitor(int){return 7;} void invoke(){++calls;if(callback)callback();} bool leaveFullscreenMode(int,int*,bool){invoke();return success;} void resizeWindow(int,int*,unsigned,unsigned){invoke();} void updateFullscreenWindow(int,int,bool){invoke();}}
struct Target {void* ptr(){return nullptr;} Target& operator=(std::nullptr_t){return *this;}};
struct Presenter {void changeGammaRamp(void*){}};
