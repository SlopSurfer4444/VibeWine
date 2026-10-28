#pragma once

#include "airconv_public.h"
#include <cstdint>

namespace dxmt {

// Only the ordinary vertex path ignores the draw packet's index type. Keep
// emulation, tessellation and stream-output descriptors byte-for-byte distinct.
template <typename Descriptor>
Descriptor canonicalize_ordinary_pipeline_desc(Descriptor desc) {
  if (!desc.GeometryShader && !desc.HullShader && !desc.DomainShader && !desc.SOLayout)
    desc.IndexBufferFormat = SM50_INDEX_BUFFER_FORMAT_NONE;
  return desc;
}

// Reflection is conservative and in output-register coordinates. Dual-source
// output assignment is a separate specialization and retains its original key.
// Sample/coverage, depth and attachment formats are deliberately not inputs.
template <typename PixelVariant>
PixelVariant canonicalize_pixel_variant(PixelVariant variant, uint32_t valid_render_targets) {
  if (!variant.dual_source_blending)
    variant.unorm_output_reg_mask &= valid_render_targets;
  return variant;
}

} // namespace dxmt
