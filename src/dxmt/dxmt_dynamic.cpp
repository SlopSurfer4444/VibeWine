#include "dxmt_dynamic.hpp"
#include "dxmt_texture.hpp"

namespace dxmt {
namespace {

template <typename Queue>
void
trimCompletedDynamicAllocations(Queue &fifo, uint64_t coherent_seq_id) {
  // The caller has reserved one reusable allocation. Retain recent bursts for
  // 300 completed sequences, then release excess pool references lazily. The
  // current name and allocations still owned by command lists are not in fifo.
  constexpr uint64_t kIdleAllocationLifetime = 300;
  while (!fifo.empty()) {
    const auto retirement = fifo.front().will_free_at;
    if (retirement > coherent_seq_id || coherent_seq_id - retirement < kIdleAllocationLifetime)
      break;
    fifo.pop();
  }
}

#if DXMT_IDLE_RETIREMENT
template<class A> uint64_t allocationBytes(const Rc<A> &allocation) {
  if (!allocation) return 0;
  if constexpr (requires { allocation->buffer(); })
    return allocation->allocatedSizeForIdleRetirement();
  else
    return allocation->texture().allocatedSize();
}
template<class Queue, class A>
void sweepDynamicPool(Queue &fifo, Rc<A> &spare, uint64_t coherent,
                      IdleRetirementDomain::Budget &budget, IdleRetirementDomain::Sample &sample) {
  if (!spare && !fifo.empty() && fifo.front().will_free_at <= coherent && budget.entries) {
    --budget.entries;
    spare = std::move(fifo.front().allocation); fifo.pop();
  }
  // Never scan past an incomplete or young FIFO head. Preserve one warm spare.
  while (!fifo.empty() && budget.entries) {
    --budget.entries;
    auto retirement = fifo.front().will_free_at;
    if (retirement > coherent || coherent - retirement < IdleRetirementDomain::kLifetime) break;
    const auto bytes = allocationBytes(fifo.front().allocation);
    if (!budget.release(bytes)) break;
    fifo.pop(); ++sample.released_count; sample.released_bytes += bytes;
  }
  if (spare) sample.completed_idle.add(1, allocationBytes(spare));
  uint64_t inspected = 0;
  for (const auto &entry : fifo.entries()) {
    if (!budget.entries) break;
    --budget.entries; ++inspected;
    auto &category = entry.will_free_at <= coherent ? sample.completed_idle : sample.inflight;
    category.add(1, allocationBytes(entry.allocation));
  }
  // Exact byte sizes for an unvisited tail are intentionally unknown.
  sample.unclassified.add(fifo.size() - inspected, 0);
}
#endif

} // namespace

DynamicBuffer::DynamicBuffer(Buffer *buffer, Flags<BufferAllocationFlag> flags
#if DXMT_IDLE_RETIREMENT
    , std::shared_ptr<IdleRetirementDomain> domain
#endif
) :
    buffer(buffer),
    flags_(flags),
    name_(buffer->current()) {
#if DXMT_IDLE_RETIREMENT
  idle_registration_.attach(std::move(domain), this, [](void *owner, uint64_t coherent,
      IdleRetirementDomain::Budget &budget, IdleRetirementDomain::Sample &sample) {
    static_cast<DynamicBuffer *>(owner)->sweepIdle(coherent, budget, sample);
  });
#endif
}

DynamicBuffer::~DynamicBuffer() {
#if DXMT_IDLE_RETIREMENT
  // Must precede destruction of FIFO/mutex/name, even during a concurrent sweep.
  idle_registration_.detach();
#endif
}
#if DXMT_IDLE_RETIREMENT
void DynamicBuffer::sweepIdle(uint64_t coherent, IdleRetirementDomain::Budget &budget,
                       IdleRetirementDomain::Sample &sample) {
  std::unique_lock<dxmt::mutex> lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock()) { ++sample.skipped_busy; return; }
  sample.current.add(1, allocationBytes(name_));
  sample.created_count += created_allocations_;
  sweepDynamicPool(fifo, idle_allocation_, coherent, budget, sample);
}
#endif

void
DynamicBuffer::incRef() {
  refcount_.fetch_add(1u, std::memory_order_acquire);
};

void
DynamicBuffer::decRef() {
  if (refcount_.fetch_sub(1u, std::memory_order_release) == 1u)
    delete this;
};

Rc<BufferAllocation>
DynamicBuffer::allocate(uint64_t coherent_seq_id) {
  std::lock_guard<dxmt::mutex> lock(mutex_);
  Rc<BufferAllocation> ret;
#if DXMT_IDLE_RETIREMENT
  if (idle_allocation_) return std::move(idle_allocation_);
#endif
  for (;;) {
    if (fifo.empty()) {
      break;
    }
    auto &entry = fifo.front();
    if (entry.will_free_at > coherent_seq_id) {
      break;
    }
    ret = std::move(entry.allocation);
    fifo.pop();
    trimCompletedDynamicAllocations(fifo, coherent_seq_id);
    break;
  }
  if (!ret.ptr()) {
    ret = buffer->allocate(flags_);
#if DXMT_IDLE_RETIREMENT
    ++created_allocations_;
#endif
  }
  return ret;
}

