/*
 * Copyright 2026 VibeWine contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Independent Metal adaptation of explicit discard intent, informed by DXVK
 * cbd2206d72d3d9dd9941d5d868bb03a858e85f9b (zlib/libpng), not copied Vulkan code.
 * See docs/discard-hints-20261025.md for source attribution and exact exclusions.
 */
#pragma once

// Included after dxmt_context.hpp. No new allocation, access-tracker update,
// retained intent, or content-validity state is introduced here.
namespace dxmt::discardhint {

inline bool
DiscardLastColorStore(EncoderData *last, Texture *texture, TextureViewKey view) {
  if (!last || last->type != EncoderType::Render || !texture ||
      uint64_t(view) != uint64_t(texture->fullView) || view.index || view.mip_start ||
      view.mip_end != 1 || view.array_start || view.array_end != 1)
    return false;
  auto *render = static_cast<RenderEncoderData *>(last);
  // The D3D11 TEXTURE2D RTV path uses zero for a nonlayered render pass.
  if (render->render_target_count != 1 || render->render_target_array_length != 0 ||
      render->dsv_planar_flags || render->depth.attachment || render->stencil.attachment)
    return false;
  auto &color = render->colors[0];
  if (!color.attachment || color.store_action != WMTStoreActionStore || color.resolve_attachment ||
      color.level || color.slice || color.depth_plane)
    return false;
  auto *attachment = color.attachment.ptr();
  auto *allocation = attachment->allocation;
  if (!allocation || allocation != texture->current() || allocation->descriptor != texture ||
      !allocation->flags().test(TextureAllocationFlag::GpuPrivate) ||
      allocation->flags().test(TextureAllocationFlag::Shared) ||
      allocation->flags().test(TextureAllocationFlag::AllocatedOnHeap) || allocation->mappedMemory ||
      uint64_t(attachment->key) != uint64_t(view) || !attachment->texture ||
      attachment->texture != allocation->texture())
    return false;
  if (texture->textureType() != WMTTextureType2D || texture->sampleCount() != 1 ||
      texture->arrayLength() != 1 || texture->miplevelCount() != 1 || texture->depth() != 1 ||
      !texture->width() || !texture->height() ||
      render->render_target_width != texture->width() || render->render_target_height != texture->height() ||
      attachment->texture.width() != texture->width() || attachment->texture.height() != texture->height())
    return false;
  // Only the last original encoder is eligible: never search past a copy/read,
  // resolve, clear, event, timestamp, compute pass or submitted command buffer.
  // Keep its draws, queries, barriers, fences and resource owners. A following
  // render cannot merge across this boundary: isEncoderSignatureMatched rejects
  // a former color whose store action is not Store. Next loads stay unchanged.
  color.store_action = WMTStoreActionDontCare;
  return true;
}

} // namespace dxmt::discardhint
