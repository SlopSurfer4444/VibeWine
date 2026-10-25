/* This file is part of DXMT, licensed under LGPL-2.1-or-later. */
#pragma once
#include <cstdint>

namespace dxmt {

// Fixed output is an explicit pixel-size request, independent of game-facing
// dimensions. Never round an aspect ratio or turn a downscale into an upscale.
struct SpatialOutputGeometry {
  uint32_t width = 0;
  uint32_t height = 0;
  bool upscale = false;
};

inline bool ResolveFixedSpatialOutput(uint32_t render_width, uint32_t render_height,
    int output_width, int output_height, uint32_t limit, SpatialOutputGeometry &result) {
  if (output_width <= 0 || output_height <= 0 ||
      uint32_t(output_width) > limit || uint32_t(output_height) > limit)
    return false;
  result.width = uint32_t(output_width);
  result.height = uint32_t(output_height);
  result.upscale = render_width != 0 && render_height != 0 &&
      render_width <= result.width && render_height <= result.height &&
      (render_width < result.width || render_height < result.height) &&
      uint64_t(render_width) * result.height == uint64_t(render_height) * result.width;
  return true;
}

} // namespace dxmt
