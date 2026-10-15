#pragma once
#include <cwchar>
#include <cstdint>

/* DS3's windowed swapchain can fill the monitor without DXGI exclusive mode.
 * Every mutation runs on the owning Win32 thread; no external process wait,
 * display-mode switch, display capture request, or persistent topmost flag. */
namespace dxmt::wsi {
enum class DS3BorderlessResult { Disabled, Deferred, Refused, Unchanged, Applied, Failed, RollbackFailed };

inline bool ds3BorderlessEnabled() {
  wchar_t opt[2] = {}, path[32768] = {};
  if (GetEnvironmentVariableW(L"DXMT_DS3_BORDERLESS", opt, 2) != 1 || opt[0] != L'1') return false;
  DWORD len = GetModuleFileNameW(nullptr, path, 32768);
  constexpr wchar_t suffix[] = L"\\Game\\DarkSoulsIII.exe";
  constexpr size_t suffix_len = sizeof(suffix) / sizeof(*suffix) - 1;
  return len > suffix_len && len < 32768 && !_wcsicmp(path + len - suffix_len, suffix);
}

inline bool ds3BorderlessCaptureSafe() {
  const wchar_t *keys[] = {L"Software\\Wine\\Mac Driver", L"Software\\Wine\\AppDefaults\\DarkSoulsIII.exe\\Mac Driver"};
  for (auto name : keys) {
    HKEY key;
    LONG error = RegOpenKeyExW(HKEY_CURRENT_USER, name, 0, KEY_QUERY_VALUE, &key);
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) continue;
    if (error != ERROR_SUCCESS) return false;
    wchar_t value[32] = {}; DWORD bytes = sizeof(value), type = 0;
    error = RegQueryValueExW(key, L"CaptureDisplaysForFullscreen", nullptr, &type, reinterpret_cast<BYTE *>(value), &bytes);
    RegCloseKey(key);
    if (error == ERROR_FILE_NOT_FOUND) continue;
    if (error != ERROR_SUCCESS || type != REG_SZ || bytes < sizeof(wchar_t) || bytes > sizeof(value) ||
        bytes % sizeof(wchar_t) || value[bytes / sizeof(wchar_t) - 1]) return false;
    auto first = value[0];
    if (first && first != L'N' && first != L'n' && first != L'F' && first != L'f' && first != L'0') return false;
  }
  return true;
}

