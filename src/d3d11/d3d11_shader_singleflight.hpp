// SPDX-License-Identifier: MIT
#pragma once

#include <condition_variable>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace dxmt {

// Synchronous, per-key initialization with cache-owned successful values. Hash
// and Equal must be non-throwing and preserve full key equality. A factory may
// initialize other keys, but must not recursively request its own pending key.
// A null result is shared by existing waiters and removed so later calls retry.
// With exceptions enabled, a factory exception is similarly shared/rethrown.
// Close wins over any still-pending outcome (including exceptions): all its
// callers return nullptr, and its eventual value is destroyed without publishing.
//
// Raw results remain valid while the owner or a retained Handle keeps State
// alive. Close does not release successful values. Acquire a Handle before
// destroying the owner if calls must finish across its destruction; acquiring
// handles/calling owner methods still requires ordinary C++ object lifetime
// synchronization. A factory using objects outside State must keep those alive.
template <typename Key, typename Value, typename Hash = std::hash<Key>, typename Equal = std::equal_to<Key>>
class ShaderSingleflightCache {
  struct Entry {
    std::mutex mutex;
    std::condition_variable ready;
    bool done = false;
    std::unique_ptr<Value> value;
#if defined(__cpp_exceptions)
    std::exception_ptr error;
#endif
  };

  struct State {
    std::mutex mutex;
    bool closed = false;
    std::unordered_map<Key, std::shared_ptr<Entry>, Hash, Equal> entries;
  };

  static Value *Read(const std::shared_ptr<Entry> &entry) {
    std::unique_lock lock(entry->mutex);
    entry->ready.wait(lock, [&] { return entry->done; });
#if defined(__cpp_exceptions)
    if (entry->error)
      std::rethrow_exception(entry->error);
#endif
    return entry->value.get();
  }

public:
  class Handle {
    friend class ShaderSingleflightCache;
    std::shared_ptr<State> state_;
    explicit Handle(std::shared_ptr<State> state) : state_(std::move(state)) {}

  public:
    Handle() = default;

    template <typename Factory> Value *GetOrCreate(const Key &key, Factory &&factory) const {
      // No further access to the Handle is needed once its state is retained.
      auto state = state_;
      if (!state)
        return nullptr;
      std::shared_ptr<Entry> entry;
      bool initialize = false;
      {
        std::lock_guard lock(state->mutex);
        if (state->closed)
          return nullptr;
        auto found = state->entries.find(key);
        if (found != state->entries.end()) {
          entry = found->second;
        } else {
          entry = std::make_shared<Entry>();
          state->entries.emplace(key, entry);
          initialize = true;
        }
      }
      if (!initialize)
        return Read(entry);

      std::unique_ptr<Value> value;
#if defined(__cpp_exceptions)
      std::exception_ptr error;
      try {
        value = std::forward<Factory>(factory)();
      } catch (...) {
        error = std::current_exception();
      }
#else
      value = std::forward<Factory>(factory)();
#endif
      {
        // All operations needing both locks take map -> entry. Neither a
        // factory nor an entry wait holds the map lock.
        std::lock_guard state_lock(state->mutex);
        std::lock_guard entry_lock(entry->mutex);
        if (!entry->done) {
          entry->value = std::move(value);
#if defined(__cpp_exceptions)
          entry->error = error;
#endif
          entry->done = true;
          if (!entry->value) {
            auto found = state->entries.find(key);
            // An old generation must never erase a replacement entry.
            if (found != state->entries.end() && found->second == entry)
              state->entries.erase(found);
          }
        }
      }
      entry->ready.notify_all();
      return Read(entry);
    }
  };

  ShaderSingleflightCache() : state_(std::make_shared<State>()) {}
  ShaderSingleflightCache(const ShaderSingleflightCache &) = delete;
  ShaderSingleflightCache &operator=(const ShaderSingleflightCache &) = delete;
  ShaderSingleflightCache(ShaderSingleflightCache &&) = delete;
  ShaderSingleflightCache &operator=(ShaderSingleflightCache &&) = delete;
  ~ShaderSingleflightCache() { Close(); }

  Handle GetHandle() const { return Handle(state_); }

  template <typename Factory> Value *GetOrCreate(const Key &key, Factory &&factory) const {
    return GetHandle().GetOrCreate(key, std::forward<Factory>(factory));
  }

  // Non-blocking cancellation: factories finish on their callers, retaining
  // State themselves; waiters can return without waiting for those factories.
  void Close() {
    auto state = state_;
    std::lock_guard state_lock(state->mutex);
    state->closed = true;
    for (auto &[key, entry] : state->entries) {
      std::lock_guard entry_lock(entry->mutex);
      if (!entry->done) {
        entry->done = true;
        entry->ready.notify_all();
      }
    }
  }

private:
  std::shared_ptr<State> state_;
};

} // namespace dxmt
