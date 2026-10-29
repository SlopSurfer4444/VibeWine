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
#include "../d3d11/cs2_trace_completion.hpp"
#include "util_env.hpp"
#include "util_win32_compat.h"
#include <atomic>

#define ASYNC_ENCODING 1

namespace dxmt {

void *
CommandChunk::allocate_cpu_heap(size_t size, size_t alignment) {
  return queue->AllocateCommandData(size, alignment);
}

void CommandChunk::fail_allocation() noexcept { queue->Fail(CommandQueue::Failure::OutOfMemory); }
void CommandChunk::fail_encoding() noexcept { queue->Fail(CommandQueue::Failure::DeviceRemoved); }

CommandQueue::CommandQueue(WMT::Device device) :
    device(device),
    commandQueue(device.newCommandQueue(kCommandChunkCount)),
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

#if DXMT_RESIDENCY_SETS
  InitializeResidencySet();
#endif

  std::string env = env::getEnvVar("DXMT_CAPTURE_FRAME");

  if (!env.empty()) {
    try {
      capture_state.scheduleNextFrameCapture(std::stoull(env));
    } catch (const std::invalid_argument &) {
    }
  }
  // Constructors may fail before the queue becomes usable. Start no worker
  // until every context/allocator/native owner has finished construction.
  try {
    if (!commandQueue || !event) throw std::bad_alloc{};
    shared_event_listener = SharedEventListener_create();
#ifndef DXMT_NATIVE
    if (!shared_event_listener) throw std::bad_alloc{};
#endif
    event_listener_thread = dxmt::thread([this]() { SharedEventListener_start(shared_event_listener); });
    encodeThread = dxmt::thread([this]() { EncodingThread(); });
    finishThread = dxmt::thread([this]() { WaitForFinishThread(); });
  } catch (...) {
    stopped.store(true, std::memory_order_release);
    wait_state_->cancel();
    if (encodeThread.joinable()) encodeThread.join();
    if (finishThread.joinable()) finishThread.join();
    if (shared_event_listener) SharedEventListener_destroy(shared_event_listener);
    if (event_listener_thread.joinable()) event_listener_thread.join();
    throw;
  }

}

CommandQueue::~CommandQueue() {
  ShutdownAndJoin();
}

void
CommandQueue::InitializeResidencySet() {
#if DXMT_RESIDENCY_SETS
  uint64_t major = 0, minor = 0, patch = 0;
  WMTGetOSVersion(&major, &minor, &patch);
  if (major >= 15) {
    WMT::Reference<WMT::Error> error;
    residency_set_ = device.newResidencySet(256, error);
    if (residency_set_)
      commandQueue.addResidencySet(residency_set_);
    else
      WARN("Bounded residency set unavailable; preserving resource declarations");
  }
#endif
}

void
CommandQueue::ShutdownAndJoin() {
  std::lock_guard<dxmt::mutex> lock(shutdown_mutex_);
  if (shutdown_complete_)
    return;
  cs2trace::ApiScope trace_shutdown(47,traceSerial());
  trace_shutdown.result=0;
  TRACE("Destructing command queue");
  {
    std::lock_guard<dxmt::mutex> completion_lock(failure_mutex_);
    stopped.store(true, std::memory_order_release);
    wait_state_->cancel();
  }
  encodeThread.join();
  finishThread.join();
  // Canceled CPU waits are not proof that the GPU released borrowed storage.
  // The encoding owner is stopped before examining its submitted markers.
  for (auto &chunk : chunks) {
    if (chunk.submitted && chunk.attached_cmdbuf &&
        chunk.attached_cmdbuf.status() <= WMTCommandBufferStatusScheduled)
      chunk.attached_cmdbuf.waitUntilCompleted();
  }
  argument_encoding_ctx.discardCommands();
  if (capture_state.takeCaptureOwnership())
    WMT::CaptureManager::sharedCaptureManager().stopCapture();
#if DXMT_RESIDENCY_SETS
  if (residency_set_) {
    ResidencyBackend backend{residency_set_};
    residency_policy_.clearAfterDrain(backend);
  }
#endif
  SharedEventListener_destroy(shared_event_listener);
  for (unsigned i = 0; i < kCommandChunkCount; i++) {
    auto &chunk = chunks[i];
    chunk.reset();
  };
  event_listener_thread.join();
  shutdown_complete_ = true;
  TRACE("Destructed command queue");
}

