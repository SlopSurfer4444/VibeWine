/*
 * Copyright 2026 Feifan He for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "Metal.hpp"
#include "dxmt_resource_initializer.hpp"
#include "dxmt_format.hpp"
#include "util_math.hpp"
#include "util_error.hpp"
#include <cstdint>
#include <cstring>
#include <mutex>

namespace dxmt {

namespace {

bool checkedMultiply(size_t a, size_t b, size_t &result) {
  if (b && a > std::numeric_limits<size_t>::max() / b)
    return false;
  result = a * b;
  return true;
}

// Validate the largest source byte offset before pointer arithmetic. Source
// storage itself remains the caller's responsibility, as in the upload API.
bool checkedSourceRange(size_t depth, size_t image_pitch, size_t rows, size_t row_pitch, size_t length) {
  size_t image_offset, row_offset;
  if (!checkedMultiply(depth - 1, image_pitch, image_offset) ||
      !checkedMultiply(rows - 1, row_pitch, row_offset) ||
      row_offset > std::numeric_limits<size_t>::max() - image_offset)
    return false;
  return length <= std::numeric_limits<size_t>::max() - image_offset - row_offset;
}

uint32_t mipExtent(uint32_t extent, uint32_t level) {
  return level < 32 ? std::max(1u, extent >> level) : 1u;
}

} // namespace

ResourceInitializer::ResourceInitializer(WMT::Device device) :
    device_(device),
    gpu_command_heap_allocator(StagingBufferBlockAllocator(
        device, WMTResourceStorageModeManaged | WMTResourceHazardTrackingModeUntracked, false
    )) {
  upload_queue_ = device.newCommandQueue(kResourceInitializerChunks);
  upload_queue_event_ = device.newSharedEvent();

  cpu_command_heap_size = kResourceInitializerCpuCommandHeapSize;
  cpu_command_heap = malloc(cpu_command_heap_size);
  reset();
}

ResourceInitializer::~ResourceInitializer() {
  ref_tracker.clear();
  free(cpu_command_heap);
}

InitResult
ResourceInitializer::initWithZero(BufferAllocation *buffer, uint64_t offset, uint64_t length) {
  std::lock_guard<dxmt::mutex> lock(mutex_);

  auto status = prepareCpu<wmtcmd_blit_fillbuffer>();
  if (status != InitStatus::Success)
    return {status, 0};
  if (!retainAllocation(buffer)) // capacity includes a possible tracker spill
    return {InitStatus::OutOfMemory, 0};
  auto fill = allocateCpuHeap<wmtcmd_blit_fillbuffer>();
  {
    fill->type = WMTBlitCommandFillBuffer;
    fill->buffer = buffer->buffer();
    fill->offset = offset;
    fill->length = length;
    fill->value = 0;
    publishBlit((wmtcmd_base *)fill, (wmtcmd_base *)fill);
  }

  return {InitStatus::Success, current_seq_id_};
}

InitResult
ResourceInitializer::initDepthStencilWithZero(
    const Texture *texture, TextureAllocation *allocation, uint32_t slice, uint32_t level, uint32_t dsv_planar
) {
  auto width_sub = mipExtent(texture->width(), level);
  auto height_sub = mipExtent(texture->height(), level);

  std::lock_guard<dxmt::mutex> lock(mutex_);
  auto status = prepareCpu<ClearRenderPassInfo>();
  if (status != InitStatus::Success)
    return {status, 0};
  if (!retainAllocation(allocation))
    return {InitStatus::OutOfMemory, 0};
  auto pass = allocateCpuHeap<ClearRenderPassInfo>();
  auto info = &pass->info;
  WMT::InitializeRenderPassInfo(*info);
  {

    info->render_target_array_length = 0;
    info->render_target_width = width_sub;
    info->render_target_height = height_sub;
    if (dsv_planar & 1) {
      info->depth.texture = allocation->texture();
      info->depth.clear_depth = 0;
      info->depth.load_action = WMTLoadActionClear;
      info->depth.store_action = WMTStoreActionStore;
      info->depth.slice = slice;
      info->depth.level = level;
    }
    if (dsv_planar & 2) {
      info->stencil.texture = allocation->texture();
      info->stencil.clear_stencil = 0;
      info->stencil.load_action = WMTLoadActionClear;
      info->stencil.store_action = WMTStoreActionStore;
      info->stencil.slice = slice;
      info->stencil.level = level;
    }

    publishClear(pass);
  }

  return {InitStatus::Success, current_seq_id_};
}

InitResult
ResourceInitializer::initRenderTargetWithZero(
    const Texture *texture, TextureAllocation *allocation, uint32_t slice, uint32_t level
) {
  auto width_sub = mipExtent(texture->width(), level);
  auto height_sub = mipExtent(texture->height(), level);

  std::lock_guard<dxmt::mutex> lock(mutex_);
  auto status = prepareCpu<ClearRenderPassInfo>();
  if (status != InitStatus::Success)
    return {status, 0};
  if (!retainAllocation(allocation))
    return {InitStatus::OutOfMemory, 0};
  auto pass = allocateCpuHeap<ClearRenderPassInfo>();
  auto info = &pass->info;
  WMT::InitializeRenderPassInfo(*info);
  {

    info->render_target_array_length = texture->textureType() == WMTTextureType3D ? mipExtent(texture->depth(), level) : 0;
    info->render_target_width = width_sub;
    info->render_target_height = height_sub;
    info->colors[0].texture = allocation->texture();
    info->colors[0].load_action = WMTLoadActionClear;
    info->colors[0].clear_color = {0, 0, 0, 0};
    info->colors[0].store_action = WMTStoreActionStore;
    info->colors[0].slice = slice;
    info->colors[0].level = level;

    publishClear(pass);
  }

  return {InitStatus::Success, current_seq_id_};
}

InitResult
ResourceInitializer::initWithZero(
    const Texture *texture, TextureAllocation *allocation, uint32_t slice, uint32_t level
) {
  if (auto dsv_planar = DepthStencilPlanarFlags(texture->pixelFormat())) {
    return initDepthStencilWithZero(texture, allocation, slice, level, dsv_planar);
  }
  if (texture->usage() & WMTTextureUsageRenderTarget) {
    return initRenderTargetWithZero(texture, allocation, slice, level);
  }

  auto width_sub = mipExtent(texture->width(), level);
  auto height_sub = mipExtent(texture->height(), level);
  auto depth_sub = mipExtent(texture->depth(), level);

  auto block_size = 1u;

  switch (texture->pixelFormat()) {
  case WMTPixelFormatBC1_RGBA:
  case WMTPixelFormatBC1_RGBA_sRGB:
  case WMTPixelFormatBC2_RGBA:
  case WMTPixelFormatBC2_RGBA_sRGB:
  case WMTPixelFormatBC3_RGBA:
  case WMTPixelFormatBC3_RGBA_sRGB:
  case WMTPixelFormatBC4_RSnorm:
  case WMTPixelFormatBC4_RUnorm:
  case WMTPixelFormatBC5_RGUnorm:
  case WMTPixelFormatBC5_RGSnorm:
  case WMTPixelFormatBC6H_RGBUfloat:
  case WMTPixelFormatBC6H_RGBFloat:
  case WMTPixelFormatBC7_RGBAUnorm:
  case WMTPixelFormatBC7_RGBAUnorm_sRGB:
    block_size = 4u;
    break;
  default:
    break;
  }

  bool is_3d_tex = texture->textureType() == WMTTextureType3D;
  size_t texel_size = MTLGetTexelSize(texture->pixelFormat());
  size_t bytes_per_row_needed;
  if (!checkedMultiply(texel_size, (size_t(width_sub) + block_size - 1) / block_size, bytes_per_row_needed))
    return {InitStatus::OutOfMemory, 0};
  size_t bytes_per_image_needed;
  if (!checkedMultiply(bytes_per_row_needed, (size_t(height_sub) + block_size - 1) / block_size, bytes_per_image_needed))
    return {InitStatus::OutOfMemory, 0};
  size_t total_bytes_needed;
  if (!checkedMultiply(bytes_per_image_needed, depth_sub, total_bytes_needed))
    return {InitStatus::OutOfMemory, 0};

  std::lock_guard<dxmt::mutex> lock(mutex_);

  if (!cpu_command_heap || !upload_queue_ || !upload_queue_event_)
    return {InitStatus::OutOfMemory, 0};

  // A replacement stays local until its fill and dependent copy are ready.
  // Flush the old valid prefix before dropping the cached owner's reference.
  WMT::Reference<WMT::Buffer> candidate;
  bool replace = zero_buffer_size_ < total_bytes_needed || !zero_buffer_;
  if (replace) {
    WMTBufferInfo info{};
    info.length = total_bytes_needed;
    info.options = WMTResourceStorageModePrivate | WMTResourceHazardTrackingModeUntracked;
    candidate = device_.newBuffer(info);
    if (!candidate)
      return {InitStatus::OutOfMemory, 0};
    if (zero_buffer_ && !idle()) {
      auto submitted = flushInternal();
      if (!submitted)
        return submitted;
    }
  }
  auto status = replace
      ? prepareCpu<wmtcmd_blit_fillbuffer, wmtcmd_blit_copy_from_buffer_to_texture>()
      : prepareCpu<wmtcmd_blit_copy_from_buffer_to_texture>();
  if (status != InitStatus::Success)
    return {status, 0};
  if (!retainAllocation(allocation))
    return {InitStatus::OutOfMemory, 0};
  auto fill = replace ? allocateCpuHeap<wmtcmd_blit_fillbuffer>() : nullptr;
  auto copy = allocateCpuHeap<wmtcmd_blit_copy_from_buffer_to_texture>();
  WMT::Buffer zero = replace ? WMT::Buffer{candidate.handle} : WMT::Buffer{zero_buffer_.handle};
  {
    copy->type = WMTBlitCommandCopyFromBufferToTexture;
    copy->src = zero;
    copy->src_offset = 0;
    copy->bytes_per_row = bytes_per_row_needed;
    copy->bytes_per_image = is_3d_tex ? bytes_per_image_needed : 0;
    copy->size = {width_sub, height_sub, depth_sub};
    copy->dst = allocation->texture();
    copy->slice = slice;
    copy->level = level;
    copy->origin = {0, 0, 0};

    if (fill) {
      fill->type = WMTBlitCommandFillBuffer;
      fill->buffer = zero;
      fill->offset = 0;
      fill->length = total_bytes_needed;
      fill->value = 0;
      fill->next.set((wmtcmd_base *)copy);
      publishBlit((wmtcmd_base *)fill, (wmtcmd_base *)copy);
      zero_buffer_ = std::move(candidate);
      zero_buffer_size_ = total_bytes_needed;
    } else {
      publishBlit((wmtcmd_base *)copy, (wmtcmd_base *)copy);
    }
  }

  return {InitStatus::Success, current_seq_id_};
}

InitResult
ResourceInitializer::initWithData(
    const Texture *texture, TextureAllocation *allocation, uint32_t slice, uint32_t level, const void *data,
    size_t row_pitch, size_t depth_pitch, uint32_t format_flags
) {
  auto width_sub = mipExtent(texture->width(), level);
  auto height_sub = mipExtent(texture->height(), level);
  auto depth_sub = mipExtent(texture->depth(), level);

  auto block_size = 1u;

  switch (texture->pixelFormat()) {
  case WMTPixelFormatBC1_RGBA:
  case WMTPixelFormatBC1_RGBA_sRGB:
  case WMTPixelFormatBC2_RGBA:
  case WMTPixelFormatBC2_RGBA_sRGB:
  case WMTPixelFormatBC3_RGBA:
  case WMTPixelFormatBC3_RGBA_sRGB:
  case WMTPixelFormatBC4_RSnorm:
  case WMTPixelFormatBC4_RUnorm:
  case WMTPixelFormatBC5_RGUnorm:
  case WMTPixelFormatBC5_RGSnorm:
  case WMTPixelFormatBC6H_RGBUfloat:
  case WMTPixelFormatBC6H_RGBFloat:
  case WMTPixelFormatBC7_RGBAUnorm:
  case WMTPixelFormatBC7_RGBAUnorm_sRGB:
    block_size = 4u;
    break;
  case WMTPixelFormatDepth32Float_Stencil8:
    return initDepthStencilWithData(
        texture, allocation, slice, level, data, row_pitch, (format_flags & MTL_DXGI_FORMAT_EMULATED_D24)
    );
  default:
    break;
  }

  bool is_1d_tex = (texture->textureType() == WMTTextureType1D) || (texture->textureType() == WMTTextureType1DArray);
  bool is_3d_tex = texture->textureType() == WMTTextureType3D;
  size_t texel_size = MTLGetTexelSize(texture->pixelFormat());
  size_t bytes_per_row_needed;
  if (!checkedMultiply(texel_size, (size_t(width_sub) + block_size - 1) / block_size, bytes_per_row_needed))
    return {InitStatus::OutOfMemory, 0};
  size_t bytes_per_row_increment = is_1d_tex ? bytes_per_row_needed : row_pitch;
  size_t bytes_per_row_valid = is_1d_tex ? bytes_per_row_needed : std::min(row_pitch, bytes_per_row_needed);
  size_t bytes_per_image_needed;
  if (!checkedMultiply(bytes_per_row_needed, (size_t(height_sub) + block_size - 1) / block_size, bytes_per_image_needed))
    return {InitStatus::OutOfMemory, 0};
  size_t bytes_per_image_increment = is_3d_tex ? depth_pitch : bytes_per_image_needed;
  size_t bytes_per_image_valid = is_3d_tex ? std::min(depth_pitch, bytes_per_image_needed) : bytes_per_image_needed;
  size_t total_bytes_needed;
  if (!checkedMultiply(bytes_per_image_needed, depth_sub, total_bytes_needed))
    return {InitStatus::OutOfMemory, 0};

  bool copy_rows = bytes_per_row_increment != bytes_per_row_needed;
  size_t source_rows = copy_rows ? (size_t(height_sub) + block_size - 1) / block_size : 1;
  if (!checkedSourceRange(depth_sub, bytes_per_image_increment, source_rows,
                          bytes_per_row_increment, copy_rows ? bytes_per_row_valid : bytes_per_image_valid))
    return {InitStatus::OutOfMemory, 0};

  std::lock_guard<dxmt::mutex> lock(mutex_);
  auto status = prepareCpu<wmtcmd_blit_copy_from_buffer_to_texture>();
  if (status != InitStatus::Success)
    return {status, 0};
  UploadHeap::Reservation reservation{};
  status = reserveGpu({&total_bytes_needed, 1}, {&reservation, 1});
  if (status != InitStatus::Success)
    return {status, 0};
  if (!retainAllocation(allocation))
    return {InitStatus::OutOfMemory, 0};
  auto copy = allocateCpuHeap<wmtcmd_blit_copy_from_buffer_to_texture>();
  auto temp = reservation.block->buffer;
  auto temp_offset = reservation.offset;
  {
    for (auto depth = 0u; depth < depth_sub; depth++) {
      if (bytes_per_row_increment != bytes_per_row_needed) {
        for (auto row = 0u; row < ((size_t(height_sub) + block_size - 1) / block_size); row++) {
          auto offset = temp_offset + depth * bytes_per_image_needed + row * bytes_per_row_needed;
          auto length = bytes_per_row_valid;
          auto src_data = ptr_add(data, depth * bytes_per_image_increment + row * bytes_per_row_increment);
          temp.updateContents(offset, src_data, length);
        }
      } else {
        auto offset = temp_offset + depth * bytes_per_image_needed;
        auto length = bytes_per_image_valid;
        auto src_data = ptr_add(data, depth * bytes_per_image_increment);
        temp.updateContents(offset, src_data, length);
      }
    }

    copy->type = WMTBlitCommandCopyFromBufferToTexture;
    copy->src = temp;
    copy->src_offset = temp_offset;
    copy->bytes_per_row = bytes_per_row_needed;
    copy->bytes_per_image = is_3d_tex ? bytes_per_image_needed : 0;
    copy->size = {width_sub, height_sub, depth_sub};
    copy->dst = allocation->texture();
    copy->slice = slice;
    copy->level = level;
    copy->origin = {0, 0, 0};

    publishBlit((wmtcmd_base *)copy, (wmtcmd_base *)copy);
  }

  return {InitStatus::Success, current_seq_id_};
}

InitResult
ResourceInitializer::initDepthStencilWithData(
    const Texture *texture, TextureAllocation *allocation, uint32_t slice, uint32_t level, const void *data,
    size_t row_pitch, bool is_d24s8
) {
  auto width_sub = mipExtent(texture->width(), level);
  auto height_sub = mipExtent(texture->height(), level);

  size_t depth_bytes_per_row = size_t(4) * width_sub;
  size_t stencil_bytes_per_row = (size_t(width_sub) + 3) & ~size_t(3);
  size_t depth_bytes_per_image, stencil_bytes_per_image;
  if (!checkedMultiply(depth_bytes_per_row, height_sub, depth_bytes_per_image) ||
      !checkedMultiply(stencil_bytes_per_row, height_sub, stencil_bytes_per_image))
    return {InitStatus::OutOfMemory, 0};

  if (!checkedSourceRange(1, 0, height_sub, row_pitch, size_t(width_sub) * (is_d24s8 ? 4 : 8)))
    return {InitStatus::OutOfMemory, 0};

  std::lock_guard<dxmt::mutex> lock(mutex_);
  auto status = prepareCpu<wmtcmd_blit_copy_from_buffer_to_texture_withblitoption,
                           wmtcmd_blit_copy_from_buffer_to_texture_withblitoption>();
  if (status != InitStatus::Success)
    return {status, 0};
  const size_t sizes[] = {depth_bytes_per_image, stencil_bytes_per_image};
  UploadHeap::Reservation reservations[2]{};
  status = reserveGpu(sizes, reservations);
  if (status != InitStatus::Success)
    return {status, 0};
  if (!retainAllocation(allocation))
    return {InitStatus::OutOfMemory, 0};
  auto depth_copy = allocateCpuHeap<wmtcmd_blit_copy_from_buffer_to_texture_withblitoption>();
  auto stencil_copy = allocateCpuHeap<wmtcmd_blit_copy_from_buffer_to_texture_withblitoption>();
  auto depth_buf = reservations[0].block->buffer;
  auto depth_buf_offset = reservations[0].offset;
  auto stencil_buf = reservations[1].block->buffer;
  auto stencil_buf_offset = reservations[1].offset;
  {
    for (auto row = 0u; row < height_sub; row++) {
      auto src_data = ptr_add(data, row * row_pitch);

      if (is_d24s8) {
        for (auto x = 0u; x < width_sub; x++) {
          uint32_t packed;
          memcpy(&packed, (const char *)src_data + size_t(x) * 4, sizeof(packed));
          float depth_val = (float)(packed & 0xFFFFFF) / (float)(0xFFFFFF);
          uint8_t stencil_val = (uint8_t)(packed >> 24);
          depth_buf.updateContents(depth_buf_offset + row * depth_bytes_per_row + size_t(x) * 4, &depth_val, 4);
          stencil_buf.updateContents(stencil_buf_offset + row * stencil_bytes_per_row + x, &stencil_val, 1);
        }
      } else {
        for (auto x = 0u; x < width_sub; x++) {
          depth_buf.updateContents(depth_buf_offset + row * depth_bytes_per_row + size_t(x) * 4, (const char *)src_data + size_t(x) * 8, 4);
          stencil_buf.updateContents(stencil_buf_offset + row * stencil_bytes_per_row + x, (const uint8_t *)src_data + size_t(x) * 8 + 4, 1);
        }
      }
    }

    depth_copy->type = WMTBlitCommandCopyFromBufferToTextureWithBlitOption;
    depth_copy->src = depth_buf;
    depth_copy->src_offset = depth_buf_offset;
    depth_copy->bytes_per_row = depth_bytes_per_row;
    depth_copy->bytes_per_image = 0;
    depth_copy->size = {width_sub, height_sub, 1};
    depth_copy->dst = allocation->texture();
    depth_copy->slice = slice;
    depth_copy->level = level;
    depth_copy->origin = {0, 0, 0};
    depth_copy->options = WMTBlitOptionDepthFromDepthStencil;

    stencil_copy->type = WMTBlitCommandCopyFromBufferToTextureWithBlitOption;
    stencil_copy->src = stencil_buf;
    stencil_copy->src_offset = stencil_buf_offset;
    stencil_copy->bytes_per_row = stencil_bytes_per_row;
    stencil_copy->bytes_per_image = 0;
    stencil_copy->size = {width_sub, height_sub, 1};
    stencil_copy->dst = allocation->texture();
    stencil_copy->slice = slice;
    stencil_copy->level = level;
    stencil_copy->origin = {0, 0, 0};
    stencil_copy->options = WMTBlitOptionStencilFromDepthStencil;

    depth_copy->next.set((wmtcmd_base *)stencil_copy);
    publishBlit((wmtcmd_base *)depth_copy, (wmtcmd_base *)stencil_copy);
  }

  return {InitStatus::Success, current_seq_id_};
}

InitResult
ResourceInitializer::flushInternal() {
  if (idle())
    return {InitStatus::Success, 0};
  if (!upload_queue_ || !upload_queue_event_ || current_seq_id_ == std::numeric_limits<uint64_t>::max())
    return {InitStatus::SubmissionFailed, 0};
  auto pool = WMT::MakeAutoreleasePool();
  auto cmdbuf = upload_queue_.commandBuffer();
  // No sequence, reference, ring or list mutation until the complete valid
  // prefix has been encoded. Discard an uncommitted partial command buffer.
  if (!cmdbuf || !encode(cmdbuf))
    return {InitStatus::SubmissionFailed, 0};
  auto seq_id = current_seq_id_;
  cmdbuf.encodeSignalEvent(upload_queue_event_, seq_id);
  cmdbuf.commit();
  ++current_seq_id_;
  reset();
  cached_coherent_seq_id = upload_queue_event_.signaledValue();
  gpu_command_heap_allocator.free_blocks(cached_coherent_seq_id);
  return {InitStatus::Success, seq_id};
}

uint64_t
ResourceInitializer::flushToWait() {
  std::lock_guard<dxmt::mutex> lock(mutex_);

  if (idle()) {
    gpu_command_heap_allocator.free_blocks(cached_coherent_seq_id);
    if (cached_coherent_seq_id == current_seq_id_ - 1)
      return 0;
    cached_coherent_seq_id = upload_queue_event_.signaledValue();
    if (cached_coherent_seq_id == current_seq_id_ - 1)
      return 0;
    return current_seq_id_ - 1;
  }

  auto submitted = flushInternal();
  if (!submitted)
    throw MTLD3DError("Resource initialization submission failed");
  return submitted.sequence;
}

void
ResourceInitializer::reset() {
  cpu_command_heap_offset = 0;

  clear_render_pass_head.next = nullptr;
  clear_render_pass_tail = &clear_render_pass_head;

  blit_cmd_head.type = WMTBlitCommandNop;
  blit_cmd_head.next.set(nullptr);
  blit_cmd_tail = (wmtcmd_base *)&blit_cmd_head;

  ref_tracker.clear();
}

bool
ResourceInitializer::encode(WMT::CommandBuffer cmdbuf) {

  auto clear_pass = clear_render_pass_head.next;
  while (clear_pass) {
    auto r = cmdbuf.renderCommandEncoder(clear_pass->info);
    if (!r)
      return false;
    r.endEncoding();
    clear_pass = clear_pass->next;
  }

  if (blit_cmd_head.next.ptr) {
    auto b = cmdbuf.blitCommandEncoder();
    if (!b)
      return false;
    b.encodeCommands(&blit_cmd_head);
    b.endEncoding();
  }
  return true;
}

InitStatus
ResourceInitializer::reserveGpu(std::span<const size_t> sizes, std::span<UploadHeap::Reservation> reservations) {
  if (gpu_command_heap_allocator.allocateChecked(
          current_seq_id_, cached_coherent_seq_id, sizes, kResourceInitializerGpuUploadHeapAlignment, reservations))
    return InitStatus::Success;
  bool retry = false;
  if (!idle()) {
    auto submitted = flushInternal();
    if (!submitted)
      return submitted.status;
    retry = true;
  } else {
    auto coherent = upload_queue_event_.signaledValue();
    retry = coherent > cached_coherent_seq_id;
    cached_coherent_seq_id = coherent;
    if (retry)
      gpu_command_heap_allocator.free_blocks(coherent);
  }
  // One additional attempt only if submission/coherence could free storage.
  if (retry && gpu_command_heap_allocator.allocateChecked(
          current_seq_id_, cached_coherent_seq_id, sizes, kResourceInitializerGpuUploadHeapAlignment, reservations))
    return InitStatus::Success;
  return InitStatus::OutOfMemory;
}

bool
ResourceInitializer::retainAllocation(Allocation *allocation) {
  constexpr size_t block_size = 0x20;
  if (ref_tracker.track(allocation))
    return true;
  auto temp = allocateCpuHeap<intptr_t[block_size]>();
  if (!temp)
    return false;
  ref_tracker.addStorage(temp, block_size * sizeof(intptr_t));
  return ref_tracker.track(allocation);
}

} // namespace dxmt
