#pragma once
#include "Metal.hpp"
#include "thread.hpp"
#include "compiler_cache_compat.hpp"
#include "dxmt_persistence.hpp"
#include <memory>
#include <utility>

namespace dxmt {

constexpr uint64_t kDXMTShaderCacheVersion = kDXMTCompilerCacheCompatibility;

class ShaderCache {
public:
  template <typename T> class LockProtected {
  public:
    [[nodiscard]] LockProtected(dxmt::mutex &mutex, T object) : object(object), mutex_(&mutex) {
      mutex_->lock();
    }
    [[nodiscard]] LockProtected() : object({}), mutex_(nullptr) {}
    ~LockProtected() {
      if (mutex_) {
        mutex_->unlock();
        mutex_ = nullptr;
      }
      object = {};
    }
    T object;
    LockProtected(const LockProtected &copy) = delete;
    LockProtected(LockProtected &&move)
        : object(std::move(move.object)), mutex_(std::exchange(move.mutex_, nullptr)) {
      move.object = {};
    }

    operator bool() const {
      return mutex_ != nullptr;
    }

    T *
    operator->() {
      return &object;
    }

  private:
    dxmt::mutex *mutex_;
  };

  static ShaderCache &getInstance(WMTMetalVersion version);

  LockProtected<WMT::CacheWriter>
  getWriter() {
    if (!writer_state_->writer)
      return {};
    return {writer_state_->mutex, writer_state_->writer};
  }

  // Copy the unchanged cache key and retain both writer state and data. The
  // resulting write remains valid after its shader/task/cache wrapper dies.
  template <typename Key>
  post_completion_work makeWrite(Key key, WMT::DispatchData data, uint64_t bytes) {
    if (!writer_state_->writer || !data)
      return {};
    WMT::Reference<WMT::DispatchData> held_data(data);
    return {[state = writer_state_, key = std::move(key), data = std::move(held_data)]() mutable {
              auto pool = WMT::MakeAutoreleasePool();
              const auto trace = active_persistence_trace;
              const auto lock_start = trace.now();
              std::lock_guard<dxmt::mutex> lock(state->mutex);
              trace.emit(87, 0, trace.owner, 0, lock_start, trace.now());
              const auto write_start = trace.now();
              try {
                const auto status = state->writer.set_checked(key, data);
                trace.emit(88, status, trace.owner, 0, write_start, trace.now());
              } catch (...) {
                trace.emit(88, 0xfffffffeu, trace.owner, 0, write_start, trace.now());
                throw;
              }
            }, bytes, 2};
  }

  LockProtected<WMT::CacheReader>
  getReader() {
    if (!scache_reader_)
      return {};
    return {scache_reader_mutex_, scache_reader_};
  }

  template <typename Key>
  WMT::Reference<WMT::DispatchData> find(const Key &key) { return read(&key, sizeof(key)); }

  ShaderCache(WMTMetalVersion metal_version);
  ShaderCache(const ShaderCache &copy) = delete;

private:
  WMT::Reference<WMT::DispatchData> read(const void *key, uint64_t length);
  uint64_t trace_serial_ = 0;
  struct WriterState {
    WMT::Reference<WMT::CacheWriter> writer;
    dxmt::mutex mutex;
  };
  std::shared_ptr<WriterState> writer_state_ = std::make_shared<WriterState>();

  WMT::Reference<WMT::CacheReader> scache_reader_;
  dxmt::mutex scache_reader_mutex_;
};

} // namespace dxmt