void
CommandQueue::Fail(Failure reason) noexcept {
  std::lock_guard<dxmt::mutex> lock(failure_mutex_);
  int32_t expected = 0;
  if (!failure_reason_.compare_exchange_strong(expected, int32_t(reason), std::memory_order_acq_rel))
    return;
  wait_state_->cancel();
  for (auto wake = failure_wakes_; wake; wake = wake->next)
    if (wake->notify) wake->notify(wake->context);
  // Reserved listener control: cancel pending registrations on its PE owner.
  MTLSharedEvent_setWin32EventAtValue(0, shared_event_listener, nullptr, 0);
}

void
CommandQueue::RegisterFailureWake(QueueFailureWake &wake) noexcept {
  std::lock_guard<dxmt::mutex> lock(failure_mutex_);
  if (wake.registered) return;
  wake.next = failure_wakes_;
  failure_wakes_ = &wake;
  wake.registered = true;
  if (FailureReason() && wake.notify) wake.notify(wake.context);
}

void
CommandQueue::UnregisterFailureWake(QueueFailureWake &wake) noexcept {
  std::lock_guard<dxmt::mutex> lock(failure_mutex_);
  auto cursor = &failure_wakes_;
  while (*cursor && *cursor != &wake) cursor = &(*cursor)->next;
  if (*cursor) *cursor = wake.next;
  wake.next = nullptr;
  wake.registered = false;
}

int32_t
CommandQueue::RegisterEvent(WMT::SharedEvent shared, void *handle, uint64_t value) noexcept {
  if (!handle) return int32_t(0x80070057); // E_INVALIDARG
  if (auto failure = FailureReason()) {
    SetEvent(handle);
    return failure;
  }
  const auto status = MTLSharedEvent_setWin32EventAtValueChecked(shared.handle, shared_event_listener, handle, value);
  if (status) {
    Fail(status == int32_t(0xc0000017) ? Failure::OutOfMemory : Failure::DeviceRemoved);
    // Rejected registration has no pending native ticket. Deliver synchronously
    // before returning failure so callers may close/reuse their event safely.
    SetEvent(handle);
    return FailureReason();
  }
  // Once admitted, the listener owns exactly one delivery, including cancellation.
  // Returning failure here would let the caller close a still-pending HANDLE.
  return 0;
}

void
CommandQueue::CommitCurrentChunk() {
  try {
  if (FailureReason() || stopped.load(std::memory_order_acquire)) return;
  // Reserve the slot before publishing; finish may retire immediately.
  for (;;) {
    auto epoch = wait_state_->epoch.load(std::memory_order_acquire);
    if (FailureReason() || stopped.load(std::memory_order_acquire)) return;
    if (chunk_ongoing.load(std::memory_order_acquire) < kCommandChunkCount - 1) break;
    wait_state_->epoch.wait(epoch, std::memory_order_acquire);
  }
  auto initializer_event = initializer.flushToWait();
  auto chunk_id = ready_for_encode.load(std::memory_order_relaxed);
  auto &chunk = chunks[chunk_id % kCommandChunkCount];
  chunk.chunk_id = chunk_id;
  chunk.chunk_event_id = GetNextEventSeqId();
  chunk.frame_ = frame_count;
  chunk.trace_generation = cs2trace::generation();
  chunk.resource_initializer_event_id = initializer_event;
  UpdateCurrentFrameStatistics([](auto &statistics) { statistics.command_buffer_count++; });
  if (chunk.trace_generation) {
    auto t = cs2trace::now();
    cs2trace::record(chunk.trace_generation,20,t,t,traceSerial(),0,chunk_id,chunk.frame_);
    cs2trace::commitReason(chunk.trace_generation,traceSerial(),chunk_id,chunk.frame_);
  }
  std::lock_guard<dxmt::mutex> admission_lock(failure_mutex_);
  if (FailureReason() || stopped.load(std::memory_order_acquire)) return;
  chunk_ongoing.fetch_add(1, std::memory_order_relaxed);
  ready_for_encode.fetch_add(1, std::memory_order_release);
  wait_state_->notify();
  cpu_command_allocator.free_blocks(cpu_coherent.signaledValue());
  } catch (const std::bad_alloc &) {
    Fail(Failure::OutOfMemory);
  } catch (...) {
    Fail(Failure::DeviceRemoved);
  }
}

