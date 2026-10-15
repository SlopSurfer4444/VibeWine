#pragma once
#include "../d3d11/cs2_trace.hpp"

#include "Metal.hpp"
#include "dxmt_allocation.hpp"
#include "dxmt_capture.hpp"
#include "dxmt_command.hpp"
#include "dxmt_command_list.hpp"
#include "dxmt_context.hpp"
#include "dxmt_occlusion_query.hpp"
#include "dxmt_resource_initializer.hpp"
#include "dxmt_ring_bump_allocator.hpp"
#include "dxmt_statistics.hpp"
#include "log/log.hpp"
#include "thread.hpp"
#include "util_cpu_fence.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

namespace dxmt {

template <typename T> class moveonly_list {
public:
  moveonly_list(T *storage, size_t size) : storage(storage), size_(size) {
    for (unsigned i = 0; i < size_; i++) {
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
    for (unsigned i = 0; i < size_; i++) {
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

class CommandQueue;

class CommandChunk {
public:
  CommandChunk(const CommandChunk &) = delete; // delete copy constructor

  void *
  allocate_cpu_heap(size_t size, size_t alignment);

  template <CommandWithContext<ArgumentEncodingContext> F>
  void
  emitcc(F &&func) {
    list_enc.emit(std::forward<F>(func), allocate_cpu_heap(list_enc.calculateCommandSize<F>(), 16));
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

  CommandList<ArgumentEncodingContext> list_enc;
  AllocationRefTracking ref_tracker;

  friend class CommandQueue;

public:
  CommandChunk() {}

  void
  reset() noexcept {
    signal_frame_latency_fence_ = ~0ull;
    readback = {};
    list_enc.reset();
    ref_tracker.clear();
    attached_cmdbuf = nullptr;
  }
};

class CommandQueue {
  const uint64_t trace_serial_=cs2trace::newSerial();

private:
  void CommitChunkInternal(CommandChunk &chunk, uint64_t seq);

  uint32_t EncodingThread();

  uint32_t WaitForFinishThread();

  std::atomic_uint64_t ready_for_encode = 1; // we start from 1, so 0 is always coherent
  std::atomic_uint64_t ready_for_commit = 1;
  std::atomic_uint64_t chunk_ongoing = 0;
  CpuFence cpu_coherent;
  CpuFence frame_latency_fence_;
  std::atomic_bool stopped;

  std::array<CommandChunk, kCommandChunkCount> chunks;
  uint64_t encoder_seq = 1;
  uint64_t frame_count = 0;
  std::atomic_uint32_t max_latency_ = 3;

  dxmt::thread encodeThread;
  dxmt::thread finishThread;
  WMT::Device device;
  WMT::Reference<WMT::CommandQueue> commandQueue;

  obj_handle_t shared_event_listener;
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
  uint64_t traceSerial() const {return trace_serial_;}
  InternalCommandLibrary cmd_library;
  ArgumentEncodingContext argument_encoding_ctx;
  WMT::Reference<WMT::SharedEvent> event;
  std::uint64_t current_event_seq_id = 0;
  FrameStatisticsContainer statistics;
  ResourceInitializer initializer;

  CommandQueue(WMT::Device device);

  ~CommandQueue();

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

  void
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
      { cs2trace::Scope trace{.type=11,.object=next_frame,.flags=max_latency};
        frame_latency_fence_.wait(next_frame - max_latency); }
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
  }

  uint32_t GetMaxLatency() { return max_latency_.load(std::memory_order_relaxed); }

  void SetMaxLatency(uint32_t value) { max_latency_.store(value, std::memory_order_relaxed); };

  void
  WaitCPUFence(uint64_t seq) {
    cpu_coherent.wait(seq);
  };

  std::tuple<WMT::Buffer, uint64_t>
  AllocateStagingBuffer(size_t size, size_t alignment) {
    auto [block, offset] = staging_allocator.allocate(ready_for_encode, cpu_coherent.signaledValue(), size, alignment);
    return {block.buffer, offset};
  }

  std::pair<WMT::Buffer, uint64_t>
  AllocateTempBuffer(uint64_t seq, size_t size, size_t alignment) {
    auto [block, offset] = copy_temp_allocator.allocate(seq, cpu_coherent.signaledValue(), size, alignment);
    return {block.buffer, offset};
  }

  AllocatedTempBufferSlice
  AllocateTempBuffer1(uint64_t seq, size_t size, size_t alignment) {
    auto [block, offset] = copy_temp_allocator.allocate(seq, cpu_coherent.signaledValue(), size, alignment);
    return {block.buffer, offset, block.gpu_address};
  }

  std::tuple<void *, WMT::Buffer, uint64_t>
  AllocateArgumentBuffer(uint64_t seq, size_t size) {
    auto [block, offset] = argbuf_allocator.allocate(seq, cpu_coherent.signaledValue(), size, 64);
    return {ptr_add(block.mapped_address, offset), block.buffer, offset};
  }

  void *
  AllocateCommandData(size_t size, size_t alignment) {
    auto [block, offset] =
        cpu_command_allocator.allocate(ready_for_encode, cpu_coherent.signaledValue(), size, alignment);
    return ptr_add(block.ptr, offset);
  }

  void Retain(uint64_t seq, Allocation *allocation);
};

} // namespace dxmt