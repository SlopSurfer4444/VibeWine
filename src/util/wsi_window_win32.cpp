/*
 * This file is part of DXMT, Copyright (c) 2023 Feifan He
 *
 * Derived from a part of DXVK (originally under zlib License),
 * Copyright (c) 2017 Philip Rebohle
 * Copyright (c) 2019 Joshua Ashton
 *
 * See <https://github.com/doitsujin/dxvk/blob/master/LICENSE>
 */

#include "util_d3dkmt.h"
#include "wsi_window.hpp"
#include "wsi_monitor.hpp"
#include "winemetal.h"
#include "wave7_feedback.h"

#include "util_string.hpp"
#include "log/log.hpp"

namespace dxmt::wsi {

static bool getMonitorDisplayMode(HMONITOR hMonitor, DWORD modeNum,
                                  DEVMODEW *pMode) {
  ::MONITORINFOEXW monInfo;
  monInfo.cbSize = sizeof(monInfo);

  if (!::GetMonitorInfoW(hMonitor, reinterpret_cast<MONITORINFO *>(&monInfo))) {
    Logger::err("Failed to query monitor info");
    return false;
  }

  return ::EnumDisplaySettingsW(monInfo.szDevice, modeNum, pMode);
}

static bool setMonitorDisplayMode(HMONITOR hMonitor, DEVMODEW *pMode) {
  ::MONITORINFOEXW monInfo;
  monInfo.cbSize = sizeof(monInfo);

  if (!::GetMonitorInfoW(hMonitor, reinterpret_cast<MONITORINFO *>(&monInfo))) {
    Logger::err("Failed to query monitor info");
    return false;
  }

  Logger::info(str::format("Setting display mode: ", pMode->dmPelsWidth, "x",
                           pMode->dmPelsHeight, "@",
                           pMode->dmDisplayFrequency));

  DEVMODEW curMode = {};
  curMode.dmSize = sizeof(curMode);

  if (getMonitorDisplayMode(hMonitor, ENUM_CURRENT_SETTINGS, &curMode)) {
    bool eq = curMode.dmPelsWidth == pMode->dmPelsWidth &&
              curMode.dmPelsHeight == pMode->dmPelsHeight &&
              curMode.dmBitsPerPel == pMode->dmBitsPerPel;

    if (pMode->dmFields & DM_DISPLAYFREQUENCY)
      eq &= curMode.dmDisplayFrequency == pMode->dmDisplayFrequency;

    if (eq)
      return true;
  }

  LONG status = ::ChangeDisplaySettingsExW(monInfo.szDevice, pMode, nullptr,
                                           CDS_FULLSCREEN, nullptr);

  if (status != DISP_CHANGE_SUCCESSFUL) {
    pMode->dmFields &= ~DM_DISPLAYFREQUENCY;

    status = ::ChangeDisplaySettingsExW(monInfo.szDevice, pMode, nullptr,
                                        CDS_FULLSCREEN, nullptr);
  }

  return status == DISP_CHANGE_SUCCESSFUL;
}

void getWindowSize(HWND hWindow, uint32_t *pWidth, uint32_t *pHeight) {
  RECT rect = {};
  ::GetClientRect(hWindow, &rect);

  if (pWidth)
    *pWidth = rect.right - rect.left;

  if (pHeight)
    *pHeight = rect.bottom - rect.top;
}

static bool getWindowLongChecked(HWND window, int index, LONG *value) {
  ::SetLastError(ERROR_SUCCESS);
  *value = ::GetWindowLongW(window, index);
  return *value != 0 || ::GetLastError() == ERROR_SUCCESS;
}

static bool setWindowLongChecked(HWND window, int index, LONG value) {
  ::SetLastError(ERROR_SUCCESS);
  LONG previous = ::SetWindowLongW(window, index, value);
  return previous != 0 || ::GetLastError() == ERROR_SUCCESS;
}

bool resizeWindow(HWND hWindow, [[maybe_unused]] DXMTWindowState *pState,
                  uint32_t width, uint32_t height) {
  RECT oldRect = {}, newRect = {0, 0, LONG(width), LONG(height)};
  LONG style = 0, exstyle = 0;
  if (!::IsWindow(hWindow) || !::GetWindowRect(hWindow, &oldRect) ||
      !getWindowLongChecked(hWindow, GWL_STYLE, &style) ||
      !getWindowLongChecked(hWindow, GWL_EXSTYLE, &exstyle) ||
      !::AdjustWindowRectEx(&newRect, style, FALSE, exstyle))
    return false;
  if (!::IsWindow(hWindow) ||
      !::MoveWindow(hWindow, oldRect.left, oldRect.top,
                    newRect.right - newRect.left, newRect.bottom - newRect.top, TRUE))
    return false;
  // MoveWindow may synchronously dispatch destruction of the HWND.
  return ::IsWindow(hWindow);
}

bool setWindowMode(HMONITOR hMonitor, HWND hWindow, const WsiMode &mode) {
  ::MONITORINFOEXW monInfo;
  monInfo.cbSize = sizeof(monInfo);

  if (!::GetMonitorInfoW(hMonitor, reinterpret_cast<MONITORINFO *>(&monInfo))) {
    Logger::err("Win32 WSI: setWindowMode: Failed to query monitor info");
    return false;
  }

  DEVMODEW devMode = {};
  devMode.dmSize = sizeof(devMode);
  devMode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
  devMode.dmPelsWidth = mode.width;
  devMode.dmPelsHeight = mode.height;
  devMode.dmBitsPerPel = mode.bitsPerPixel;

  if (mode.refreshRate.numerator != 0) {
    devMode.dmFields |= DM_DISPLAYFREQUENCY;
    devMode.dmDisplayFrequency =
        mode.refreshRate.numerator / mode.refreshRate.denominator;
  }

  return setMonitorDisplayMode(hMonitor, &devMode);
}

static constexpr WCHAR fullscreenStateProperty[] = L"DXMTFullscreenState";


static bool ownsFullscreenState(HWND window, const DXMTWindowState *state, HANDLE token) {
  return token && state->capture_token == token && ::IsWindow(window) &&
      state->captured && state->owner == window &&
      ::GetPropW(window, fullscreenStateProperty) == token;
}

static void retireFullscreenCapture(DXMTWindowState *state, HANDLE token) {
  // A synchronous callback may already have recaptured this same storage.
  if (state->capture_token == token)
    *state = {};
}

bool enterFullscreenMode(HMONITOR hMonitor, HWND hWindow,
                         DXMTWindowState *pState,
                         [[maybe_unused]] bool modeSwitch) {
  if (pState->captured || !::IsWindow(hWindow))
    return false;

  RECT rect = {};
  DXMTWindowState saved = {};
  if (!getDesktopCoordinates(hMonitor, &rect) ||
      !::GetWindowRect(hWindow, &saved.rect) ||
      !getWindowLongChecked(hWindow, GWL_STYLE, &saved.style) ||
      !getWindowLongChecked(hWindow, GWL_EXSTYLE, &saved.exstyle))
    return false;

  // Do not steal another capture or mutate the window/state if IDs are exhausted.
  if (::GetPropW(hWindow, fullscreenStateProperty))
    return false;
  const HANDLE token = DXMTAllocateFullscreenToken();
  if (!token || !::SetPropW(hWindow, fullscreenStateProperty, token))
    return false;
  saved.owner = hWindow;
  saved.capture_token = token;
  saved.captured = true;
  *pState = saved;

  D3DKMT_ESCAPE escape = {};
  escape.Type = D3DKMT_ESCAPE_SET_PRESENT_RECT_WINE;
  escape.pPrivateDriverData = &rect;
  escape.PrivateDriverDataSize = sizeof(rect);
  escape.hContext = HandleToUlong(hWindow);
  pState->present_rect_pending = true;
  if (!ownsFullscreenState(hWindow, pState, token) || D3DKMTEscape(&escape) != 0 ||
      !ownsFullscreenState(hWindow, pState, token))
    return false;

  if (!ownsFullscreenState(hWindow, pState, token) ||
      !setWindowLongChecked(hWindow, GWL_STYLE, saved.style & ~WS_OVERLAPPEDWINDOW) ||
      !ownsFullscreenState(hWindow, pState, token))
    return false;
  pState->style_pending = true;
  pState->position_pending = true;

  if (!ownsFullscreenState(hWindow, pState, token) ||
      !setWindowLongChecked(hWindow, GWL_EXSTYLE, saved.exstyle & ~WS_EX_OVERLAPPEDWINDOW) ||
      !ownsFullscreenState(hWindow, pState, token))
    return false;
  pState->exstyle_pending = true;

  if (!ownsFullscreenState(hWindow, pState, token) ||
      !::SetWindowPos(hWindow, HWND_TOPMOST, rect.left, rect.top,
                     rect.right - rect.left, rect.bottom - rect.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE))
    return false;
  // SetWindowPos can synchronously dispatch code that destroys the HWND.
  return ownsFullscreenState(hWindow, pState, token);
}

bool leaveFullscreenMode(HWND hWindow, DXMTWindowState *pState,
                         bool restoreCoordinates) {
  if (!pState->captured)
    return true;
  const HANDLE token = pState->capture_token;
  if (!ownsFullscreenState(hWindow, pState, token)) {
    // The original window is gone; never restore an HWND recycled for another.
    retireFullscreenCapture(pState, token);
    return true;
  }

  bool success = true;
  // Independently restore each completed mutation. A partial entry need not
  // have changed both styles; application changes are preserved as in DXGI.
  if (pState->style_pending) {
    LONG current = 0;
    if (!getWindowLongChecked(hWindow, GWL_STYLE, &current)) {
      success = false;
    } else if (!ownsFullscreenState(hWindow, pState, token)) {
      retireFullscreenCapture(pState, token);
      return true;
    } else if ((current & ~WS_VISIBLE) != (pState->style & ~(WS_VISIBLE | WS_OVERLAPPEDWINDOW))) {
      pState->style_pending = false;
    } else if (ownsFullscreenState(hWindow, pState, token) &&
               setWindowLongChecked(hWindow, GWL_STYLE, pState->style) &&
               ownsFullscreenState(hWindow, pState, token)) {
      pState->style_pending = false;
    } else {
      success = false;
    }
  }
  if (!ownsFullscreenState(hWindow, pState, token)) {
    retireFullscreenCapture(pState, token);
    return true;
  }
  if (pState->exstyle_pending) {
    LONG current = 0;
    if (!getWindowLongChecked(hWindow, GWL_EXSTYLE, &current)) {
      success = false;
    } else if (!ownsFullscreenState(hWindow, pState, token)) {
      retireFullscreenCapture(pState, token);
      return true;
    } else if ((current & ~WS_EX_TOPMOST) != (pState->exstyle & ~(WS_EX_TOPMOST | WS_EX_OVERLAPPEDWINDOW))) {
      pState->exstyle_pending = false;
    } else if (ownsFullscreenState(hWindow, pState, token) &&
               setWindowLongChecked(hWindow, GWL_EXSTYLE, pState->exstyle) &&
               ownsFullscreenState(hWindow, pState, token)) {
      pState->exstyle_pending = false;
    } else {
      success = false;
    }
  }
  if (!ownsFullscreenState(hWindow, pState, token)) {
    retireFullscreenCapture(pState, token);
    return true;
  }
  if (pState->present_rect_pending) {
    RECT empty = {};
    D3DKMT_ESCAPE escape = {};
    escape.Type = D3DKMT_ESCAPE_SET_PRESENT_RECT_WINE;
    escape.pPrivateDriverData = &empty;
    escape.PrivateDriverDataSize = sizeof(empty);
    escape.hContext = HandleToUlong(hWindow);
    if (D3DKMTEscape(&escape) == 0 && ownsFullscreenState(hWindow, pState, token))
      pState->present_rect_pending = false;
    else
      success = false;
  }
  if (!ownsFullscreenState(hWindow, pState, token)) {
    retireFullscreenCapture(pState, token);
    return true;
  }
  if (pState->position_pending) {
    UINT flags = SWP_FRAMECHANGED | SWP_NOACTIVATE;
    if (!restoreCoordinates)
      flags |= SWP_NOSIZE | SWP_NOMOVE;
    const RECT rect = pState->rect;
    if (::SetWindowPos(hWindow,
                      (pState->exstyle & WS_EX_TOPMOST) ? HWND_TOPMOST : HWND_NOTOPMOST,
                      rect.left, rect.top, rect.right - rect.left,
                      rect.bottom - rect.top, flags) &&
        ownsFullscreenState(hWindow, pState, token))
      pState->position_pending = false;
    else
      success = false;
  }

  if (!ownsFullscreenState(hWindow, pState, token)) {
    retireFullscreenCapture(pState, token);
    return true;
  }
  if (!pState->style_pending && !pState->exstyle_pending &&
      !pState->present_rect_pending && !pState->position_pending) {
    if (ownsFullscreenState(hWindow, pState, token)) {
      ::RemovePropW(hWindow, fullscreenStateProperty);
      if (::GetPropW(hWindow, fullscreenStateProperty) == token)
        return false;
    }
    retireFullscreenCapture(pState, token);
  }
  return success;
}

bool detachFullscreenState(HWND hWindow, const DXMTWindowState *pState) {
  const HANDLE token = pState->capture_token;
  // Only remove this capture's token. Destroyed/recycled windows and a marker
  // installed by another owner must not be modified during terminal cleanup.
  if (!token || !::IsWindow(hWindow) ||
      ::GetPropW(hWindow, fullscreenStateProperty) != token)
    return true;
  ::RemovePropW(hWindow, fullscreenStateProperty);
  // Removal failure must be visible; never claim the physical mutations were
  // restored, or delete a replacement marker installed during an API callback.
  return ::GetPropW(hWindow, fullscreenStateProperty) != token;
}

static bool displayCaptureCleanupComplete() {
  W7FBRequest request = {};
  request.size = sizeof(request);
  request.version = W7FB_VERSION;
  request.op = W7FB_CAPTURE_STATUS;
  request.status = W7FB_UNSUPPORTED;
  // This optional query uses an existing property call. An old bridge returns
  // zero for this property without touching the request, so it cannot falsely
  // discharge our pending native cleanup obligation.
  uint64_t reply = MTLCommandBuffer_property(
      reinterpret_cast<obj_handle_t>(&request),
      static_cast<WMTCommandBufferProperty>(W7FB_PROPERTY));
  return reply == W7FB_REPLY && request.status == W7FB_OK;
}

bool restoreDisplayMode(HMONITOR hMonitor) {
  WCHAR device_name[32] = {};
  return getDisplayName(hMonitor, device_name) && restoreDisplayMode(device_name);
}

bool restoreDisplayMode(const WCHAR *device_name) {
  if (!device_name || !device_name[0])
    return false;
  DEVMODEW current_mode = {}, registry_mode = {};
  current_mode.dmSize = sizeof(current_mode);
  registry_mode.dmSize = sizeof(registry_mode);
  if (!EnumDisplaySettingsExW(device_name, ENUM_CURRENT_SETTINGS, &current_mode, 0))
    return false;
  if (!EnumDisplaySettingsExW(device_name, ENUM_REGISTRY_SETTINGS, &registry_mode, 0))
    return false;
  do {
    if (current_mode.dmPelsWidth != registry_mode.dmPelsWidth)
      break;
    if (current_mode.dmPelsHeight != registry_mode.dmPelsHeight)
      break;
    if (current_mode.dmBitsPerPel != registry_mode.dmBitsPerPel)
      break;
    if ((current_mode.dmFields & registry_mode.dmFields & DM_DISPLAYFREQUENCY) &&
        current_mode.dmDisplayFrequency != registry_mode.dmDisplayFrequency)
      break;
    if (displayCaptureCleanupComplete())
      return true;
    // The mode may already be restored while native capture release failed.
    // A pending lease still requires a cleanup attempt and native receipt.
  } while (0);
  LONG ret = ChangeDisplaySettingsExW(device_name, NULL, NULL, 0, NULL);
  return ret == DISP_CHANGE_SUCCESSFUL && displayCaptureCleanupComplete();
}

HMONITOR getWindowMonitor(HWND hWindow) {
  RECT windowRect = {0, 0, 0, 0};
  ::GetWindowRect(hWindow, &windowRect);

  HMONITOR monitor =
      ::MonitorFromPoint({(windowRect.left + windowRect.right) / 2,
                          (windowRect.top + windowRect.bottom) / 2},
                         MONITOR_DEFAULTTOPRIMARY);

  return monitor;
}

bool isWindow(HWND hWindow) { return ::IsWindow(hWindow); }

bool updateFullscreenWindow(HMONITOR hMonitor, HWND hWindow,
                            bool forceTopmost) {
  // Snapshot this capture generation, independently of state storage lifetime.
  // Reentrant leave/reenter on a recycled HWND receives a fresh opaque token.
  const HANDLE owner = ::GetPropW(hWindow, fullscreenStateProperty);
  const auto ownsWindow = [&] {
    return owner && ::IsWindow(hWindow) &&
        ::GetPropW(hWindow, fullscreenStateProperty) == owner;
  };
  RECT bounds = {};
  if (!ownsWindow() || !wsi::getDesktopCoordinates(hMonitor, &bounds))
    return false;

  D3DKMT_ESCAPE escape = {};
  escape.Type = D3DKMT_ESCAPE_SET_PRESENT_RECT_WINE;
  escape.pPrivateDriverData = &bounds;
  escape.PrivateDriverDataSize = sizeof(bounds);
  escape.hContext = HandleToUlong(hWindow);
  if (!ownsWindow() || D3DKMTEscape(&escape) != 0 || !ownsWindow())
    return false;

  // The fullscreen capture retains position/present-rect cleanup after a
  // partial update. Do not report success if the following window move fails.
  bool positioned = forceTopmost
      ? ::SetWindowPos(hWindow, HWND_TOPMOST, bounds.left, bounds.top,
                       bounds.right - bounds.left, bounds.bottom - bounds.top,
                       SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE)
      : ::MoveWindow(hWindow, bounds.left, bounds.top, bounds.right - bounds.left,
                     bounds.bottom - bounds.top, TRUE);
  return positioned && ownsWindow();
}

bool isForeground(HWND hWindow) {
  return ::GetForegroundWindow() == hWindow;
}

bool isMinimized(HWND hWindow) {
  return ::IsIconic(hWindow);
}

} // namespace dxmt::wsi