void
CommandQueue::CommitChunkInternal(CommandChunk &chunk, uint64_t seq) {

  if (chunk.trace_generation) {
    auto t = cs2trace::now();
    cs2trace::record(chunk.trace_generation,21,t,t,traceSerial(),0,seq,chunk.frame_);
  }
  auto pool = WMT::MakeAutoreleasePool();

#if DXMT_RESIDENCY_SETS
  if (residency_set_) {
    ResidencyBackend backend{residency_set_};
    residency_policy_.retire(cpu_coherent.signaledValue(), backend);
  }
#endif

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

    capture_state.captureStarted(capture_mgr.startCapture(info));
    break;
  }
  case CaptureState::NextAction::StopCapture: {
    auto capture_mgr = WMT::CaptureManager::sharedCaptureManager();
    if (capture_state.takeCaptureOwnership())
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
  if (!cmdbuf) {
    Fail(Failure::OutOfMemory);
    return;
  }
  chunk.attached_cmdbuf = cmdbuf;
  if (chunk.resource_initializer_event_id) {
    cmdbuf.encodeWaitForEvent(initializer.event(), chunk.resource_initializer_event_id);
  }
  { cs2trace::Scope trace{.type=14,.object=seq};
    chunk.encode(chunk.attached_cmdbuf, this->argument_encoding_ctx); }
  std::lock_guard<dxmt::mutex> submit_lock(failure_mutex_);
  if (FailureReason() || stopped.load(std::memory_order_acquire)) return;
  { cs2trace::Scope trace{.type=15,.object=seq}; cmdbuf.commit(); }
  chunk.submitted = true;

  if (chunk.trace_generation) {
    auto t = cs2trace::now();
    cs2trace::record(chunk.trace_generation,22,t,t,traceSerial(),0,seq,chunk.frame_);
  }
  ready_for_commit.fetch_add(1, std::memory_order_release);
  wait_state_->notify();
}

