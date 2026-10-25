#pragma once

#include "d3d11_1.h"
#include "util_hash.hpp"

#ifndef DXMT_SEMANTIC_BLEND_HASH
#define DXMT_SEMANTIC_BLEND_HASH 0
#endif

namespace dxmt {

inline void
addPipelineBlendTarget(HashState &state, const D3D11_RENDER_TARGET_BLEND_DESC1 &target) noexcept {
  state.add(target.RenderTargetWriteMask);
  state.add(target.BlendEnable);
  state.add(target.LogicOpEnable);
  if (target.BlendEnable) {
    state.add(target.BlendOp);
    state.add(target.BlendOpAlpha);
    state.add(target.SrcBlend);
    state.add(target.SrcBlendAlpha);
    state.add(target.DestBlend);
    state.add(target.DestBlendAlpha);
  }
  if (target.LogicOpEnable)
    state.add(target.LogicOp);
}

// Match the graphics pipeline comparator, including its count-zero behavior.
// Inactive fields and targets must not affect hashes of equivalent pipelines.
inline size_t
hashPipelineBlend(const D3D11_BLEND_DESC1 &desc, uint32_t num_rt) noexcept {
  HashState state;
  state.add(desc.IndependentBlendEnable);
  state.add(desc.AlphaToCoverageEnable);
  uint32_t count = desc.IndependentBlendEnable ? num_rt : 1;
  for (uint32_t i = 0; i < count; ++i)
    addPipelineBlendTarget(state, desc.RenderTarget[i]);
  return state;
}

} // namespace dxmt