inline DS3BorderlessResult applyDS3Borderless(HWND window, bool windowed) {
  if (!windowed || !ds3BorderlessEnabled()) return DS3BorderlessResult::Disabled;
  static thread_local bool mutating = false;
  DWORD pid = 0;
  DWORD tid = GetWindowThreadProcessId(window, &pid);
  if (!tid || pid != GetCurrentProcessId()) return DS3BorderlessResult::Refused;
  if (tid != GetCurrentThreadId() || mutating) return DS3BorderlessResult::Deferred;
  if (!IsWindow(window) || GetAncestor(window, GA_ROOT) != window || GetWindow(window, GW_OWNER) ||
      IsIconic(window) || !ds3BorderlessCaptureSafe()) return DS3BorderlessResult::Refused;
  auto read = [&](int index, LONG_PTR &value) {
    SetLastError(0); value = GetWindowLongPtrW(window, index); return value || !GetLastError();
  };
  auto write = [&](int index, LONG_PTR value) {
    SetLastError(0); auto prior = SetWindowLongPtrW(window, index, value); return prior || !GetLastError();
  };
  LONG_PTR style, ex;
  RECT rect;
  MONITORINFO monitor = {}; monitor.cbSize = sizeof(monitor);
  if (!read(GWL_STYLE, style) || !read(GWL_EXSTYLE, ex) || (style & (WS_CHILD | WS_MAXIMIZE)) ||
      !GetWindowRect(window, &rect) || !GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor))
    return DS3BorderlessResult::Refused;
  RECT target = monitor.rcMonitor;
  int64_t width = static_cast<int64_t>(target.right) - target.left;
  int64_t height = static_cast<int64_t>(target.bottom) - target.top;
  if (width <= 0 || height <= 0 || width > 16384 || height > 16384) return DS3BorderlessResult::Refused;
  LONG_PTR desired_style = (style & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW)) | WS_POPUP;
  LONG_PTR desired_ex = ex & ~static_cast<LONG_PTR>(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE | WS_EX_TOPMOST);
  if (style == desired_style && ex == desired_ex && EqualRect(&rect, &target)) return DS3BorderlessResult::Unchanged;
  struct Guard { bool &flag; Guard(bool &f) : flag(f) { flag = true; } ~Guard() { flag = false; } } guard(mutating);
  auto matches = [&](LONG_PTR s, LONG_PTR e, const RECT &r) {
    LONG_PTR actual_s, actual_e; RECT actual;
    return IsWindow(window) && read(GWL_STYLE, actual_s) && read(GWL_EXSTYLE, actual_e) &&
        actual_s == s && actual_e == e && GetWindowRect(window, &actual) && EqualRect(&actual, &r);
  };
  bool changed_style = write(GWL_STYLE, desired_style);
  bool changed_ex = changed_style && write(GWL_EXSTYLE, desired_ex);
  bool positioned = changed_ex && SetWindowPos(window, HWND_NOTOPMOST, target.left, target.top, width, height,
      SWP_FRAMECHANGED | SWP_NOACTIVATE);
  RECT client;
  if (positioned && matches(desired_style, desired_ex, target) && GetClientRect(window, &client) &&
      client.right - client.left == width && client.bottom - client.top == height) return DS3BorderlessResult::Applied;
  /* A window procedure can reject or alter a mutation. Do not overwrite that
   * third state. Restore only fields still equal to our request or original. */
  LONG_PTR current_style, current_ex; RECT current_rect;
  if (!IsWindow(window) || !read(GWL_STYLE, current_style) || !read(GWL_EXSTYLE, current_ex) ||
      !GetWindowRect(window, &current_rect) || (!EqualRect(&current_rect, &rect) && !EqualRect(&current_rect, &target)) ||
      (current_style != style && current_style != desired_style) || (current_ex != ex && current_ex != desired_ex))
    return DS3BorderlessResult::RollbackFailed;
  bool restored = write(GWL_STYLE, style) && write(GWL_EXSTYLE, ex) &&
      SetWindowPos(window, (ex & WS_EX_TOPMOST) ? HWND_TOPMOST : HWND_NOTOPMOST, rect.left, rect.top,
          rect.right - rect.left, rect.bottom - rect.top, SWP_FRAMECHANGED | SWP_NOACTIVATE) && matches(style, ex, rect);
  return restored ? DS3BorderlessResult::Failed : DS3BorderlessResult::RollbackFailed;
}

/* Compatibility size policy is deliberately limited to the opted-in DS3
 * windowed route. Each zero axis retains its previous render size; at initial
 * creation it uses the client size captured before enlarging the window.
 * This operation never mutates a window and is safe on the render thread. */
inline DS3BorderlessResult resolveDS3RenderSize(HWND window, bool windowed, uint32_t &width, uint32_t &height, uint32_t previous_width = 0, uint32_t previous_height = 0) {
  if (!windowed || !ds3BorderlessEnabled()) return DS3BorderlessResult::Disabled;
  uint32_t render_width = width ? width : previous_width;
  uint32_t render_height = height ? height : previous_height;
  if (!render_width || !render_height) {
    RECT client = {};
    if (!GetClientRect(window, &client) || client.right < client.left || client.bottom < client.top)
      return DS3BorderlessResult::Refused;
    if (!render_width) render_width = client.right - client.left;
    if (!render_height) render_height = client.bottom - client.top;
  }
  width = render_width;
  height = render_height;
  return DS3BorderlessResult::Unchanged;
}

/* Retained owner-thread initialization helper. Buffer resize uses only the
 * resolver above, so WM_SIZE callbacks and render-thread requests do not
 * depend on permission to change window geometry. */
inline DS3BorderlessResult prepareDS3Borderless(HWND window, bool windowed, uint32_t &width, uint32_t &height, uint32_t previous_width = 0, uint32_t previous_height = 0) {
  uint32_t render_width = width, render_height = height;
  auto result = resolveDS3RenderSize(window, windowed, render_width, render_height, previous_width, previous_height);
  if (result != DS3BorderlessResult::Unchanged) return result;
  result = applyDS3Borderless(window, windowed);
  if (result == DS3BorderlessResult::Applied || result == DS3BorderlessResult::Unchanged) {
    width = render_width;
    height = render_height;
  }
  return result;
}
}
