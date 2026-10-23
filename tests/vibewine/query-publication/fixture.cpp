// SPDX-License-Identifier: MIT
// CPU seams only: production query classes, API classes, flush selection and Rc.
#include <algorithm>
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "rc/util_rc_ptr.hpp"

namespace dxmt { using mutex = std::mutex; }
struct WMTBufferInfo {
  uint64_t length;
  uint64_t options;
  struct Pointer {
    void *value;
    void set(void *p) { value = p; }
    void *get() const { return value; }
  } memory;
};
constexpr uint64_t WMTResourceHazardTrackingModeUntracked = 1;
namespace WMT {
struct Buffer { std::shared_ptr<uint64_t[]> data; };
template <typename T> using Reference = T;
struct Device {
  static inline unsigned allocations = 0;
  Buffer newBuffer(WMTBufferInfo &info) {
    assert(info.length); // Native Metal buffers must not be created for empty chunks.
    allocations++;
    Buffer buffer{std::shared_ptr<uint64_t[]>(new uint64_t[info.length / 8]{})};
    info.memory.set(buffer.data.get());
    return buffer;
  }
};
}
namespace dxmt {
#include "query-under-test.hpp"
}

using BOOL = int;
using UINT = unsigned;
using UINT64 = uint64_t;
using HRESULT = int;
constexpr HRESULT S_OK = 0, S_FALSE = 1, DXGI_ERROR_INVALID_CALL = -1;
constexpr UINT D3D11_QUERY_OCCLUSION_PREDICATE = 1;
#define STDMETHODCALLTYPE
namespace dxmt {
enum class QueryState { Signaled, Building, Issued };
struct MTLD3D11OcclusionQuery {
  virtual ~MTLD3D11OcclusionQuery() = default;
  virtual UINT GetDataSize() = 0;
  virtual HRESULT GetData(void *) = 0;
  virtual VisibilityResultQuery *Begin() = 0;
  virtual VisibilityResultQuery *End() = 0;
  virtual void DoDeferredQuery(VisibilityResultQuery *) = 0;
};
struct MTLD3D11TimestampQuery {
  virtual ~MTLD3D11TimestampQuery() = default;
  virtual UINT GetDataSize() = 0;
  virtual HRESULT GetData(void *) = 0;
  virtual TimestampQuery *End() = 0;
};
template <typename T> struct MTLD3DQueryBase : T { struct { UINT Query = 0; } desc_; };
#include "query-api-under-test.hpp"

struct QueryReadbacks { std::unique_ptr<VisibilityResultReadback> visibility; };
struct FlushSeam {
  WMT::Device device_;
  VisibilityResultOffsetBumpState vro_state_;
  std::vector<Rc<VisibilityResultQuery>> pending_queries_;
  QueryReadbacks flush(uint64_t seqId) {
#include "flush-under-test.hpp"
    return readbacks;
  }
};
}
using namespace dxmt;
static std::atomic<unsigned> failures = 0;
void check(bool condition, const char *name) {
  if (!condition) { std::cerr << "FAIL " << name << '\n'; failures++; }
}

void publication_race() {
  TimestampQuery timestamp;
  VisibilityResultQuery visibility;
  visibility.begin(1, 0);
  visibility.end(1, 1);
  uint64_t data = 9;
  std::atomic<bool> go = false;
  std::atomic<uint64_t> sink = 0;
  std::mutex device_mutex;
  std::thread writer([&] {
    while (!go.load()) {}
    timestamp.issue(123);
    visibility.issue(1, &data, 1);
  });
  std::thread reader([&] {
    while (!go.load()) {}
    uint64_t out = 0, sum = 0;
    for (unsigned i = 0; i < 10000; ++i) {
      std::lock_guard lock(device_mutex);
      sum += timestamp.getValue(&out) + out;
      sum += visibility.getValue(&out) + out;
    }
    sink = sum;
  });
  go = true;
  writer.join(); reader.join();
  uint64_t value;
  check(timestamp.getValue(&value) && value == 123, "timestamp publication");
  check(visibility.getValue(&value) && value == 9, "visibility publication");
  std::cout << "publication race exercised\n";
}

