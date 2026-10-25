#pragma once
#include <algorithm>
#include <cmath>
#include "winemetal.h"

namespace dxmt {

// Work in pixels of the actually acquired drawable, including asynchronous resize.
// The ordinary presenter keeps its exact full-viewport behavior.
inline WMTViewport
fitPresentViewport(double output_width, double output_height,
                   double source_width, double source_height, bool preserve_aspect) {
  WMTViewport result{0, 0, output_width, output_height, 0, 1};
  if (!preserve_aspect || !std::isfinite(output_width) || !std::isfinite(output_height) ||
      !std::isfinite(source_width) || !std::isfinite(source_height) ||
      output_width <= 0 || output_height <= 0 || source_width <= 0 || source_height <= 0)
    return result;
  const auto scale = std::min(output_width / source_width, output_height / source_height);
  result.width = source_width * scale;
  result.height = source_height * scale;
  result.originX = (output_width - result.width) / 2;
  result.originY = (output_height - result.height) / 2;
  return result;
}

} // namespace dxmt
