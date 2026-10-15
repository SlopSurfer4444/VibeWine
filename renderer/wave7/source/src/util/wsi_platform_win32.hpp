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

namespace dxmt::wsi {

/**
 * \brief Impl-dependent state
 */
struct DXMTWindowState {
  // A window property binds this snapshot to this HWND generation. Destroying
  // a window removes its properties, so handle reuse cannot consume old state.
  HWND owner = nullptr;
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