/*
 * Copyright 2026 Feifan He for CodeWeavers
 * Copyright 2026 Marc-Aurel Zent for CodeWeavers
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
#include "../d3d11/cs2_trace.hpp"

#include "Metal.hpp"
#include "dxmt_allocation.hpp"
#include "dxmt_capture.hpp"
#include "dxmt_command.hpp"
#include "dxmt_command_list.hpp"
#include "dxmt_context.hpp"
#include "dxmt_counter.hpp"
#include "dxmt_idle_retirement.hpp"
#include "dxmt_idle_diagnostics.hpp"
#include "dxmt_occlusion_query.hpp"
#include "dxmt_resource_initializer.hpp"
#include "dxmt_residency_set_policy.hpp"
#include "dxmt_ring_bump_allocator.hpp"
#include "dxmt_statistics.hpp"
#include "log/log.hpp"
#include "thread.hpp"
#include "util_cpu_fence.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>

namespace dxmt {

template <typename T> class moveonly_list {
public:
  moveonly_list(T *storage, size_t size) : storage(storage), size_(size) {
    for (unsigned i = 0; storage && i < size_; i++) {
      new (storage + i) T();
    }
  }

  moveonly_list(const moveonly_list &copy) = delete;
  moveonly_list(moveonly_list &&move) {
    storage = move.storage;
    move.storage = nullptr;
    size_ = move.size_;
    move.size_ = 0;
  };

  ~moveonly_list() {
    for (unsigned i = 0; storage && i < size_; i++) {
      storage[i].~T();
    }
  };

  T &
  operator[](int index) {
    return storage[index];
  }

  std::span<T>
  span() const {
    return std::span<T>(storage, size_);
  }

  explicit operator bool() const { return storage || size_ == 0; }

  T *
  data() const {
    return storage;
  }

  size_t
  size() const {
    return size_;
  }

private:
  T *storage;
  size_t size_;
};

constexpr uint32_t kCommandChunkCount = 32;

struct QueueFailureWake {
  QueueFailureWake *next = nullptr;
  void (*notify)(void *) noexcept = nullptr;
  void *context = nullptr;
  bool registered = false;
};

class CommandQueue;

class CommandChunk {
public:
  CommandChunk(const CommandChunk &) = delete; // delete copy constructor

  void *
  allocate_cpu_heap(size_t size, size_t alignment);
  void fail_allocation() noexcept;
  void fail_encoding() noexcept;

  template <CommandWithContext<ArgumentEncodingContext> F>
  void
  emitcc(F &&func) {
    auto storage = allocate_cpu_heap(list_enc.calculateCommandSize<F>(), 16);
    if (!storage)
      return;
    try {
      list_enc.emit(std::forward<F>(func), storage);
    } catch (const std::bad_alloc &) {
      fail_allocation();
    } catch (...) {
      fail_encoding();
    }
  }

  void
  encode(WMT::CommandBuffer cmdbuf, ArgumentEncodingContext &enc) {
    enc.$$setEncodingContext(
      chunk_id,
      frame_
    );
#ifdef DXMT_DEBUG
    auto t0 = clock::now();
#endif
    list_enc.execute(enc);
    attached_cmdbuf = cmdbuf;
#ifdef DXMT_DEBUG
    auto t1 = clock::now();
#endif
    readback = enc.flushCommands(cmdbuf, chunk_id, chunk_event_id);
#ifdef DXMT_DEBUG
    auto t2 = clock::now();
#endif
#ifdef DXMT_DEBUG
    enc.updateFrameStatistics([&](auto &statistics) {
      statistics.encode_prepare_interval += (t1 - t0);
      statistics.encode_flush_interval += (t2 - t1);
    });
#endif
    if (signal_frame_latency_fence_ != ~0ull)
      enc.frameStatistics().encodingComplete(frame_);
  };

  uint32_t trace_generation = 0;
  uint64_t chunk_id;
  uint64_t chunk_event_id;
  uint64_t frame_;
  uint64_t signal_frame_latency_fence_;
  QueryReadbacks readback;
  uint64_t resource_initializer_event_id;

private:
  CommandQueue *queue;
  WMT::Reference<WMT::CommandBuffer> attached_cmdbuf;
  bool submitted = false;

  CommandList<ArgumentEncodingContext> list_enc;
  AllocationRefTracking ref_tracker;

  friend class CommandQueue;

public:
  CommandChunk() {}

  void
  reset(uint64_t traceQueue = 0) noexcept {
    submitted = false;
    signal_frame_latency_fence_ = ~0ull;
    readback = {};
    const auto gen = traceQueue && cs2trace::detailLimit() ? trace_generation : 0;
    // Unpublished/default chunks have no initialized sequence or frame. Avoid
    // reading those fields during shutdown resets with diagnostics unarmed.
    const auto trace_chunk = gen ? chunk_id : 0;
    const auto trace_frame = gen ? frame_ : 0;
    {
      cs2trace::StallScope trace(117, traceQueue, trace_chunk, trace_frame, gen);
      list_enc.reset();
    }
    {
      cs2trace::StallScope trace(118, traceQueue, trace_chunk, trace_frame, gen);
      ref_tracker.clear();
    }
    {
      cs2trace::StallScope trace(119, traceQueue, trace_chunk, trace_frame, gen);
      attached_cmdbuf = nullptr;
    }
  }
};

class CommandQueue {
  const uint64_t trace_serial_=cs2trace::newSerial();

private:
  void InitializeResidencySet();
  void CommitChunkInternal(CommandChunk &chunk, uint64_t seq);

  uint32_t EncodingThread();

  uint32_t WaitForFinishThread();

  std::atomic_uint64_t ready_for_encode = 1; // we start from 1, so 0 is always coherent
  std::atomic_uint64_t ready_for_commit = 1;
  std::atomic_uint64_t chunk_ongoing = 0;
  std::shared_ptr<CpuFence::WaitState> wait_state_ = std::make_shared<CpuFence::WaitState>();
  CpuFence cpu_coherent{wait_state_};
  CpuFence frame_latency_fence_{wait_state_};
  QueueFailureWake *failure_wakes_ = nullptr;
  dxmt::mutex failure_mutex_;
  std::atomic<int32_t> failure_reason_{0};
  std::atomic_bool stopped{false};
  dxmt::mutex shutdown_mutex_;
  bool shutdown_complete_ = false;

  std::array<CommandChunk, kCommandChunkCount> chunks;
  uint64_t encoder_seq = 1;
  uint64_t frame_count = 0;
  std::atomic_uint32_t max_latency_ = 3;

#if DXMT_IDLE_RETIREMENT
  // Construct before workers; completion sweeping has no independent worker.
  std::shared_ptr<IdleRetirementDomain> idle_retirement_ = std::make_shared<IdleRetirementDomain>();
  IdleRetirementDiagnostics idle_diagnostics_{env::getEnvVar("DXMT_IDLE_ATTRIBUTION") == "1"};
#endif
  dxmt::thread encodeThread;
  dxmt::thread finishThread;
  WMT::Device device;
  WMT::Reference<WMT::CommandQueue> commandQueue;
#if DXMT_RESIDENCY_SETS
  WMT::Reference<WMT::ResidencySet> residency_set_;
  struct ResidencyBufferCandidate : WMT::Buffer {
    BufferAllocation *owner;
    ResidencyBufferCandidate(WMT::Buffer buffer, BufferAllocation *owner) : WMT::Buffer(buffer), owner(owner) {}
  };
  struct ResidencyBufferReference : WMT::Buffer {
    // Keep the C++ identity alive as well as its native backing. Retain() can
    // then touch every view/copy use without confusing recycled owner pointers.
    Rc<BufferAllocation> owner;
    ResidencyBufferReference(ResidencyBufferCandidate candidate) : WMT::Buffer(candidate), owner(candidate.owner) {}
  };
  ResidencySetPolicy<ResidencyBufferReference> residency_policy_;
  struct ResidencyBackend {
    WMT::ResidencySet set;
    bool add(WMT::Allocation allocation) {
      set.addAllocations(&allocation, 1);
      set.commit();
      return true; // existing bridge has no fallible membership result
    }
    void remove(WMT::Allocation allocation) {
      set.removeAllocations(&allocation, 1);
      set.commit();
    }
  };
#endif

  obj_handle_t shared_event_listener = 0;
  dxmt::thread event_listener_thread;

  friend class CommandChunk;
  uint64_t
  GetNextEncoderId() {
    return encoder_seq++;
  }

  RingBumpState<StagingBufferBlockAllocator> staging_allocator;
  RingBumpState<GpuPrivateBufferBlockAllocator> copy_temp_allocator;
  RingBumpState<StagingBufferBlockAllocator, kCommandChunkGPUHeapSize> argbuf_allocator;
  RingBumpState<HostBufferBlockAllocator, kCommandChunkCPUHeapSize, dxmt::null_mutex> cpu_command_allocator;
  RingBumpState<HostBufferBlockAllocator, 0x1000 /* 4kB */> reftracker_storage_allocator;
  CaptureState capture_state;

