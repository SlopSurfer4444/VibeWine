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

#pragma once
#include "Metal.hpp"
#include "dxmt_buffer.hpp"
#include "dxmt_context.hpp"
#include "dxmt_ring_bump_allocator.hpp"
#include "dxmt_texture.hpp"
#include <limits>
#include <span>

namespace dxmt {

constexpr size_t kResourceInitializerCpuCommandHeapSize = 0x100000; // 1MB
constexpr size_t kResourceInitializerGpuUploadHeapSize = 0x2000000; // 32MB
constexpr size_t kResourceInitializerGpuUploadHeapAlignment = 256;
constexpr size_t kResourceInitializerChunks = 2;

static_assert(kResourceInitializerChunks > 1);

enum class InitStatus { Success, OutOfMemory, SubmissionFailed };

struct InitResult {
  InitStatus status;
  uint64_t sequence; // meaningful only on success; never use a failure as an event value
  explicit operator bool() const { return status == InitStatus::Success; }
};

class ResourceInitializer {
public:
  ResourceInitializer(WMT::Device device);
  ~ResourceInitializer();

  InitResult initWithZero(BufferAllocation *buffer, uint64_t offset, uint64_t length);

  InitResult initDepthStencilWithZero(
      const Texture *texture, TextureAllocation *allocation, uint32_t slice, uint32_t level, uint32_t dsv_planar
  );
  InitResult
  initRenderTargetWithZero(const Texture *texture, TextureAllocation *allocation, uint32_t slice, uint32_t level);
  InitResult initWithZero(const Texture *texture, TextureAllocation *allocation, uint32_t slice, uint32_t level);
  InitResult initWithData(
      const Texture *texture, TextureAllocation *allocation, uint32_t slice, uint32_t level, const void *data,
      size_t row_pitch, size_t depth_pitch, uint32_t format_flags
  );

  /*
   * Flush pending works and return the event id to wait
   * 0 may be returned, meaning no work to wait. Throws MTLD3DError if
   * command-buffer/encoder creation fails; the pending prefix remains queued.
   */
  uint64_t flushToWait();

  void
  wait(uint64_t seq_id, uint64_t timeout = -1) {
    if (cached_coherent_seq_id >= seq_id)
      return;
    upload_queue_event_.waitUntilSignaledValue(seq_id, timeout);
  }

  WMT::Event
  event() {
    return upload_queue_event_;
  }

private:
  InitResult flushInternal();

  InitResult initDepthStencilWithData(
      const Texture *texture, TextureAllocation *allocation, uint32_t slice, uint32_t level, const void *data,
      size_t row_pitch, bool is_d24s8
  );

  struct ClearRenderPassInfo {
    WMTRenderPassInfo info;
    ClearRenderPassInfo *next;
  };

  bool
  idle() {
    if (blit_cmd_head.next.ptr)
      return false;
    if (clear_render_pass_head.next)
      return false;
    return true;
  }

  void reset();

  bool encode(WMT::CommandBuffer cmdbuf);

  // Check capacity without publishing nodes or retaining resources. The single
  // possible tracker spill is included even when its inline storage has room.
  template <typename... Commands>
  InitStatus prepareCpu() {
    if (!cpu_command_heap || !upload_queue_ || !upload_queue_event_)
      return InitStatus::OutOfMemory;
    if (current_seq_id_ == std::numeric_limits<uint64_t>::max())
      return InitStatus::SubmissionFailed;
    auto fits = [&] {
      size_t cursor = cpu_command_heap_offset;
      auto add = [&](size_t size, size_t alignment) {
        if (cursor > kResourceInitializerCpuCommandHeapSize)
          return false;
        size_t padding = (alignment - cursor % alignment) % alignment;
        if (padding > kResourceInitializerCpuCommandHeapSize - cursor)
          return false;
        cursor += padding;
        if (size > kResourceInitializerCpuCommandHeapSize - cursor)
          return false;
        cursor += size;
        return true;
      };
      return add(sizeof(intptr_t[32]), alignof(intptr_t[32])) &&
             (add(sizeof(Commands), alignof(Commands)) && ...);
    };
    if (fits())
      return InitStatus::Success;
    if (idle())
      return InitStatus::OutOfMemory;
    auto submitted = flushInternal();
    if (!submitted)
      return submitted.status;
    return fits() ? InitStatus::Success : InitStatus::OutOfMemory;
  }

  template <typename T>
  T *allocateCpuHeap() {
    constexpr size_t size = sizeof(T);
    constexpr size_t alignment = alignof(T);
    if (!cpu_command_heap || cpu_command_heap_offset > kResourceInitializerCpuCommandHeapSize)
      return nullptr;
    size_t adjustment = (alignment - cpu_command_heap_offset % alignment) % alignment;
    if (adjustment > kResourceInitializerCpuCommandHeapSize - cpu_command_heap_offset)
      return nullptr;
    auto aligned = cpu_command_heap_offset + adjustment;
    if (size > kResourceInitializerCpuCommandHeapSize - aligned)
      return nullptr;
    cpu_command_heap_offset = aligned + size;
    return reinterpret_cast<T *>(ptr_add(cpu_command_heap, aligned));
  }

  void publishBlit(wmtcmd_base *first, wmtcmd_base *last) {
    last->next.set(nullptr);
    blit_cmd_tail->next.set(first);
    blit_cmd_tail = last;
  }

  void publishClear(ClearRenderPassInfo *pass) {
    pass->next = nullptr;
    clear_render_pass_tail->next = pass;
    clear_render_pass_tail = pass;
  }

  using UploadHeap = RingBumpState<StagingBufferBlockAllocator, kResourceInitializerGpuUploadHeapSize>;
  // The generic ring accepts an exclusive reuse frontier. Only this upload
  // owner has an inclusive event completion value: signal N follows every
  // source read in submission N. Never advance across the pending generation
  // or wrap the event sentinel. The experiment remains disabled by default.
  static uint64_t uploadReuseBoundary(uint64_t current, uint64_t completed) {
#if defined(DXMT_INITIALIZER_INCLUSIVE_REUSE) && DXMT_INITIALIZER_INCLUSIVE_REUSE
    if (!current || current == std::numeric_limits<uint64_t>::max() || completed >= current)
      return 0;
    return completed + 1; // completed < current <= UINT64_MAX - 1
#else
    (void)current;
    return completed;
#endif
  }

  InitStatus reserveGpu(std::span<const size_t> sizes, std::span<UploadHeap::Reservation> reservations);

  void refreshUploadCompletion(); // caller holds mutex_

  bool retainAllocation(Allocation *allocation);

  uint64_t current_seq_id_ = 1;
  uint64_t cached_coherent_seq_id = 0;
  // At most one speculative completion read for each submitted generation.
  // Allocation failure keeps its existing independent refresh/retry path.
  uint64_t upload_poll_seq_id_ = 0;
  WMT::Device device_;
  WMT::Reference<WMT::CommandQueue> upload_queue_;
  WMT::Reference<WMT::SharedEvent> upload_queue_event_;
  WMT::Reference<WMT::Buffer> zero_buffer_;
  size_t zero_buffer_size_ = 0;
  dxmt::mutex mutex_;

  void *cpu_command_heap;
  size_t cpu_command_heap_size;
  size_t cpu_command_heap_offset;

  UploadHeap gpu_command_heap_allocator;

  wmtcmd_blit_nop blit_cmd_head;
  wmtcmd_base *blit_cmd_tail;

  ClearRenderPassInfo clear_render_pass_head;
  ClearRenderPassInfo *clear_render_pass_tail;

  AllocationRefTracking ref_tracker;
};

} // namespace dxmt
