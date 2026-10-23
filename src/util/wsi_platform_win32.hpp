/*
 * This file is part of DXMT, Copyright (c) 2023 Feifan He
 *
 * Derived from a part of DXVK (originally under zlib License),
 * Copyright (c) 2017 Philip Rebohle
 * Copyright (c) 2019 Joshua Ashton
 *
 * See <https://github.com/doitsujin/dxvk/blob/master/LICENSE>
 */

#pragma once

#include <windows.h>

// Internal PE import from the matching DXGI DLL. Both D3D11 and D3D12 utility
// copies must share one nonreusing capture-token allocator.
extern "C" HANDLE __stdcall DXMTAllocateFullscreenToken();

namespace dxmt::wsi {

/**
 * \brief Impl-dependent state
 */
struct DXMTWindowState {
  // An opaque, process-unique token binds this capture generation to the HWND.
  // It is never a state address: leave/reenter and address reuse get fresh tokens.
  HWND owner = nullptr;
  HANDLE capture_token = nullptr;
  bool captured = false;
  bool style_pending = false;
  bool exstyle_pending = false;
  bool present_rect_pending = false;
  bool position_pending = false;
  LONG style = 0;
  LONG exstyle = 0;
  RECT rect = {0, 0, 0, 0};
};

} // namespace dxmt::wsi