void functional() {
  uint64_t value = 999, data[] = {2, 3, 5, 7};
  TimestampQuery timestamp;
  check(!timestamp.getValue(&value) && value == 999, "timestamp pending preserves output");
  timestamp.issue(0xfedcba9876543210ull);
  check(timestamp.getValue(&value) && value == 0xfedcba9876543210ull, "timestamp full width");
  value = 999;
  VisibilityResultQuery q, overlap;
  check(!q.getValue(&value) && value == 999, "unbegun pending");
  q.begin(1, 1); overlap.begin(1, 2);
  check(!q.getValue(&value), "building pending");
  q.issue(1, data, 4);
  check(!q.getValue(&value), "partial query pending");
  q.end(2, 2); overlap.end(2, 3);
  check(q.queryEndAt() == 2 && !q.getValue(&value), "ended pending");
  overlap.issue(1, data, 4);
  q.issue(2, data, 4); overlap.issue(2, data, 4);
  check(q.getValue(&value) && value == 20, "cross chunk accumulation");
  check(overlap.getValue(&value) && value == 22, "overlapping accumulation");
  q.reset();
  check(!q.getValue(&value) && q.queryEndAt() == ~0ull, "reset pending");
  q.begin(3, 1); q.end(3, 2); q.issue(3, data, 4);
  check(q.getValue(&value) && value == 3, "serial reuse");
  q.begin(4, 1); q.end(4, 1);
  check(q.getValue(&value) && value == 0, "same chunk zero range");
  {
    FlushSeam seam;
    Rc<VisibilityResultQuery> query = new VisibilityResultQuery;
    query->begin(9, 0); query->end(9, 0);
    seam.pending_queries_.push_back(query);
    auto readback = seam.flush(9);
    check(seam.pending_queries_.empty(), "same chunk empty removed");
    readback.visibility.reset();
    check(query->getValue(&value) && value == 0, "same chunk empty flushed ready");
  }
  for (bool prior_draw : {false, true}) {
    FlushSeam seam;
    Rc<VisibilityResultQuery> query = new VisibilityResultQuery;
    query->begin(10, 0);
    seam.pending_queries_.push_back(query);
    if (prior_draw) {
      seam.vro_state_.beginEncoder();
      uint64_t offset;
      seam.vro_state_.tryGetNextWriteOffset(true, offset);
      seam.vro_state_.endEncoder();
    }
    auto first = seam.flush(10);
    if (first.visibility && prior_draw)
      static_cast<uint64_t *>(first.visibility->visibility_result_heap_info.memory.get())[0] = 17;
    query->end(11, 0);
    unsigned allocations = WMT::Device::allocations;
    auto last = seam.flush(11);
    check(bool(last.visibility), prior_draw ? "empty trailing completion retained" : "all empty completion retained");
    check(WMT::Device::allocations == allocations, "empty chunk no native allocation");
    check(seam.pending_queries_.empty(), "ended query removed");
    check(!query->getValue(&value), "empty end waits for finish");
    first.visibility.reset();
    check(!query->getValue(&value), "earlier readback cannot finish later end");
    last.visibility.reset();
    check(query->getValue(&value) && value == (prior_draw ? 17 : 0),
          prior_draw ? "empty trailing completion ready" : "all empty completion ready");
  }
}

