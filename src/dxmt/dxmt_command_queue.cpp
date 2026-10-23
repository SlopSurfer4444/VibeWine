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

#include "dxmt_command_queue.hpp"
#include "Metal.hpp"
#include "dxmt_statistics.hpp"
#include "util_env.hpp"
#include "util_win32_compat.h"
#include <atomic>

#define ASYNC_ENCODING 1

namespace dxmt {

void *
CommandChunk::allocate_cpu_heap(size_t size, size_t alignment) {
  return queue->AllocateCommandData(size, alignment);
}

CommandQueue::CommandQueue(WMT::Device device) :
    encodeThread([this]() { this->EncodingThread(); }),
    finishThread([this]() { this->WaitForFinishThread(); }),
    device(device),
    commandQueue(device.newCommandQueue(kCommandChunkCount)),
    shared_event_listener(SharedEventListener_create()),
    event_listener_thread([this]() { SharedEventListener_start(this->shared_event_listener); }),
    staging_allocator({
        device, WMTResourceOptionCPUCacheModeWriteCombined | WMTResourceHazardTrackingModeUntracked |
                    WMTResourceStorageModeManaged, false
    }),
    copy_temp_allocator({device, WMTResourceHazardTrackingModeUntracked | WMTResourceStorageModePrivate}),
    argbuf_allocator({
        device,
        WMTResourceHazardTrackingModeUntracked | WMTResourceCPUCacheModeWriteCombined | WMTResourceStorageModeShared
    }),
    cpu_command_allocator({}),
    reftracker_storage_allocator({}),
    cmd_library(device),
    argument_encoding_ctx(*this, device, cmd_library),
    initializer(device),
    counter_pool(device) {
  for (unsigned i = 0; i < kCommandChunkCount; i++) {
    auto &chunk = chunks[i];
    chunk.queue = this;
    chunk.reset();
  };
  event = device.newSharedEvent();

  std::string env = env::getEnvVar("DXMT_CAPTURE_FRAME");

  if (!env.empty()) {
    try {
      capture_state.scheduleNextFrameCapture(std::stoull(env));
    } catch (const std::invalid_argument &) {
    }
  }
}

CommandQueue::~CommandQueue() {
  ShutdownAndJoin();
}

void
CommandQueue::ShutdownAndJoin() {
  std::lock_guard<dxmt::mutex> lock(shutdown_mutex_);
  if (shutdown_complete_)
    return;
  cs2trace::ApiScope trace_shutdown(47,traceSerial());
  trace_shutdown.result=0;
  TRACE("Destructing command queue");
  stopped.store(true);
  ready_for_encode++;
  ready_for_encode.notify_one();
  ready_for_commit++;
  ready_for_commit.notify_one();
  SharedEventListener_destroy(shared_event_listener);
  encodeThread.join();
  finishThread.join();
  for (unsigned i = 0; i < kCommandChunkCount; i++) {
    auto &chunk = chunks[i];
    chunk.reset();
  };
  event_listener_thread.join();
  shutdown_complete_ = true;
  TRACE("Destructed command queue");
}

void
CommandQueue::CommitCurrentChunk() {
  auto chunk_id = ready_for_encode.load(std::memory_order_relaxed);
  auto &chunk = chunks[chunk_id % kCommandChunkCount];
  chunk.chunk_id = chunk_id;
  chunk.chunk_event_id = GetNextEventSeqId();
  chunk.frame_ = frame_count;
  chunk.trace_generation = cs2trace::generation();
  chunk.resource_initializer_event_id = initializer.flushToWait();
  UpdateCurrentFrameStatistics([](auto &statistics) { statistics.command_buffer_count++; });
#if ASYNC_ENCODING
  if (chunk.trace_generation) {
    auto t = cs2trace::now();
    cs2trace::record(chunk.trace_generation,20,t,t,traceSerial(),0,chunk_id,chunk.frame_);
    cs2trace::commitReason(chunk.trace_generation,traceSerial(),chunk_id,chunk.frame_);
  }
  ready_for_encode.fetch_add(1, std::memory_order_release);
  ready_for_encode.notify_one();

#ifdef DXMT_DEBUG
  auto t0 = clock::now();
#endif
  { cs2trace::Scope trace{.type=16,.object=chunk_id};
    chunk_ongoing.wait(kCommandChunkCount - 1, std::memory_order_acquire); }
  chunk_ongoing.fetch_add(1, std::memory_order_relaxed);
#ifdef DXMT_DEBUG
  auto t1 = clock::now();
  UpdateCurrentFrameStatistics([&](auto &statistics) { statistics.commit_interval += (t1 - t0); });
#endif

#else
  CommitChunkInternal(chunk, ready_for_encode.fetch_add(1, std::memory_order_relaxed));
#endif

  cpu_command_allocator.free_blocks(cpu_coherent.signaledValue());
}

void
CommandQueue::CommitChunkInternal(CommandChunk &chunk, uint64_t seq) {

  if (chunk.trace_generation) {
    auto t = cs2trace::now();
    cs2trace::record(chunk.trace_generation,21,t,t,traceSerial(),0,seq,chunk.frame_);
  }
  auto pool = WMT::MakeAutoreleasePool();

  switch (capture_state.getNextAction(chunk.frame_)) {
  case CaptureState::NextAction::StartCapture: {
    WMTCaptureInfo info;
    auto capture_mgr = WMT::CaptureManager::sharedCaptureManager();
    info.capture_object = device;
    info.destination = WMTCaptureDestinationGPUTraceDocument;
    char filename[1024];
    std::time_t now;
    std::time(&now);
    std::strftime(filename, 1024, "_%H'%M'%S_%m-%d-%y.gputrace", std::localtime(&now));
    auto fileUrl = env::getUnixPath(env::getExeBaseName() + "_F." + std::to_string(chunk.frame_) + filename);
    WARN("A new capture will be saved to ", fileUrl);
    info.output_url.set(fileUrl.c_str());

    capture_mgr.startCapture(info);
    break;
  }
  case CaptureState::NextAction::StopCapture: {
    auto capture_mgr = WMT::CaptureManager::sharedCaptureManager();
    capture_mgr.stopCapture();
    break;
  }
  case CaptureState::NextAction::Nothing: {
    if (capture_state.shouldCaptureNextFrame()) {
      capture_state.scheduleNextFrameCapture(chunk.frame_ + 1);
    }
    break;
  }
  }

  auto cmdbuf = commandQueue.commandBuffer();
  chunk.attached_cmdbuf = cmdbuf;
  if (chunk.resource_initializer_event_id) {
    cmdbuf.encodeWaitForEvent(initializer.event(), chunk.resource_initializer_event_id);
  }
  { cs2trace::Scope trace{.type=14,.object=seq};
    chunk.encode(chunk.attached_cmdbuf, this->argument_encoding_ctx); }
  { cs2trace::Scope trace{.type=15,.object=seq}; cmdbuf.commit(); }

  if (chunk.trace_generation) {
    auto t = cs2trace::now();
    cs2trace::record(chunk.trace_generation,22,t,t,traceSerial(),0,seq,chunk.frame_);
  }
  ready_for_commit.fetch_add(1, std::memory_order_release);
  ready_for_commit.notify_one();
}

uint32_t
CommandQueue::EncodingThread() {
#if ASYNC_ENCODING
  env::setThreadName("dxmt-encode-thread");
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
  uint64_t internal_seq = 1;
  while (!stopped.load()) {
    ready_for_encode.wait(internal_seq, std::memory_order_acquire);
    if (stopped.load())
      break;
    // perform...
    auto &chunk = chunks[internal_seq % kCommandChunkCount];
    CommitChunkInternal(chunk, internal_seq);
    internal_seq++;
  }
  TRACE("encoder thread gracefully terminates");
#endif
  return 0;
}

uint32_t
CommandQueue::WaitForFinishThread() {
  env::setThreadName("dxmt-finish-thread");
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
  uint64_t internal_seq = 1;
  while (!stopped.load()) {
    ready_for_commit.wait(internal_seq, std::memory_order_acquire);
    if (stopped.load())
      break;
    auto &chunk = chunks[internal_seq % kCommandChunkCount];
    if (chunk.attached_cmdbuf.status() <= WMTCommandBufferStatusScheduled) {
      cs2trace::Scope trace{.type=13,.object=internal_seq};
      chunk.attached_cmdbuf.waitUntilCompleted();
    }
    if (chunk.trace_generation && chunk.trace_generation == cs2trace::generation()) {
      auto t = cs2trace::now();
      auto status = chunk.attached_cmdbuf.status();
      cs2trace::record(chunk.trace_generation,23,t,t,traceSerial(),status,internal_seq,chunk.frame_);
      // Completed command-buffer properties only; no new wait or counter buffer.
      if (status == WMTCommandBufferStatusCompleted) {
        auto gpu_start = chunk.attached_cmdbuf.gpuStartTime();
        auto gpu_end = chunk.attached_cmdbuf.gpuEndTime();
        if (gpu_start && gpu_end >= gpu_start)
          cs2trace::record(chunk.trace_generation,24,gpu_start,gpu_end,traceSerial(),status,internal_seq,chunk.frame_);
      }
    }
    if (chunk.attached_cmdbuf.status() == WMTCommandBufferStatusError) {
      ERR("Device error at frame ", chunk.frame_, ": ", chunk.attached_cmdbuf.error().description().getUTF8String());
    }
    if (auto logs = chunk.attached_cmdbuf.logs()) {
      for (auto &log : logs.elements()) {
        ERR("Frame ", chunk.frame_, ": ", log.description().getUTF8String());
      }
    }

    if (chunk.signal_frame_latency_fence_ != ~0ull)
      frame_latency_fence_.signal(chunk.signal_frame_latency_fence_);

    // Readback destruction publishes query data before the CPU coherence fence.
    // Keep the birth identities before reset; detailed probes use CPU steady
    // time only and share the opt-in generation budget with compiler probes.
    const auto trace_gen = cs2trace::detailLimit() ? chunk.trace_generation : 0;
    const auto trace_frame = chunk.frame_;
    const auto readback_start = trace_gen ? cs2trace::now() : 0;
    chunk.readback = {};
    if (trace_gen)
      cs2trace::detailRecord(trace_gen,90,readback_start,cs2trace::now(),traceSerial(),0,internal_seq,trace_frame);
    chunk.reset();
    if (trace_gen) {
      auto t = cs2trace::now();
      cs2trace::detailRecord(trace_gen,91,t,t,traceSerial(),0,internal_seq,trace_frame);
    }
    cpu_coherent.signal(internal_seq);
    if (trace_gen) {
      auto t = cs2trace::now();
      cs2trace::detailRecord(trace_gen,92,t,t,traceSerial(),0,internal_seq,trace_frame);
    }
    chunk_ongoing.fetch_sub(1, std::memory_order_release);
    chunk_ongoing.notify_one();

    staging_allocator.free_blocks(internal_seq);
    copy_temp_allocator.free_blocks(internal_seq);
    argbuf_allocator.free_blocks(internal_seq);

    internal_seq++;
  }
  TRACE("finishing thread gracefully terminates");
  return 0;
}

void CommandQueue::Retain(uint64_t seq, Allocation* allocation) {
  auto &chunk = chunks[seq % kCommandChunkCount];
  auto &tracker = chunk.ref_tracker;
  constexpr size_t block_size = decltype(reftracker_storage_allocator)::block_size;
  while (unlikely(!tracker.track(allocation))) {
    auto [temp_buffer, _] = reftracker_storage_allocator.allocate(seq, cpu_coherent.signaledValue(), block_size, 1);
    tracker.addStorage(temp_buffer.ptr, block_size);
  }
};

} // namespace dxmt
