#include "dxmt_staging.hpp"
#include <cassert>

namespace dxmt {

StagingResource::StagingResource(
  WMT::Device device, uint64_t length, uint32_t bytes_per_row, uint32_t bytes_per_image
) :
    bytesPerRow(bytes_per_row),
    bytesPerImage(bytes_per_image),
    length(length),
    device_(device) {
  buffer_ = new Buffer(length, device);
  immediate_name_ = allocate(0);
  buffer_->rename(allocation(immediate_name_));
}

void
StagingResource::incRef() {
  refcount_.fetch_add(1u, std::memory_order_acquire);
};

void
StagingResource::decRef() {
  if (refcount_.fetch_sub(1u, std::memory_order_release) == 1u)
    delete this;
};

void
StagingResource::useCopyDestination(uint64_t seq_id) {
  assert(!mapped && "write to staging resource while being mapped");
  // coherent read-after-write
  cpu_coherent_after_finished_seq_id = std::max(seq_id, cpu_coherent_after_finished_seq_id);
  // coherent write-after-write
  gpu_occupied_until_finished_seq_id = std::max(seq_id, gpu_occupied_until_finished_seq_id);
}
void
StagingResource::useCopySource(uint64_t seq_id) {
  assert(!mapped && "read from staging resource while being mapped");
  gpu_occupied_until_finished_seq_id = std::max(seq_id, gpu_occupied_until_finished_seq_id);
}

StagingMapResult
StagingResource::tryMap(uint64_t coherent_seq_id, bool read, bool write) {
  std::lock_guard<dxmt::mutex> lock(mutex_);
  if (mapped)
    return StagingMapResult::Mapped;
  trimCompletedAllocations(coherent_seq_id);
  if (read && coherent_seq_id < cpu_coherent_after_finished_seq_id)
    return StagingMapResult(cpu_coherent_after_finished_seq_id - coherent_seq_id);
  if (write && coherent_seq_id < gpu_occupied_until_finished_seq_id) {
    if (coherent_seq_id >= cpu_coherent_after_finished_seq_id)
      return StagingMapResult::Renamable;
    return StagingMapResult(gpu_occupied_until_finished_seq_id - coherent_seq_id);
  }
  return StagingMapResult::Mappable;
}

void
StagingResource::unmap() {
  mapped = false;
}

uint64_t
StagingResource::allocate(uint64_t coherent_seq_id) {
  std::lock_guard<dxmt::mutex> lock(mutex_);
  trimCompletedAllocations(coherent_seq_id);
  uint64_t ret = idle_allocation_;
  idle_allocation_ = kInvalidAllocation;
  if (ret == kInvalidAllocation) {
    Flags<BufferAllocationFlag> flags;
#ifdef __i386__
    flags.set(BufferAllocationFlag::CpuPlaced);
#endif
    auto next = buffer_->allocate(flags);
    if (free_slots_.empty()) {
      // Trimming must not allocate after retiring a slot. Reserve before
      // publishing a new pool index so every slot can enter the freelist even
      // if the next pool growth fails; an allocation failure preserves names.
      if (free_slots_.capacity() <= buffer_pool.size()) {
        const auto capacity = free_slots_.capacity();
        const auto maximum = free_slots_.max_size();
        const auto grown = capacity > maximum / 2 ? maximum : std::max<size_t>(1, capacity * 2);
        free_slots_.reserve(std::max(buffer_pool.size() + 1, grown));
      }
      buffer_pool.push_back(std::move(next));
      ret = buffer_pool.size() - 1;
    } else {
      ret = free_slots_.back();
      free_slots_.pop_back();
      buffer_pool[ret] = std::move(next);
    }
  }
  return ret;
}

void
StagingResource::trimCompletedAllocations(uint64_t coherent_seq_id) {
  if (idle_allocation_ == kInvalidAllocation && !fifo.empty() && fifo.front().will_free_at <= coherent_seq_id) {
    idle_allocation_ = fifo.front().id;
    fifo.pop();
    assert(idle_allocation_ != immediate_name_);
  }
  while (!fifo.empty() && fifo.front().will_free_at <= coherent_seq_id) {
    auto entry = fifo.front();
    // The completion check above also prevents unsigned subtraction underflow.
    if (coherent_seq_id - entry.will_free_at < kIdleAllocationLifetime)
      break;
    fifo.pop();
    assert(entry.id != immediate_name_);
    buffer_pool[entry.id] = nullptr;
    free_slots_.push_back(entry.id);
  }
}

void
StagingResource::updateImmediateName(uint64_t current_seq_id, uint64_t allocation) {
  std::lock_guard<dxmt::mutex> lock(mutex_);
  fifo.push(QueueEntry{.id = immediate_name_, .will_free_at = current_seq_id});
  immediate_name_ = allocation;
  cpu_coherent_after_finished_seq_id = 0;
  gpu_occupied_until_finished_seq_id = 0;
}

} // namespace dxmt