void generations() {
  // Match native aggregate initialization; poison placement storage so an
  // omitted production `= {}` cannot be hidden by allocator zero pages.
  {
    alignas(VisibilityResultReadback) unsigned char storage[sizeof(VisibilityResultReadback)];
    std::fill_n(storage, sizeof(storage), 0xa5);
    std::vector<Rc<VisibilityResultQuery>> queries;
    auto readback = new (storage) VisibilityResultReadback(WMT::Device{}, 8, 0, queries);
    check(readback->visibility_result_heap_info.memory.get() == nullptr
              && readback->visibility_result_heap_info.length == 0,
          "empty native info initialized");
    readback->~VisibilityResultReadback();
  }
  OcclusionQuery api_impl;
  MTLD3D11OcclusionQuery &api = api_impl;
  Rc<VisibilityResultQuery> old = api.Begin(); old->begin(1, 0);
  api.End(); old->end(1, 1);
  Rc<VisibilityResultQuery> current = api.Begin(); current->begin(2, 0);
  check(old.ptr() != current.ptr(), "reuse creates separate generation");
  uint64_t data = 4, value = 99;
  old->issue(1, &data, 1);
  check(api.GetData(&value) == DXGI_ERROR_INVALID_CALL, "building API invalid call");
  api.End(); current->end(2, 1);
  check(api.GetData(&value) == S_FALSE, "old completion cannot publish new generation");
  data = 7; current->issue(2, &data, 1);
  check(api.GetData(&value) == S_OK && value == 7, "new generation value");
  check(api.GetData(&value) == S_OK && value == 7, "repeated API ready value");
  MTLD3D11TimestampQueryImpl ts_impl;
  MTLD3D11TimestampQuery &ts = ts_impl;
  Rc<TimestampQuery> old_ts = ts.End(), current_ts = ts.End();
  old_ts->issue(42);
  check(ts.GetData(&value) == S_FALSE, "old timestamp cannot publish new generation");
  current_ts->issue(84);
  check(ts.GetData(&value) == S_OK && value == 84, "timestamp generation value");
  // Pending readback owns the last Rc even after API/local ownership retires.
  for (unsigned i = 0; i < 128; ++i) {
    Rc<VisibilityResultQuery> query = new VisibilityResultQuery;
    query->begin(3, 0); query->end(3, 1);
    std::vector<Rc<VisibilityResultQuery>> queries{query};
    auto readback = std::make_unique<VisibilityResultReadback>(WMT::Device{}, 3, 1, queries);
    check(readback->queries.size() == 1 && readback->queries[0] == query, "readback owns last query");
    static_cast<uint64_t *>(readback->visibility_result_heap_info.memory.get())[0] = 1;
    queries.clear(); query = nullptr;
    std::thread finish([owned = std::move(readback)]() mutable { owned.reset(); });
    finish.join();
  }
}

void concurrent_encoder_finish_poll() {
  VisibilityResultQuery query;
  std::atomic<bool> begin_published = false, issue_published = false, finished = false;
  std::mutex device_mutex;
  std::thread encoder([&] {
    query.begin(1, 0); begin_published = true;
    while (!issue_published.load()) {}
    query.end(2, 1);
  });
  std::thread finish([&] {
    while (!begin_published.load()) {}
    uint64_t data = 11;
    query.issue(1, &data, 1); issue_published = true;
    while (query.queryEndAt() != 2) {}
    query.issue(2, &data, 1); finished = true;
  });
  std::vector<std::thread> readers;
  for (unsigned i = 0; i < 4; ++i) readers.emplace_back([&] {
    uint64_t value = 0;
    while (!finished.load()) {
      std::lock_guard lock(device_mutex);
      if (query.getValue(&value)) check(value == 22, "concurrent poll coherent value");
    }
  });
  encoder.join(); finish.join();
  for (auto &reader : readers) reader.join();
  uint64_t value;
  check(query.getValue(&value) && value == 22, "encoder finish poll completed");
}

int main(int argc, char **argv) {
  bool race_only = argc == 2 && std::string(argv[1]) == "race";
  bool empty_only = argc == 2 && std::string(argv[1]) == "empty";
  if (!empty_only) publication_race();
  if (!race_only) functional();
  if (!race_only && !empty_only) { generations(); concurrent_encoder_finish_poll(); }
  if (failures) return 1;
  std::cout << "PASS query publication" << (race_only ? " race" : " scenarios") << '\n';
}