uint32_t
CommandQueue::EncodingThread() {
#if ASYNC_ENCODING
  env::setThreadName("dxmt-encode-thread");
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
  uint64_t internal_seq = 1;
  while (!stopped.load()) {
    const auto epoch = wait_state_->epoch.load(std::memory_order_acquire);
    if (FailureReason() || stopped.load(std::memory_order_acquire)) break;
    if (ready_for_encode.load(std::memory_order_acquire) == internal_seq) {
      wait_state_->epoch.wait(epoch, std::memory_order_acquire);
      continue;
    }
    if (stopped.load() || FailureReason())
      break;
    // perform...
    auto &chunk = chunks[internal_seq % kCommandChunkCount];
    try {
      CommitChunkInternal(chunk, internal_seq);
    } catch (const std::bad_alloc &) {
      Fail(Failure::OutOfMemory);
    } catch (...) {
      Fail(Failure::DeviceRemoved);
    }
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
    const auto epoch = wait_state_->epoch.load(std::memory_order_acquire);
    if (FailureReason() || stopped.load(std::memory_order_acquire)) break;
    if (ready_for_commit.load(std::memory_order_acquire) == internal_seq) {
      wait_state_->epoch.wait(epoch, std::memory_order_acquire);
      continue;
    }
    if (stopped.load() || FailureReason())
      break;
    try {
    auto &chunk = chunks[internal_seq % kCommandChunkCount];
    const auto status_before = chunk.attached_cmdbuf.status();
    if (status_before <= WMTCommandBufferStatusScheduled) {
      cs2trace::StallCompletionWait completion(chunk.trace_generation, traceSerial(), internal_seq, chunk.frame_,
                                             chunk.signal_frame_latency_fence_, chunk.resource_initializer_event_id);
      if (completion.active()) {
        // SharedEvent is the initializer event's actual native class. One
        // opt-in getter samples its observed value immediately before waiting.
        if (chunk.resource_initializer_event_id) {
          WMT::SharedEvent initializer_event;
          initializer_event.handle = initializer.event().handle;
          completion.initializer_observed = initializer_event.signaledValue();
        }
        completion.ready_encode = ready_for_encode.load(std::memory_order_acquire);
        completion.ready_commit = ready_for_commit.load(std::memory_order_acquire);
        completion.ongoing = chunk_ongoing.load(std::memory_order_relaxed);
        completion.coherent = cpu_coherent.signaledValue();
        completion.latency = frame_latency_fence_.signaledValue();
      }
      bool slow_completion = false;
      {
        cs2trace::Scope trace{.type=13,.object=internal_seq};
        completion.begin();
        chunk.attached_cmdbuf.waitUntilCompleted();
        slow_completion = completion.finish();
      }
      if (slow_completion) {
        const auto status_after = chunk.attached_cmdbuf.status();
        uint64_t gpu_start = 0, gpu_end = 0;
        if (status_after == WMTCommandBufferStatusCompleted) {
          gpu_start = chunk.attached_cmdbuf.gpuStartTime();
          gpu_end = chunk.attached_cmdbuf.gpuEndTime();
        }
        completion.publish(status_before, status_after, gpu_start, gpu_end);
      }
    }
    if (chunk.trace_generation && chunk.trace_generation == cs2trace::generation()) {
      auto t = cs2trace::now();
      auto status = chunk.attached_cmdbuf.status();
      cs2trace::record(chunk.trace_generation,23,t,t,traceSerial(),status,internal_seq,chunk.frame_);
      // Completed command-buffer properties only; no new wait or counter buffer.
      if (status == WMTCommandBufferStatusCompleted && cs2trace::profileAllows(24)) {
        auto gpu_start = chunk.attached_cmdbuf.gpuStartTime();
        auto gpu_end = chunk.attached_cmdbuf.gpuEndTime();
        if (gpu_start && gpu_end >= gpu_start)
          cs2trace::record(chunk.trace_generation,24,gpu_start,gpu_end,traceSerial(),status,internal_seq,chunk.frame_);
      }
    }
    if (chunk.attached_cmdbuf.status() == WMTCommandBufferStatusError) {
      ERR("Device error at frame ", chunk.frame_, ": ", chunk.attached_cmdbuf.error().description().getUTF8String());
    }
    if (chunk.attached_cmdbuf.status() != WMTCommandBufferStatusCompleted) {
      Fail(Failure::DeviceRemoved);
      break;
    }
    std::lock_guard<dxmt::mutex> completion_lock(failure_mutex_);
    if (FailureReason()) break;
    if (auto logs = chunk.attached_cmdbuf.logs()) {
      for (auto &log : logs.elements()) {
        ERR("Frame ", chunk.frame_, ": ", log.description().getUTF8String());
      }
    }

    if (chunk.signal_frame_latency_fence_ != ~0ull)
      frame_latency_fence_.signal(chunk.signal_frame_latency_fence_);

    // Explicit publication is permitted only after verified native completion.
    // Keep the birth identities before reset; detailed probes use CPU steady
    // time only and share the opt-in generation budget with compiler probes.
    const auto trace_gen = cs2trace::detailLimit() ? chunk.trace_generation : 0;
    const auto trace_frame = chunk.frame_;
    const auto readback_start = trace_gen ? cs2trace::now() : 0;
    chunk.readback.publish();
    chunk.readback = {};
    if (trace_gen)
      cs2trace::detailRecord(trace_gen,90,readback_start,cs2trace::now(),traceSerial(),0,internal_seq,trace_frame);
    {
      cs2trace::StallScope trace(110, traceSerial(), internal_seq, trace_frame);
      chunk.reset(traceSerial());
    }
    if (trace_gen) {
      auto t = cs2trace::now();
      cs2trace::detailRecord(trace_gen,91,t,t,traceSerial(),0,internal_seq,trace_frame);
    }
    cpu_coherent.signal(internal_seq);
#if DXMT_IDLE_RETIREMENT
    const auto idle_sample = [&] {
      cs2trace::StallScope retirement_trace(108, traceSerial(), internal_seq, trace_frame);
      return idle_retirement_->sweep(internal_seq);
    }();
    if (idle_diagnostics_.enabled()) {
      char line[1024];
      if (idle_diagnostics_.record(cs2trace::now(), idle_sample, GetCurrentProcessId(), traceSerial(), line))
        Logger::diagnostic(line);
    }
#endif
    if (trace_gen) {
      auto t = cs2trace::now();
      cs2trace::detailRecord(trace_gen,92,t,t,traceSerial(),0,internal_seq,trace_frame);
    }
    chunk_ongoing.fetch_sub(1, std::memory_order_release);
    wait_state_->notify();

    {
      cs2trace::StallScope trace(109, traceSerial(), internal_seq, trace_frame);
      staging_allocator.free_blocks(internal_seq);
      copy_temp_allocator.free_blocks(internal_seq);
      argbuf_allocator.free_blocks(internal_seq);
      // reset() detached this completed chunk's borrowed tracking storage
      // before publishing coherence. Keep pending chunks, age completed bursts.
      reftracker_storage_allocator.free_blocks(internal_seq);
    }

    internal_seq++;
    } catch (const std::bad_alloc &) {
      Fail(Failure::OutOfMemory);
    } catch (...) {
      Fail(Failure::DeviceRemoved);
    }
  }
  TRACE("finishing thread gracefully terminates");
  return 0;
}

