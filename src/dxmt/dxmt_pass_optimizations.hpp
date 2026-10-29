/*
 * Copyright 2026 VibeWine contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Included after dxmt_context.hpp: these helpers operate on the recorded
// production encoder/command types, without owning resources or changing fences.
namespace dxmt::passopt {

inline bool
DiscardAdjacentColorStores(RenderEncoderData *render, ClearEncoderData *clear) {
  // Run on the original list, before clear folding or encoder reordering. The
  // store decision then survives a subsequent fold without inferring provenance
  // from a load action or stepping over any intervening operation.
  if (render->next != clear || render->type != EncoderType::Render || clear->type != EncoderType::Clear ||
      render->render_target_count != 1 || render->render_target_array_length != 1 ||
      render->dsv_planar_flags || render->depth.attachment || render->stencil.attachment ||
      clear->clear_dsv || clear->array_length != 1)
    return false;
  auto &color = render->colors[0];
  if (!color.attachment || color.attachment.ptr() != clear->attachment.ptr() ||
      color.store_action != WMTStoreActionStore || color.resolve_attachment || color.level || color.slice ||
      color.depth_plane)
    return false;
  auto *view = color.attachment.ptr();
  auto *allocation = view->allocation;
  if (!allocation || !allocation->flags().test(TextureAllocationFlag::GpuPrivate) ||
      allocation->flags().test(TextureAllocationFlag::Shared) ||
      allocation->flags().test(TextureAllocationFlag::AllocatedOnHeap) || allocation->mappedMemory)
    return false;
  auto *texture = allocation->descriptor;
  if (!texture || texture->textureType() != WMTTextureType2D || texture->sampleCount() != 1 ||
      texture->arrayLength() != 1 || texture->miplevelCount() != 1 || texture->depth() != 1 ||
      !texture->width() || !texture->height() ||
      render->render_target_width != texture->width() || render->render_target_height != texture->height() ||
      clear->width != texture->width() || clear->height != texture->height())
    return false;
  // A base view only: no reinterpretation or subresource. GpuPrivate also
  // excludes the existing linear/buffer-backed texture factories; those never
  // set that flag. Shared and placement-heap storage are rejected above.
  if (view->key.index || view->key.mip_start || view->key.mip_end != 1 || view->key.array_start ||
      view->key.array_end != 1 || !view->texture || view->texture != allocation->texture())
    return false;
  // Bind the extent proof to the actual native attachment, not descriptor
  // equality alone. These two getters are paid only for an otherwise eligible
  // pair and are part of this experiment's cost, not an assumed free saving.
  if (view->texture.width() != texture->width() || view->texture.height() != texture->height())
    return false;
  color.store_action = WMTStoreActionDontCare;
  return true;
}

// Eliminate only a dead full color clear in the ORIGINAL adjacent list. The
// survivor retains the last clear value, all external dependencies and both
// completion identities. Never seek through even an inert-looking Null node.
inline bool
CoalesceAdjacentColorClears(ClearEncoderData *former, ClearEncoderData *latter) {
  if (former->next != latter || former->type != EncoderType::Clear || latter->type != EncoderType::Clear ||
      former->clear_dsv || latter->clear_dsv || former->array_length != 1 || latter->array_length != 1 ||
      !former->attachment || !latter->attachment || former->attachment.ptr() != latter->attachment.ptr() ||
      former->fence_wait.intersectedWith(former->fence_update))
    return false;
  auto *view = latter->attachment.ptr();
  auto *allocation = view->allocation;
  if (!allocation || !allocation->flags().test(TextureAllocationFlag::GpuPrivate) ||
      allocation->flags().test(TextureAllocationFlag::Shared) ||
      allocation->flags().test(TextureAllocationFlag::AllocatedOnHeap) || allocation->mappedMemory)
    return false;
  auto *texture = allocation->descriptor;
  if (!texture || texture->textureType() != WMTTextureType2D || texture->sampleCount() != 1 ||
      texture->arrayLength() != 1 || texture->miplevelCount() != 1 || texture->depth() != 1 ||
      !texture->width() || !texture->height() || former->width != texture->width() ||
      former->height != texture->height() || latter->width != texture->width() ||
      latter->height != texture->height())
    return false;
  if (view->key.index || view->key.mip_start || view->key.mip_end != 1 || view->key.array_start ||
      view->key.array_end != 1 || !view->texture || view->texture != allocation->texture())
    return false;
  if (view->texture.width() != latter->width || view->texture.height() != latter->height)
    return false;

  // This is the same fence contraction as existing Clear->Render folding.
  // Only former's internal completion waits disappear; unrelated waits remain.
  latter->fence_wait.merge(former->fence_wait);
  latter->fence_wait.subtract(former->fence_update);
  latter->fence_update.merge(former->fence_update);
  former->~ClearEncoderData();
  former->next = nullptr;
  former->type = EncoderType::Null;
  return true;
}

inline size_t
DeduplicateRenderResidency(wmtcmd_render_nop &head) {
  struct Entry {
    obj_handle_t resource = 0;
    uint8_t usage = 0;
    uint8_t stages = 0;
  };
  // Fixed storage and linear traversal. A collision/eviction merely misses a
  // possible duplicate; no hash collision can manufacture residency coverage.
  std::array<Entry, 128> cache{};
  size_t removed = 0;
  for (auto *node = static_cast<wmtcmd_base *>(head.next.get()); node;
       node = static_cast<wmtcmd_base *>(node->next.get())) {
    if (node->type != WMTRenderCommandUseResource)
      continue;
    auto *command = reinterpret_cast<wmtcmd_render_useresource *>(node);
    if (!command->resource || !command->usage || !command->stages)
      continue;
    auto &entry = cache[((command->resource >> 4) ^ (command->resource >> 21)) & (cache.size() - 1)];
    const auto usage = uint8_t(command->usage), stages = uint8_t(command->stages);
    // Both masks must be covered by ONE actual earlier declaration. Combining
    // vertex-read with fragment-write would incorrectly imply vertex-write.
    if (entry.resource == command->resource && (entry.usage & usage) == usage &&
        (entry.stages & stages) == stages) {
      command->type = WMTRenderCommandNop;
      ++removed;
    } else {
      entry = {command->resource, usage, stages};
    }
  }
  // Nops retain list topology/tail and all nondeclaration command ordering.
  return removed;
}

} // namespace dxmt::passopt
