#pragma once

#include "Metal.hpp"
#include "log/log.hpp"
#include "thread.hpp"
#include "util_math.hpp"
#include <mutex>
#include <list>
#include <array>
#include <limits>
#include <new>
#include <span>

namespace dxmt {

constexpr size_t kStagingBlockSize = 0x2000000; // 32MB
constexpr size_t kStagingBlockSizeForDeferredContext = 0x200000; // 2MB
constexpr size_t kStagingBlockLifetime = 300;

template <typename Allocator, size_t BlockSize = kStagingBlockSize, class mutex = dxmt::mutex> class RingBumpState {

public:

  static constexpr size_t block_size = BlockSize;

  RingBumpState(Allocator &&allocator) : allocator_(std::move(allocator)) {}

  std::pair<typename Allocator::Block &, uint64_t>
  allocate(uint64_t seq_id, uint64_t coherent_id, size_t size, size_t alignment);

  struct Reservation {
    typename Allocator::Block *block = nullptr;
    uint64_t offset = 0;
  };

  // The initializer reserves at most two planes. Failure preserves both the
  // ring and the caller's outputs, including when a later plane cannot fit.
  bool allocateChecked(uint64_t seq_id, uint64_t coherent_id, std::span<const size_t> sizes,
                       size_t alignment, std::span<Reservation> out);

  void free_blocks(uint64_t coherent_id);

private:
  struct Allocation {
    size_t allocated_size;
    size_t total_size;
    uint64_t last_used_seq_id;
    uint64_t inc_time_to_live;
    Allocator::Block block;
  };

  Allocation &allocate_or_reuse_block(uint64_t seq_id, uint64_t coherent_id, size_t block_size);

  std::pair<typename Allocator::Block &, uint64_t>
  suballocate(Allocation &allocation, size_t size, size_t alignment) {
    auto offset = align(allocation.allocated_size, alignment);
    allocation.allocated_size = offset + size;
    return {allocation.block, offset};
  };

  // Aging and erasing completed entries must preserve references to surviving
  // blocks, including a current tail being populated by the producer.
  std::list<Allocation> fifo;
  mutex mutex_;
  Allocator allocator_;
};

class GpuPrivateBufferBlockAllocator {
public:
  GpuPrivateBufferBlockAllocator(WMT::Device device, WMTResourceOptions block_options) {
    device_ = device;
    buffer_info_.memory.set(nullptr);
    buffer_info_.options = block_options;
  }

  class Block {
  public:
    WMT::Reference<WMT::Buffer> buffer;
    uint64_t gpu_address = 0;

    Block() = default;
    Block(const Block &copy) = delete;
    Block(Block &&move) = default;
  };

  Block
  allocate(size_t block_size) {
    Block block{};
    buffer_info_.length = block_size;
    block.buffer = device_.newBuffer(buffer_info_);
    block.gpu_address = buffer_info_.gpu_address;
    return block;
  };

  bool valid(const Block &block) const { return bool(block.buffer); }

private:
  WMT::Device device_;
  WMTBufferInfo buffer_info_{};
};

class StagingBufferBlockAllocator {
public:
  StagingBufferBlockAllocator(WMT::Device device, WMTResourceOptions block_options, bool placed_buffer = true) {
    device_ = device;
    buffer_info_ = block_options;
    placed_buffer_ = placed_buffer;
  }

  class Block {
  public:
    WMT::Reference<WMT::Buffer> buffer;
    uint64_t gpu_address = 0;
    void *mapped_address = nullptr;

    ~Block() {
      if (mapped_address) {
        free(mapped_address);
        mapped_address = nullptr;
      }
    };

    Block() = default;

    Block(const Block &) = delete;
    Block(Block &&move) {
      buffer = std::move(move.buffer);
      gpu_address = move.gpu_address;
      mapped_address = move.mapped_address;
      move.mapped_address = nullptr;
    };
  };

  Block
  allocate(size_t block_size) {
    Block block{};
    block.mapped_address = placed_buffer_ ? malloc(block_size) : nullptr;
    WMTBufferInfo info{};
    info.options = buffer_info_;
    info.memory.set(block.mapped_address);
    info.length = block_size;
    block.buffer = device_.newBuffer(info);
    block.gpu_address = info.gpu_address;
    return block;
  };

