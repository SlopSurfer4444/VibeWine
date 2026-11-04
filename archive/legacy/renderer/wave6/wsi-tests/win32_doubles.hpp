#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using LONG = int32_t;
using DWORD = uint32_t;
using UINT = uint32_t;
using BOOL = int;
using WCHAR = wchar_t;
using HANDLE = void *;
using HWND = void *;
using HMONITOR = void *;
using ULONG_PTR = uintptr_t;
using LONG_PTR = intptr_t;
using ATOM = uint16_t;
using NTSTATUS = LONG;
constexpr BOOL FALSE = 0, TRUE = 1;
constexpr LONG GWL_STYLE = -16, GWL_EXSTYLE = -20;
constexpr LONG WS_VISIBLE = 0x10000000, WS_OVERLAPPEDWINDOW = 0x00cf0000;
constexpr LONG WS_EX_TOPMOST = 8, WS_EX_OVERLAPPEDWINDOW = 0x300;
constexpr UINT SWP_NOSIZE = 1, SWP_NOMOVE = 2, SWP_NOACTIVATE = 0x10;
constexpr UINT SWP_FRAMECHANGED = 0x20, SWP_SHOWWINDOW = 0x40;
const HWND HWND_TOPMOST = reinterpret_cast<HWND>(uintptr_t(-1));
const HWND HWND_NOTOPMOST = reinterpret_cast<HWND>(uintptr_t(-2));
constexpr DWORD DM_PELSWIDTH = 0x80000, DM_PELSHEIGHT = 0x100000;
constexpr DWORD DM_BITSPERPEL = 0x40000, DM_DISPLAYFREQUENCY = 0x400000;
constexpr DWORD ENUM_CURRENT_SETTINGS = DWORD(-1), ENUM_REGISTRY_SETTINGS = DWORD(-2);
constexpr DWORD CDS_FULLSCREEN = 4, MONITOR_DEFAULTTOPRIMARY = 1;
constexpr LONG DISP_CHANGE_SUCCESSFUL = 0;
constexpr DWORD ERROR_SUCCESS = 0;
constexpr UINT D3DKMT_ESCAPE_SET_PRESENT_RECT_WINE = 101;
struct RECT { LONG left, top, right, bottom; };
struct POINT { LONG x, y; };
struct MONITORINFO { DWORD cbSize; RECT rcMonitor, rcWork; DWORD dwFlags; };
struct MONITORINFOEXW : MONITORINFO { WCHAR szDevice[32]; };
struct DEVMODEW {
  WCHAR dmDeviceName[32]{};
  uint16_t dmSpecVersion{}, dmDriverVersion{}, dmSize{}, dmDriverExtra{};
  DWORD dmFields{};
  LONG dmPositionX{}, dmPositionY{};
  DWORD dmBitsPerPel{}, dmPelsWidth{}, dmPelsHeight{}, dmDisplayFlags{}, dmDisplayFrequency{};
};
struct D3DKMT_ESCAPE {
  UINT Type{}; void *pPrivateDriverData{}; UINT PrivateDriverDataSize{}; DWORD hContext{};
};
inline DWORD HandleToUlong(HWND h) { return DWORD(reinterpret_cast<uintptr_t>(h)); }

