#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <cstdio>
#include <cstring>
#include <cassert>
using DWORD=uint32_t;using LONG=int32_t;using LONG_PTR=int64_t;using BYTE=unsigned char;using HWND=int64_t;using HKEY=int64_t;
struct RECT { LONG left,top,right,bottom; };
struct MONITORINFO { DWORD cbSize;RECT rcMonitor,rcWork;DWORD dwFlags; };
constexpr LONG_PTR WS_CHILD=0x40000000,WS_MAXIMIZE=0x1000000,WS_POPUP=0x80000000,WS_OVERLAPPEDWINDOW=0x00cf0000;
constexpr LONG_PTR WS_EX_WINDOWEDGE=0x100,WS_EX_CLIENTEDGE=0x200,WS_EX_DLGMODALFRAME=1,WS_EX_STATICEDGE=0x20000,WS_EX_TOPMOST=8;
constexpr int GWL_STYLE=-16,GWL_EXSTYLE=-20,GA_ROOT=2,GW_OWNER=4,MONITOR_DEFAULTTONEAREST=2;
constexpr HWND HWND_NOTOPMOST=-2,HWND_TOPMOST=-1;constexpr int SWP_FRAMECHANGED=32,SWP_NOACTIVATE=16;
constexpr HKEY HKEY_CURRENT_USER=1;constexpr DWORD KEY_QUERY_VALUE=1,ERROR_SUCCESS=0,ERROR_FILE_NOT_FOUND=2,ERROR_PATH_NOT_FOUND=3,REG_SZ=1;
static const wchar_t *opt=L"1",*path=L"C:\\games\\Game\\DarkSoulsIII.exe";static DWORD pid=123,tid=7,self_tid=7,error;
static RECT rect={40,90,1960,1170},monitor={-3840,-120,0,2040};static LONG_PTR style=WS_OVERLAPPEDWINDOW,ex=WS_EX_WINDOWEDGE;
static bool alive=true,iconic=false,owned=false,is_child=false,capture=false,malformed=false,reg_error=false,reenter=false,override_style=false,override_geometry=false,invalid_client=false,client_error=false;
static int writes,positions,fail_write,fail_position;static int nested_result;
DWORD GetEnvironmentVariableW(const wchar_t *,wchar_t *out,DWORD cap){auto n=wcslen(opt);if(n<cap)wcscpy(out,opt);return n<cap?n:n+1;}
DWORD GetModuleFileNameW(void *,wchar_t *out,DWORD cap){auto n=wcslen(path);if(n<cap)wcscpy(out,path);return n<cap?n:cap;}
int _wcsicmp(const wchar_t *a,const wchar_t *b){while(*a&&*b){if(towlower(*a)!=towlower(*b))return 1;a++;b++;}return *a!=*b;}
DWORD GetWindowThreadProcessId(HWND h,DWORD *p){*p=pid;return h==1?tid:0;}DWORD GetCurrentProcessId(){return 123;}DWORD GetCurrentThreadId(){return self_tid;}
bool IsWindow(HWND){return alive;}HWND GetAncestor(HWND h,int){return is_child?2:h;}HWND GetWindow(HWND,int){return owned?2:0;}bool IsIconic(HWND){return iconic;}
void SetLastError(DWORD e){error=e;}DWORD GetLastError(){return error;}
LONG_PTR GetWindowLongPtrW(HWND,int i){return i==GWL_STYLE?style:ex;}
LONG_PTR SetWindowLongPtrW(HWND,int,LONG_PTR);
bool GetWindowRect(HWND,RECT *r){*r=rect;return true;}bool GetClientRect(HWND,RECT *r){if(client_error)return false;*r={0,0,rect.right-rect.left-(invalid_client?1:0),rect.bottom-rect.top};return true;}
int MonitorFromWindow(HWND,int){return 1;}bool GetMonitorInfoW(int,MONITORINFO *m){m->rcMonitor=monitor;return true;}
bool EqualRect(const RECT *a,const RECT *b){return !memcmp(a,b,sizeof(*a));}
bool SetWindowPos(HWND,HWND after,int x,int y,int w,int h,int){positions++;if(positions==fail_position)return false;rect={x,y,x+w,y+h};if(override_geometry)rect.left++;if(after==HWND_NOTOPMOST)ex&=~WS_EX_TOPMOST;else ex|=WS_EX_TOPMOST;return true;}
LONG RegOpenKeyExW(HKEY,const wchar_t *,int,DWORD,HKEY *k){if(reg_error)return 5;if(!capture&&!malformed)return ERROR_FILE_NOT_FOUND;*k=2;return 0;}
LONG RegQueryValueExW(HKEY,const wchar_t *,void *,DWORD *type,BYTE *data,DWORD *bytes){*type=REG_SZ;auto *v=reinterpret_cast<wchar_t *>(data);v[0]=capture?L'Y':L'N';v[1]=0;*bytes=2*sizeof(wchar_t);if(malformed)*bytes=1;return 0;}void RegCloseKey(HKEY){}
#include "../source/src/util/ds3_borderless.hpp"
LONG_PTR SetWindowLongPtrW(HWND h,int i,LONG_PTR value){writes++;if(writes==fail_write){error=5;return 0;}auto &field=i==GWL_STYLE?style:ex;auto before=field;field=value;if(reenter){reenter=false;nested_result=(int)dxmt::wsi::applyDS3Borderless(h,true);}if(override_style&&i==GWL_STYLE)field|=0x08000000;return before;}
void reset(){opt=L"1";path=L"C:\\games\\Game\\DarkSoulsIII.exe";pid=123;tid=self_tid=7;error=0;rect={40,90,1960,1170};monitor={-3840,-120,0,2040};style=WS_OVERLAPPEDWINDOW;ex=WS_EX_WINDOWEDGE;alive=true;iconic=owned=is_child=capture=malformed=reg_error=reenter=override_style=override_geometry=invalid_client=client_error=false;writes=positions=fail_write=fail_position=0;nested_result=-1;}
using R=dxmt::wsi::DS3BorderlessResult;
int main(){unsigned cases=0;auto expect=[&](R r){assert(dxmt::wsi::applyDS3Borderless(1,true)==r);cases++;};
 reset();opt=L"";expect(R::Disabled);assert(!writes);
 reset();opt=L"11";expect(R::Disabled);assert(!writes);
 reset();path=L"C:\\games\\Game\\Other.exe";expect(R::Disabled);
 reset();path=L"C:\\games\\DarkSoulsIII.exe";expect(R::Disabled);
 reset();assert(dxmt::wsi::applyDS3Borderless(1,false)==R::Disabled);cases++;
 reset();pid=999;expect(R::Refused);assert(!writes);
 reset();self_tid++;expect(R::Deferred);assert(!writes);
 reset();iconic=true;expect(R::Refused);assert(!writes);
 reset();is_child=true;expect(R::Refused);assert(!writes);
 reset();owned=true;expect(R::Refused);
 reset();capture=true;expect(R::Refused);assert(!writes);
 reset();malformed=true;expect(R::Refused);
 reset();reg_error=true;expect(R::Refused);
 reset();monitor.right=monitor.left;expect(R::Refused);
 reset();monitor.left=INT32_MIN;monitor.right=INT32_MAX;expect(R::Refused);
 reset();monitor.right=20000;expect(R::Refused);
 reset();expect(R::Applied);assert(EqualRect(&rect,&monitor)&&!(style&WS_OVERLAPPEDWINDOW)&&!(ex&WS_EX_TOPMOST));int count=writes;expect(R::Unchanged);assert(writes==count);
 reset();path=L"C:\\games\\GAME\\darkSoulsIII.EXE";expect(R::Applied);
 reset();reenter=true;expect(R::Applied);assert(nested_result==(int)R::Deferred);
 reset();fail_write=1;expect(R::Failed);assert(style==WS_OVERLAPPEDWINDOW&&ex==WS_EX_WINDOWEDGE&&rect.left==40);
 reset();fail_write=2;expect(R::Failed);assert(style==WS_OVERLAPPEDWINDOW&&ex==WS_EX_WINDOWEDGE);
 reset();fail_position=1;expect(R::Failed);assert(rect.left==40&&style==WS_OVERLAPPEDWINDOW);
 reset();override_style=true;expect(R::RollbackFailed);assert(writes==2&&style&0x08000000);
 reset();override_geometry=true;expect(R::RollbackFailed);assert(positions==1&&rect.left==monitor.left+1);
 reset();invalid_client=true;expect(R::Failed);assert(rect.left==40);
 reset();uint32_t rw=1920,rh=1080;assert(dxmt::wsi::prepareDS3Borderless(1,true,rw,rh)==R::Applied&&rw==1920&&rh==1080);cases++;
 reset();rw=rh=0;assert(dxmt::wsi::prepareDS3Borderless(1,true,rw,rh)==R::Applied&&rw==1920&&rh==1080);cases++;
 reset();rw=rh=0;self_tid++;assert(dxmt::wsi::prepareDS3Borderless(1,true,rw,rh)==R::Deferred&&!rw&&!rh);cases++;
 reset();rect=monitor;rw=rh=0;assert(dxmt::wsi::prepareDS3Borderless(1,true,rw,rh,1920,1080)==R::Applied&&rw==1920&&rh==1080);cases++;
 reset();rect=monitor;rw=rh=0;opt=L"";assert(dxmt::wsi::prepareDS3Borderless(1,true,rw,rh,1920,1080)==R::Disabled&&!rw&&!rh);cases++;
 reset();rw=rh=0;client_error=true;assert(dxmt::wsi::prepareDS3Borderless(1,true,rw,rh)==R::Refused&&!writes&&!rw&&!rh);cases++;
 printf("{\"cases\":%u,\"passed\":true,\"wineStarted\":false}\n",cases);return 0;
}