  bool valid(const Block &block) const {
    return block.buffer && (!placed_buffer_ || block.mapped_address);
  }

private:
  WMT::Device device_;
  WMTResourceOptions buffer_info_;
  bool placed_buffer_;
};

class HostBufferBlockAllocator {
public:
  class Block {
  public:
    void *ptr = nullptr;

    Block() = default;

    Block(const Block &) = delete;
    Block(Block &&move) {
      ptr = move.ptr;
      move.ptr = nullptr;
    };

    ~Block() {
      if (ptr) {
        free(ptr);
        ptr = nullptr;
      }
    };
  };

  Block
  allocate(size_t block_size) {
    Block block{};
    block.ptr = malloc(block_size);
    return block;
  };

  bool valid(const Block &block) const { return block.ptr != nullptr; }
};

template <typename Allocator, size_t BlockSize, class mutex>
bool
RingBumpState<Allocator, BlockSize, mutex>::allocateChecked(
    uint64_t seq_id, uint64_t coherent_id, std::span<const size_t> sizes,
    size_t alignment, std::span<Reservation> out
) {
  if (sizes.empty() || sizes.size() > 2 || out.size() != sizes.size() ||
      alignment == 0 || (alignment & (alignment - 1)) != 0)
    return false;
  for (auto size : sizes) {
    // Native lengths and offsets are uint64_t. Also reject an unrepresentable
    // aligned extent before touching the allocator or ring metadata.
    if (size == 0 || size > std::numeric_limits<uint64_t>::max() ||
        size > std::numeric_limits<size_t>::max() - (alignment - 1))
      return false;
  }

  std::lock_guard<mutex> lock(mutex_);
  using Iterator = typename std::list<Allocation>::iterator;
  struct Plan {
    Iterator entry;
    size_t used;
    bool candidate;
  };
  std::array<Plan, 2> plans{};
  std::array<Reservation, 2> reservations{};
  size_t plan_count = 0;
  std::list<Allocation> pending;

  auto reserve = [&](size_t used, size_t total, size_t size, size_t &offset) {
    if (used > std::numeric_limits<size_t>::max() - (alignment - 1))
      return false;
    offset = (used + alignment - 1) & ~(alignment - 1);
    return offset <= total && size <= total - offset;
  };

  try {
    for (size_t i = 0; i < sizes.size(); ++i) {
      const auto size = sizes[i];
      size_t offset = 0;
      if (plan_count && reserve(plans[plan_count - 1].used,
                                plans[plan_count - 1].entry->total_size, size, offset)) {
        auto &plan = plans[plan_count - 1];
        plan.used = offset + size;
        reservations[i] = {&plan.entry->block, offset};
        continue;
      }

      Iterator selected = fifo.end();
      size_t used = 0;
      if (!plan_count && !fifo.empty()) {
        auto tail = std::prev(fifo.end());
        if (reserve(tail->allocated_size, tail->total_size, size, offset)) {
          selected = tail;
          used = offset + size;
        }
      }
      if (selected == fifo.end()) {
        for (auto it = fifo.begin(); it != fifo.end(); ++it) {
          if (it->last_used_seq_id >= coherent_id)
            break;
          bool already_selected = false;
          for (size_t j = 0; j < plan_count; ++j)
            already_selected |= !plans[j].candidate && plans[j].entry == it;
          if (!already_selected && it->total_size == BlockSize &&
              reserve(0, it->total_size, size, offset)) {
            selected = it;
            used = offset + size;
            break;
          }
        }
      }
      if (selected != fifo.end()) {
        if (!allocator_.valid(selected->block))
          return false;
        plans[plan_count++] = {selected, used, false};
        reservations[i] = {&selected->block, offset};
        continue;
      }

      const auto total = std::max(size, BlockSize);
      if (total > std::numeric_limits<uint64_t>::max())
        return false;
      auto block = allocator_.allocate(total);
      if (!allocator_.valid(block))
        return false;
      // Both native allocation and list-node allocation can fail. Pending
      // owns every prepared block until a completely nonallocating commit.
      pending.push_back({0, total, seq_id, 0, std::move(block)});
      auto candidate = std::prev(pending.end());
      plans[plan_count++] = {candidate, size, true};
      reservations[i] = {&candidate->block, 0};
    }
  } catch (const std::bad_alloc &) {
    return false;
  }

  for (size_t i = 0; i < plan_count; ++i) {
    auto &plan = plans[i];
    plan.entry->allocated_size = plan.used;
    plan.entry->last_used_seq_id = seq_id;
    plan.entry->inc_time_to_live = 0;
    if (plan.candidate)
      fifo.splice(fifo.end(), pending, plan.entry);
    else
      fifo.splice(fifo.end(), fifo, plan.entry);
  }
  for (size_t i = 0; i < sizes.size(); ++i)
    out[i] = reservations[i];
  return true;
}

template <typename Allocator, size_t BlockSize, class mutex>
std::pair<typename Allocator::Block &, uint64_t>
RingBumpState<Allocator, BlockSize, mutex>::allocate(
    uint64_t seq_id, uint64_t coherent_id, size_t size, size_t alignment
) {
  std::lock_guard<mutex> lock(mutex_);
  while (!fifo.empty()) {
    auto &latest = fifo.back();
    if ((align(latest.allocated_size, alignment) + size) > latest.total_size) {
      break;
    }
    latest.last_used_seq_id = seq_id;
    latest.inc_time_to_live = 0;
    return suballocate(latest, size, alignment);
  }
  return suballocate(
      allocate_or_reuse_block(
          seq_id, coherent_id, std::max(size, BlockSize) // in case required size is larger than block size
      ),
      size, alignment
  );
};

template <typename Allocator, size_t BlockSize, class mutex>
void
RingBumpState<Allocator, BlockSize, mutex>::free_blocks(uint64_t coherent_id) {
  std::lock_guard<mutex> lock(mutex_);
  if (coherent_id == -1ull) {
    fifo.clear();
    return;
  }
  auto warm = fifo.end();
  for (auto it = fifo.begin(); it != fifo.end(); ++it) {
    if (it->last_used_seq_id > coherent_id)
      break;
    if (it->total_size == BlockSize)
      warm = it;
  }
  for (auto it = fifo.begin(); it != fifo.end();) {
    if (it->last_used_seq_id > coherent_id)
      break;
    auto expired = (coherent_id - it->last_used_seq_id) > kStagingBlockLifetime ||
                   it->inc_time_to_live > kStagingBlockLifetime;
    auto adhoc = it->total_size != BlockSize;
    if (adhoc || (expired && it != warm)) {
      // Completion protects GPU/CPU users. Keep one warm standard block, but
      // never let that reserve protect the rest of a completed high-water burst.
      it = fifo.erase(it);
      continue;
    }
    // An initializer's coherent sequence can stop after an upload burst. Age
    // every completed block together and saturate instead of wrapping around.
    // Check expiration first to preserve the existing >300-call grace boundary.
    if (it->inc_time_to_live <= kStagingBlockLifetime)
      it->inc_time_to_live++;
    ++it;
  }
};

template <typename Allocator, size_t BlockSize, class mutex>
RingBumpState<Allocator, BlockSize, mutex>::Allocation &
RingBumpState<Allocator, BlockSize, mutex>::allocate_or_reuse_block(
    uint64_t seq_id, uint64_t coherent_id, size_t block_size
) {
  while (!fifo.empty()) {
    auto &front = fifo.front();
    if (front.last_used_seq_id < coherent_id) {
      if (front.total_size != BlockSize) {
        fifo.pop_front();
        continue;
      } else if (front.total_size >= block_size) {
        front.last_used_seq_id = seq_id;
        front.allocated_size = 0;
        front.inc_time_to_live = 0;
        // Recycling a completed block needs no new list node. Keep the Block
        // identity stable and avoid an allocator failure after metadata changed.
        fifo.splice(fifo.end(), fifo, fifo.begin());
        return fifo.back();
      }
      WARN("forced to allocate new block of size ", block_size);
    }
    break;
  }
  fifo.push_back({
      .allocated_size = 0,
      .total_size = block_size,
      .last_used_seq_id = seq_id,
      .inc_time_to_live = 0,
      .block = allocator_.allocate(block_size),
  });
  return fifo.back();
};

} // namespace dxmt