namespace fake {
const HWND hwnd = reinterpret_cast<HWND>(uintptr_t(0x123));
const HMONITOR monitor = reinterpret_cast<HMONITOR>(uintptr_t(0x456));
struct Window {
  bool alive = true;
  LONG style = WS_VISIBLE | WS_OVERLAPPEDWINDOW | 0x100;
  LONG exstyle = WS_EX_OVERLAPPEDWINDOW | 0x2000;
  RECT rect = {40, 70, 840, 670};
  RECT present = {};
  std::map<std::wstring, HANDLE> props;
};
inline Window window;
inline DWORD last_error;
inline std::string failure;
inline std::string additional_failure;
inline int fail_occurrence = 1, fail_limit = 1, matches;
inline bool failed;
inline bool malformed_devmode;
inline int enum_count, display_changes;
inline int warnings;
inline std::vector<std::string> events;
inline std::function<void(const std::string &)> after_call;
inline bool eq(RECT a, RECT b) {
  return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}
inline bool empty(RECT a) { return eq(a, RECT{}); }
inline void reset() {
  window = Window{}; last_error = 0; failure.clear(); additional_failure.clear(); fail_occurrence = fail_limit = 1;
  matches = 0; failed = false; malformed_devmode = false; enum_count = display_changes = warnings = 0;
  events.clear(); after_call = {};
}
inline bool trip(const char *name) {
  events.emplace_back(name);
  bool result = (failure == name && ++matches >= fail_occurrence && matches < fail_occurrence + fail_limit) ||
    additional_failure == name;
  if (result) { failed = true; last_error = 5; }
  return result;
}
inline void post(const char *name) { if (after_call) after_call(name); }
inline bool valid(HWND h) { return h == hwnd && window.alive; }
inline void require(bool ok, const std::string &why) {
  if (!ok) throw std::runtime_error(why);
}
inline void inject(std::string name, int occurrence = 1, int limit = 1) {
  failure = std::move(name); fail_occurrence = occurrence; fail_limit = limit; matches = 0; failed = false;
}
inline void replacement() { window = Window{}; window.style = 0x117; window.exstyle = 0x29; }
}
inline void SetLastError(DWORD value) { fake::last_error = value; }
inline DWORD GetLastError() { return fake::last_error; }
inline BOOL IsWindow(HWND h) { return fake::valid(h); }
inline LONG GetWindowLongW(HWND h, int index) {
  const char *name = index == GWL_STYLE ? "get_style" : "get_exstyle";
  if (!fake::valid(h) || fake::trip(name)) { SetLastError(5); return 0; }
  LONG value = index == GWL_STYLE ? fake::window.style : fake::window.exstyle;
  fake::post(name); return value;
}
inline LONG SetWindowLongW(HWND h, int index, LONG value) {
  const char *name = index == GWL_STYLE ? "set_style" : "set_exstyle";
  if (!fake::valid(h) || fake::trip(name)) { SetLastError(5); return 0; }
  LONG &slot = index == GWL_STYLE ? fake::window.style : fake::window.exstyle;
  LONG old = slot; slot = value; fake::post(name); return old;
}
inline LONG_PTR GetWindowLongPtrW(HWND h, int index) { return GetWindowLongW(h, index); }
inline LONG_PTR SetWindowLongPtrW(HWND h, int index, LONG_PTR value) {
  return SetWindowLongW(h, index, LONG(value));
}
inline BOOL GetWindowRect(HWND h, RECT *out) {
  if (!fake::valid(h) || fake::trip("get_rect")) return FALSE;
  *out = fake::window.rect; return TRUE;
}
inline BOOL GetClientRect(HWND h, RECT *out) { return GetWindowRect(h, out); }
inline BOOL SetRect(RECT *r, int l, int t, int right, int bottom) { *r = {l,t,right,bottom}; return TRUE; }
inline BOOL OffsetRect(RECT *r, int x, int y) {
  r->left += x; r->right += x; r->top += y; r->bottom += y; return TRUE;
}
inline BOOL AdjustWindowRectEx(RECT *, DWORD, BOOL, DWORD) { return TRUE; }
inline BOOL SetWindowPos(HWND h, HWND order, int x, int y, int width, int height, UINT flags) {
  if (!fake::valid(h) || fake::trip("position")) return FALSE;
  RECT &r = fake::window.rect;
  int w = flags & SWP_NOSIZE ? r.right-r.left : width;
  int he = flags & SWP_NOSIZE ? r.bottom-r.top : height;
  if (!(flags & SWP_NOMOVE)) { r.left = x; r.top = y; }
  r.right = r.left + w; r.bottom = r.top + he;
  if (order == HWND_TOPMOST) fake::window.exstyle |= WS_EX_TOPMOST;
  if (order == HWND_NOTOPMOST) fake::window.exstyle &= ~WS_EX_TOPMOST;
  if (flags & SWP_SHOWWINDOW) fake::window.style |= WS_VISIBLE;
  fake::post("position"); return TRUE;
}
inline BOOL MoveWindow(HWND h, int x, int y, int w, int he, BOOL) {
  return SetWindowPos(h, nullptr, x, y, w, he, SWP_NOACTIVATE);
}
inline BOOL SetPropW(HWND h, const WCHAR *name, HANDLE token) {
  if (!fake::valid(h) || fake::trip("set_prop")) return FALSE;
  fake::window.props[name] = token; return TRUE;
}
inline HANDLE GetPropW(HWND h, const WCHAR *name) {
  if (!fake::valid(h)) return nullptr;
  auto it = fake::window.props.find(name);
  return it == fake::window.props.end() ? nullptr : it->second;
}
inline HANDLE RemovePropW(HWND h, const WCHAR *name) {
  if (!fake::valid(h) || fake::trip("remove_prop")) { SetLastError(5); return nullptr; }
  HANDLE old = GetPropW(h,name);
  if (fake::valid(h)) fake::window.props.erase(name);
  fake::post("remove_prop");
  return old;
}
inline DWORD GetWindowThreadProcessId(HWND h, DWORD *pid) {
  if (pid) *pid = 17;
  return fake::valid(h) ? 18 : 0;
}
inline NTSTATUS D3DKMTEscape(D3DKMT_ESCAPE *escape) {
  if (fake::trip("present_rect")) return -1;
  fake::require(escape->PrivateDriverDataSize == sizeof(RECT), "bad escape RECT size");
  fake::require(escape->hContext == HandleToUlong(fake::hwnd), "bad escape owner");
  if (!fake::window.alive) return -1;
  fake::window.present = *static_cast<RECT *>(escape->pPrivateDriverData);
  fake::post("present_rect"); return 0;
}
inline BOOL GetMonitorInfoW(HMONITOR h, MONITORINFO *info) {
  if (h != fake::monitor || fake::trip("monitor_info")) return FALSE;
  fake::require(info->cbSize == sizeof(MONITORINFOEXW), "monitor info cbSize");
  std::wcscpy(static_cast<MONITORINFOEXW *>(info)->szDevice, L"DISPLAY1"); return TRUE;
}
inline BOOL EnumDisplaySettingsExW(const WCHAR *name, DWORD mode, DEVMODEW *out, DWORD) {
  ++fake::enum_count;
  bool clean = out->dmSize == sizeof(*out) && out->dmDriverExtra == 0 &&
    out->dmFields == 0 && out->dmPelsWidth == 0 && out->dmPelsHeight == 0 &&
    out->dmBitsPerPel == 0 && out->dmDisplayFrequency == 0;
  if (!clean) { fake::malformed_devmode = true; return FALSE; }
  if (!name || !*name || fake::trip(mode == ENUM_CURRENT_SETTINGS ? "enum_current" : "enum_registry")) return FALSE;
  out->dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
  out->dmPelsWidth = mode == ENUM_REGISTRY_SETTINGS ? 1920 : 1280;
  out->dmPelsHeight = mode == ENUM_REGISTRY_SETTINGS ? 1080 : 720;
  out->dmBitsPerPel = 32; out->dmDisplayFrequency = 60; return TRUE;
}
inline BOOL EnumDisplaySettingsW(const WCHAR *name, DWORD mode, DEVMODEW *out) {
  return EnumDisplaySettingsExW(name, mode, out, 0);
}
inline LONG ChangeDisplaySettingsExW(const WCHAR *name, DEVMODEW *mode, HWND, DWORD, void *) {
  ++fake::display_changes;
  fake::require(name && std::wcscmp(name, L"DISPLAY1") == 0, "display restore loses stable device name");
  if (fake::trip(mode ? "display_set" : "display_restore")) return -1;
  return DISP_CHANGE_SUCCESSFUL;
}
inline HMONITOR MonitorFromPoint(POINT, DWORD) { return fake::monitor; }
inline HWND GetForegroundWindow() { return fake::hwnd; }
inline BOOL IsIconic(HWND) { return FALSE; }

namespace dxmt {
struct Logger {
  template<class... T> static void err(T&&...) { ++fake::warnings; }
  template<class... T> static void warn(T&&...) { ++fake::warnings; }
  template<class... T> static void info(T&&...) {}
};
namespace str { template<class... T> std::string format(T&&...) { return {}; } }
namespace wsi {
bool restoreDisplayMode(const WCHAR *device_name);
struct WsiRational { uint32_t numerator, denominator; };
struct WsiMode { uint32_t width, height; WsiRational refreshRate; uint32_t bitsPerPixel; bool interlaced; };
inline bool getDesktopCoordinates(HMONITOR h, RECT *out) {
  if (h != fake::monitor || fake::trip("desktop_rect")) return false;
  *out = {0,0,1920,1080}; return true;
}
inline bool getDisplayName(HMONITOR h, WCHAR (&name)[32]) {
  if (h != fake::monitor || fake::trip("display_name")) return false;
  std::wcscpy(name,L"DISPLAY1"); return true;
}
}
}