public:
  std::shared_ptr<IdleRetirementDomain> idleRetirement() const {
#if DXMT_IDLE_RETIREMENT
    return idle_retirement_;
#else
    return {};
#endif
  }

  // The first error is terminal. Cancellation never advances a completion value.
  enum class Failure : int32_t { OutOfMemory = int32_t(0x8007000e), DeviceRemoved = int32_t(0x887a0005) };
  int32_t FailureReason() const noexcept { return failure_reason_.load(std::memory_order_acquire); }
  // GetDeviceRemovedReason has a documented DXGI-only reason set. Keep the
  // original HRESULT above for operation errors and diagnostics; unclassified
  // translator/native failure does not establish physical removal or reset.
  int32_t DeviceRemovedReason() const noexcept {
    return FailureReason() ? int32_t(0x887a0020) : 0; // DXGI_ERROR_DRIVER_INTERNAL_ERROR
  }
  void Fail(Failure reason) noexcept;
  void RegisterFailureWake(QueueFailureWake &wake) noexcept;
  void UnregisterFailureWake(QueueFailureWake &wake) noexcept;
  int32_t RegisterEvent(WMT::SharedEvent event, void *handle, uint64_t value) noexcept;
  std::shared_ptr<CpuFence::WaitState> WaitState() const { return wait_state_; }

  // Called only from the raw-buffer residency path on the encoding owner.
  // Returning false preserves the existing per-encoder declaration.
  bool TryReadOnlyResidency(BufferAllocation *allocation, DXMT_RESOURCE_RESIDENCY requested, uint64_t seq);

  uint64_t traceSerial() const {return trace_serial_;}
  InternalCommandLibrary cmd_library;
  ArgumentEncodingContext argument_encoding_ctx;
  WMT::Reference<WMT::SharedEvent> event;
  std::uint64_t current_event_seq_id = 0;
  FrameStatisticsContainer statistics;
  ResourceInitializer initializer;
  CounterPool counter_pool;

  CommandQueue(WMT::Device device);

  ~CommandQueue();

  // The device retirement worker calls this while its context/cache/pools are
  // still alive. Never call from one of this queue's workers. The destructor's
  // second call is harmless, and concurrent external calls wait for the join.
  void ShutdownAndJoin();

  CommandChunk *
  CurrentChunk() {
    auto id = ready_for_encode.load(std::memory_order_relaxed);
    return &chunks[id % kCommandChunkCount];
  };

  uint64_t
  CoherentSeqId() {
    return cpu_coherent.signaledValue();
  };

  uint64_t
  CurrentSeqId() {
    return ready_for_encode.load(std::memory_order_relaxed);
  };

  uint64_t
  GetNextEventSeqId() {
    return ++current_event_seq_id;
  };

  uint64_t
  GetCurrentEventSeqId() {
    return current_event_seq_id;
  };


  uint64_t
  SignaledEventSeqId() {
    return event.signaledValue();
  };

  obj_handle_t GetSharedEventListener() {
    return shared_event_listener;
  }

  /**
  This is not thread-safe!
  CurrentChunk & CommitCurrentChunk should be called on the same thread

  */
  void CommitCurrentChunk();

  uint64_t CurrentFrameSeq() {
    return frame_count + 1;
  }

  template <typename F>
  void UpdateCurrentFrameStatistics(F &&mutate) {
    statistics.update(frame_count, std::forward<F>(mutate));
  }

  // Called under the device/context submission lock immediately after the
  // present chunk commit: subsequent CPU submissions belong to the next frame.
  uint64_t AdvanceFrame() { return frame_count++; }

  bool
  PresentBoundary(uint64_t presented_frame, uint32_t max_latency) {
    const auto next_frame = presented_frame + 1;
#ifdef DXMT_DEBUG
    clock::duration wait_interval{};
#endif
    // After present N-th frame (N starts from 1), wait for (N - max_latency)-th frame.
    if (likely(next_frame > max_latency)) {
#ifdef DXMT_DEBUG
      auto t0 = clock::now();
#endif
      { cs2trace::PresentationScope exact(96,traceSerial(),presented_frame,next_frame-max_latency,max_latency);
        cs2trace::Scope trace{.type=11,.object=next_frame,.flags=max_latency};
        if (!frame_latency_fence_.wait(next_frame - max_latency)) return false; }
#ifdef DXMT_DEBUG
      wait_interval = clock::now() - t0;
#endif
    }
    // Attribute this present's CPU wait to its own frame and close only after
    // timing is recorded. Neither submission nor statistics locks span the wait.
#ifdef DXMT_DEBUG
    statistics.update(presented_frame, [&](auto &frame) {
      frame.present_latency_interval += wait_interval;
      frame.latency = max_latency;
    });
#endif
    statistics.cpuComplete(presented_frame);
    return !FailureReason();
  }

  uint32_t GetMaxLatency() { return max_latency_.load(std::memory_order_relaxed); }

  void SetMaxLatency(uint32_t value) { max_latency_.store(value, std::memory_order_relaxed); };

  bool
  WaitCPUFence(uint64_t seq) {
    return cpu_coherent.wait(seq);
  };

  template <typename Allocator>
  typename Allocator::Reservation
  Reserve(Allocator &allocator, uint64_t seq, size_t size, size_t alignment) {
    typename Allocator::Reservation reservation;
    if (FailureReason() || !allocator.allocateChecked(seq, cpu_coherent.signaledValue(),
          std::span<const size_t>(&size, 1), alignment, std::span(&reservation, 1)))
      throw std::bad_alloc{};
    return reservation;
  }

  std::tuple<WMT::Buffer, uint64_t>
  AllocateStagingBuffer(size_t size, size_t alignment) noexcept {
    try {
      auto r = Reserve(staging_allocator, ready_for_encode, size, alignment);
      return {r.block->buffer, r.offset};
    } catch (const std::bad_alloc &) {
      Fail(Failure::OutOfMemory);
      return {};
    }
  }

  std::pair<WMT::Buffer, uint64_t>
  AllocateTempBuffer(uint64_t seq, size_t size, size_t alignment) {
    auto r = Reserve(copy_temp_allocator, seq, size, alignment);
    return {r.block->buffer, r.offset};
  }

  AllocatedTempBufferSlice
  AllocateTempBuffer1(uint64_t seq, size_t size, size_t alignment) {
    auto r = Reserve(copy_temp_allocator, seq, size, alignment);
    return {r.block->buffer, r.offset, r.block->gpu_address};
  }

  std::tuple<void *, WMT::Buffer, uint64_t>
  AllocateArgumentBuffer(uint64_t seq, size_t size) {
    auto r = Reserve(argbuf_allocator, seq, std::max(size, size_t(1)), 64);
    return {ptr_add(r.block->mapped_address, r.offset), r.block->buffer, r.offset};
  }

  void *
  AllocateCommandData(size_t size, size_t alignment) noexcept {
    if (!size) return nullptr;
    try {
      auto r = Reserve(cpu_command_allocator, ready_for_encode, size, alignment);
      return ptr_add(r.block->ptr, r.offset);
    } catch (const std::bad_alloc &) {
      Fail(Failure::OutOfMemory);
      return nullptr;
    }
  }

  void Retain(uint64_t seq, Allocation *allocation);
};

} // namespace dxmt
