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

} // namespace dxmt
