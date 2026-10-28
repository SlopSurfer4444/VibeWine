#pragma once

namespace dxmt {

class Buffer;

// DISCARD changes the backing allocation/suballocation, while the Buffer
// identity stays stable. Preserve the existing table rebuild for every bound
// alias of that Buffer, without dirtying unrelated shader stages or slots.
// This scans existing bindings only; it owns no extra resource references or
// dependency registry. Shader/encoder changes keep their normal invalidation.
template <typename ShaderStages>
void
InvalidateDiscardedConstantBuffers(ShaderStages &stages, const Buffer *discarded) {
  for (auto &stage : stages) {
    for (const auto &[slot, binding] : stage.ConstantBuffers) {
      if (binding.Buffer->buffer().ptr() == discarded)
        stage.ConstantBuffers.set_dirty(slot);
    }
  }
}

// Both vertex bindings and buffer SRVs keep the canonical Buffer identity.
// Texture SRVs return a null buffer and remain outside buffer DISCARD scans.
template <typename VertexBindings>
void
InvalidateDiscardedVertexBuffers(VertexBindings &bindings, const Buffer *discarded) {
  for (const auto &[slot, binding] : bindings) {
    if (binding.Buffer->buffer().ptr() == discarded)
      bindings.set_dirty(slot);
  }
}

template <typename ShaderStages>
void
InvalidateDiscardedShaderResources(ShaderStages &stages, const Buffer *discarded) {
  for (auto &stage : stages) {
    for (const auto &[slot, binding] : stage.SRVs) {
      if (binding.SRV->buffer().ptr() == discarded)
        stage.SRVs.set_dirty(slot);
    }
  }
}

} // namespace dxmt