void
DynamicBuffer::updateImmediateName(uint64_t current_seq_id, Rc<BufferAllocation> &&allocation, uint32_t suballocation, bool owned_by_command_list) {
  std::lock_guard<dxmt::mutex> lock(mutex_);
  if (!owned_by_command_list_)
    fifo.push(QueueEntry{.allocation = std::move(name_), .will_free_at = current_seq_id});
  name_ = std::move(allocation);
  name_suballocation_ = suballocation;
  owned_by_command_list_ = owned_by_command_list;
}

void
DynamicBuffer::recycle(uint64_t current_seq_id, Rc<BufferAllocation> &&allocation) {
  std::lock_guard<dxmt::mutex> lock(mutex_);
  if (owned_by_command_list_) {
    if (name_.ptr() == allocation.ptr()) {
      owned_by_command_list_ = false;
      auto _ = std::move(allocation);
      return;
    }
  }
  fifo.push(QueueEntry{.allocation = std::move(allocation), .will_free_at = current_seq_id});
}

uint32_t
DynamicBuffer::nextSuballocation() {
  if (name_->hasSuballocatoin(name_suballocation_ + 1)) {
    return ++name_suballocation_;
  }
  return 0;
}

DynamicLinearTexture::DynamicLinearTexture(Texture *texture, Flags<TextureAllocationFlag> flags
#if DXMT_IDLE_RETIREMENT
    , std::shared_ptr<IdleRetirementDomain> domain
#endif
) :
    texture(texture),
    flags_(flags),
    name_(texture->current()) {
#if DXMT_IDLE_RETIREMENT
  idle_registration_.attach(std::move(domain), this, [](void *owner, uint64_t coherent,
      IdleRetirementDomain::Budget &budget, IdleRetirementDomain::Sample &sample) {
    static_cast<DynamicLinearTexture *>(owner)->sweepIdle(coherent, budget, sample);
  });
#endif
}

DynamicLinearTexture::~DynamicLinearTexture() {
#if DXMT_IDLE_RETIREMENT
  // Must precede destruction of FIFO/mutex/name, even during a concurrent sweep.
  idle_registration_.detach();
#endif
}
#if DXMT_IDLE_RETIREMENT
void DynamicLinearTexture::sweepIdle(uint64_t coherent, IdleRetirementDomain::Budget &budget,
                       IdleRetirementDomain::Sample &sample) {
  std::unique_lock<dxmt::mutex> lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock()) { ++sample.skipped_busy; return; }
  sample.current.add(1, allocationBytes(name_));
  sample.created_count += created_allocations_;
  sweepDynamicPool(fifo, idle_allocation_, coherent, budget, sample);
}
#endif

void
DynamicLinearTexture::incRef() {
  refcount_.fetch_add(1u, std::memory_order_acquire);
};

void
DynamicLinearTexture::decRef() {
  if (refcount_.fetch_sub(1u, std::memory_order_release) == 1u)
    delete this;
};

Rc<TextureAllocation>
DynamicLinearTexture::allocate(uint64_t coherent_seq_id) {
  std::lock_guard<dxmt::mutex> lock(mutex_);
  Rc<TextureAllocation> ret;
#if DXMT_IDLE_RETIREMENT
  if (idle_allocation_) return std::move(idle_allocation_);
#endif
  for (;;) {
    if (fifo.empty()) {
      break;
    }
    auto &entry = fifo.front();
    if (entry.will_free_at > coherent_seq_id) {
      break;
    }
    ret = std::move(entry.allocation);
    fifo.pop();
    trimCompletedDynamicAllocations(fifo, coherent_seq_id);
    break;
  }
  if (!ret.ptr()) {
    ret = texture->allocate(flags_);
#if DXMT_IDLE_RETIREMENT
    ++created_allocations_;
#endif
  }
  return ret;
}

void
DynamicLinearTexture::updateImmediateName(uint64_t current_seq_id, Rc<TextureAllocation> &&allocation, bool owned_by_command_list) {
  std::lock_guard<dxmt::mutex> lock(mutex_);
  if (!owned_by_command_list_)
    fifo.push(QueueEntry{.allocation = std::move(name_), .will_free_at = current_seq_id});
  name_ = std::move(allocation);
  owned_by_command_list_ = owned_by_command_list;
}

void
DynamicLinearTexture::recycle(uint64_t current_seq_id, Rc<TextureAllocation> &&allocation) {
  std::lock_guard<dxmt::mutex> lock(mutex_);
  if (owned_by_command_list_) {
    if (name_.ptr() == allocation.ptr()) {
      owned_by_command_list_ = false;
      auto _ = std::move(allocation);
      return;
    }
  }
  fifo.push(QueueEntry{.allocation = std::move(allocation), .will_free_at = current_seq_id});
}

} // namespace dxmt
