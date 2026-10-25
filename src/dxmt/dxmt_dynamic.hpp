#pragma once
#include "dxmt_buffer.hpp"
#include "dxmt_texture.hpp"
#include <queue>
#include "dxmt_idle_retirement.hpp"

namespace dxmt {

class DynamicBuffer {
public:
  void incRef();
  void decRef();

  Rc<BufferAllocation> allocate(uint64_t coherent_seq_id);
  void updateImmediateName(uint64_t current_seq_id, Rc<BufferAllocation> &&allocation, uint32_t suballocation, bool owned_by_command_list);
  void recycle(uint64_t current_seq_id, Rc<BufferAllocation> &&allocation);
  uint32_t nextSuballocation();

  Rc<BufferAllocation>
  immediateName() {
    return name_;
  }

  uint32_t
  immediateSuballocation() {
    return name_suballocation_;
  }

  void *
  immediateMappedMemory() {
    return name_->mappedMemory(name_suballocation_);
  }

  DynamicBuffer(Buffer *buffer, Flags<BufferAllocationFlag> flags
#if DXMT_IDLE_RETIREMENT
      , std::shared_ptr<IdleRetirementDomain> domain = {}
#endif
  );
  ~DynamicBuffer();

  struct QueueEntry {
    Rc<BufferAllocation> allocation;
    uint64_t will_free_at;
  };

  /**
   * readonly
   */
  Buffer *buffer;

private:
  Flags<BufferAllocationFlag> flags_;
  std::atomic<uint32_t> refcount_ = {0u};
#if DXMT_IDLE_RETIREMENT
  IdleRetirementQueue<QueueEntry> fifo;
  Rc<BufferAllocation> idle_allocation_;
  IdleRetirementDomain::Registration idle_registration_;
  uint64_t created_allocations_ = 1;
  void sweepIdle(uint64_t coherent, IdleRetirementDomain::Budget &, IdleRetirementDomain::Sample &);
#else
  std::queue<QueueEntry> fifo;
#endif
  dxmt::mutex mutex_;
  Rc<BufferAllocation> name_;
  uint32_t name_suballocation_ = 0;
  bool owned_by_command_list_ = false;
};

class DynamicLinearTexture {
public:
  void incRef();
  void decRef();

  Rc<TextureAllocation> allocate(uint64_t coherent_seq_id);
  void updateImmediateName(uint64_t current_seq_id, Rc<TextureAllocation> &&allocation, bool owned_by_command_list);
  void recycle(uint64_t current_seq_id, Rc<TextureAllocation> &&allocation);

  Rc<TextureAllocation>
  immediateName() {
    return name_;
  };

  void *
  mappedMemory() {
    return name_->mappedMemory;
  }

  DynamicLinearTexture(Texture *buffer, Flags<TextureAllocationFlag> flags
#if DXMT_IDLE_RETIREMENT
      , std::shared_ptr<IdleRetirementDomain> domain = {}
#endif
  );
  ~DynamicLinearTexture();

  struct QueueEntry {
    Rc<TextureAllocation> allocation;
    uint64_t will_free_at;
  };
  /**
   * readonly
   */
  Texture *texture;

private:
  Flags<TextureAllocationFlag> flags_;
  std::atomic<uint32_t> refcount_ = {0u};
#if DXMT_IDLE_RETIREMENT
  IdleRetirementQueue<QueueEntry> fifo;
  Rc<TextureAllocation> idle_allocation_;
  IdleRetirementDomain::Registration idle_registration_;
  uint64_t created_allocations_ = 1;
  void sweepIdle(uint64_t coherent, IdleRetirementDomain::Budget &, IdleRetirementDomain::Sample &);
#else
  std::queue<QueueEntry> fifo;
#endif
  dxmt::mutex mutex_;
  Rc<TextureAllocation> name_;
  bool owned_by_command_list_ = false;
};

} // namespace dxmt