void CommandQueue::Retain(uint64_t seq, Allocation* allocation) {
#if DXMT_RESIDENCY_SETS
  residency_policy_.touch(allocation, seq);
#endif
  auto &chunk = chunks[seq % kCommandChunkCount];
  auto &tracker = chunk.ref_tracker;
  constexpr size_t block_size = decltype(reftracker_storage_allocator)::block_size;
  while (unlikely(!tracker.track(allocation))) {
    auto reservation = Reserve(reftracker_storage_allocator, seq, block_size, 1);
    tracker.addStorage(reservation.block->ptr, block_size);
  }
};

bool
CommandQueue::TryReadOnlyResidency(BufferAllocation *allocation, DXMT_RESOURCE_RESIDENCY requested, uint64_t seq) {
#if DXMT_RESIDENCY_SETS
  if (!residency_set_ || !allocation)
    return false;
  const auto flags = allocation->flags();
  // GpuManaged is a storage mode, not Metal hazard tracking: these standalone
  // buffers are created untracked. GpuReadonly is set for D3D11 immutable data.
  // Its NoTracking alias is also used by counters, which never call this hook.
  const bool eligible = flags.test(BufferAllocationFlag::GpuReadonly) &&
                        !flags.any(BufferAllocationFlag::AllocatedOnHeap, BufferAllocationFlag::CpuPlaced,
                                   BufferAllocationFlag::OwnedByCommandList) &&
                        (requested & DXMT_RESOURCE_RESIDENCY_READ) && !(requested & DXMT_RESOURCE_RESIDENCY_WRITE);
  if (!eligible)
    return false;
  const auto buffer = allocation->buffer();
  const auto owner = static_cast<Allocation *>(allocation);
  const auto cached_charge = residency_policy_.cachedCharge(buffer.handle, owner);
  const auto bytes = cached_charge ? cached_charge : buffer.allocatedSize();
  ResidencyBackend backend{residency_set_};
  return residency_policy_.cover(ResidencyBufferCandidate{buffer, allocation}, bytes, seq, eligible, backend, owner);
#else
  (void)allocation;
  (void)requested;
  (void)seq;
  return false;
#endif
}

} // namespace dxmt
