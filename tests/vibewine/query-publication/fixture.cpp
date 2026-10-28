// SPDX-License-Identifier: MIT
// CPU seams only: production query classes, API classes, flush selection and Rc.
#include <algorithm>
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <iostream>
#include <cstring>
#include <array>
#include <string>
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
template <typename T> struct WrapperCounts {
  static inline std::atomic<unsigned> allocations = 0, deletions = 0;
};
#include "query-under-test.hpp"
}

using BOOL = int;
using UINT = unsigned;
using UINT64 = uint64_t;
using HRESULT = int;
constexpr HRESULT S_OK = 0, S_FALSE = 1, DXGI_ERROR_INVALID_CALL = -1, E_INVALIDARG = -2, E_FAIL = -3;
constexpr UINT D3D11_QUERY_EVENT = 0, D3D11_QUERY_OCCLUSION = 1,
  D3D11_QUERY_OCCLUSION_PREDICATE = 2, D3D11_QUERY_TIMESTAMP = 3,
  D3D11_QUERY_TIMESTAMP_DISJOINT = 4, D3D11_QUERY_PIPELINE_STATISTICS = 5;
constexpr UINT D3D11_ASYNC_GETDATA_DONOTFLUSH = 1;
constexpr BOOL FALSE = 0;
struct D3D11_QUERY_DESC { UINT Query = 0, MiscFlags = 0; };
using D3D11_QUERY_DESC1 = D3D11_QUERY_DESC;
struct D3D11_QUERY_DATA_TIMESTAMP_DISJOINT { uint64_t Frequency; BOOL Disjoint; };
struct D3D11_QUERY_DATA_PIPELINE_STATISTICS { uint64_t counters[11]; };
#define STDMETHODCALLTYPE
#define ERR(...) ((void)0)
namespace cs2trace {
struct ApiScope {
  template <typename... T> ApiScope(T...) {}
  template <typename F> HRESULT invoke(F f) { return f(); }
};
enum class CommitReason { GetDataNotReady };
struct ReasonScope { template <typename... T> ReasonScope(T...) {} };
struct SyncScope { uint64_t gen; int type; uint64_t object; uint32_t flags; uint64_t queue, frame; ~SyncScope() {} };
uint64_t operationGeneration(uint64_t n) { return n; }
}
namespace dxmt {
using d3d11_device_mutex = std::recursive_mutex;
enum class QueryState { Signaled, Building, Issued, Undefined };
enum class EventState { Pending, Signaled, Stall, Invalid };
constexpr size_t kEventStallThreshold = 64;
struct MTLD3D11Device {};
struct ID3D11Asynchronous {
  virtual ~ID3D11Asynchronous() = default;
  virtual UINT GetDataSize() = 0;
};
struct ID3D11Query : ID3D11Asynchronous { virtual void GetDesc(D3D11_QUERY_DESC *) = 0; };
struct MTLD3D11EventQuery : ID3D11Query {
  virtual void Issue(uint64_t) = 0;
  virtual EventState CheckEventState(uint64_t) = 0;
};
struct MTLD3D11OcclusionQuery : ID3D11Query {
  virtual HRESULT GetData(void *) = 0;
  virtual VisibilityResultQuery *Begin() = 0;
  virtual VisibilityResultQuery *End() = 0;
  virtual void DoDeferredQuery(VisibilityResultQuery *) = 0;
};
struct MTLD3D11TimestampQuery : ID3D11Query {
  virtual HRESULT GetData(void *) = 0;
  virtual TimestampQuery *End() = 0;
};
template <typename T> struct MTLD3DQueryBase : T {
  D3D11_QUERY_DESC desc_;
  MTLD3DQueryBase() = default;
  MTLD3DQueryBase(MTLD3D11Device *, const D3D11_QUERY_DESC1 *desc) : desc_(*desc) {}
  void GetDesc(D3D11_QUERY_DESC *desc) override { *desc = desc_; }
};
#include "event-under-test.hpp"
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
struct ImmediateInterface { virtual HRESULT GetData(ID3D11Asynchronous *, void *, UINT, UINT) = 0; };
struct ImmediateSeam : ImmediateInterface {
  struct Queue {
    uint64_t signaled = 0;
    struct { unsigned event_stall = 0; } stats;
    uint64_t traceSerial() { return 1; }
    uint64_t SignaledEventSeqId() { return signaled; }
    uint64_t CurrentFrameSeq() { return 1; }
    template <typename F> void UpdateCurrentFrameStatistics(F f) { f(stats); }
  } cmd_queue;
  d3d11_device_mutex mutex;
  unsigned flushes = 0;
  void Flush() { std::lock_guard lock(mutex); flushes++; }
#include "immediate-under-test.hpp"
};
struct StatisticsQuery : ID3D11Query {
  UINT GetDataSize() override { return sizeof(D3D11_QUERY_DATA_PIPELINE_STATISTICS); }
  void GetDesc(D3D11_QUERY_DESC *d) override { d->Query = D3D11_QUERY_PIPELINE_STATISTICS; }
};
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

void frontier() {
  ImmediateSeam imm;
  D3D11_QUERY_DESC1 event_desc{D3D11_QUERY_EVENT}, disjoint_desc{D3D11_QUERY_TIMESTAMP_DISJOINT};
  MTLD3D11EventQueryImpl<BOOL> event(nullptr, &event_desc);
  MTLD3D11EventQueryImpl<D3D11_QUERY_DATA_TIMESTAMP_DISJOINT> disjoint(nullptr, &disjoint_desc);
  OcclusionQuery occlusion, predicate;
  occlusion.desc_.Query = D3D11_QUERY_OCCLUSION;
  predicate.desc_.Query = D3D11_QUERY_OCCLUSION_PREDICATE;
  MTLD3D11TimestampQueryImpl timestamp;
  timestamp.desc_.Query = D3D11_QUERY_TIMESTAMP;
  StatisticsQuery statistics;
  std::array<ID3D11Asynchronous *, 6> queries{&event, &occlusion, &predicate, &timestamp, &disjoint, &statistics};
  alignas(uint64_t) std::array<unsigned char, 128> destination, original;
  destination.fill(0xa5); original = destination;
  event.Issue(1); disjoint.Issue(1);
  Rc<VisibilityResultQuery> oq = static_cast<MTLD3D11OcclusionQuery &>(occlusion).Begin();
  oq->begin(1, 0);
  check(imm.GetData(&occlusion, destination.data() + 8, 0, 0) == DXGI_ERROR_INVALID_CALL && destination == original,
        "building status-only no write");
  static_cast<MTLD3D11OcclusionQuery &>(occlusion).End(); oq->end(1, 1);
  Rc<VisibilityResultQuery> pq = static_cast<MTLD3D11OcclusionQuery &>(predicate).Begin();
  pq->begin(1, 0); static_cast<MTLD3D11OcclusionQuery &>(predicate).End(); pq->end(1, 1);
  Rc<TimestampQuery> tq = static_cast<MTLD3D11TimestampQuery &>(timestamp).End();
  for (unsigned i = 0; i < queries.size(); ++i) {
    auto q = queries[i];
    auto label = std::to_string(i);
    auto verify = [&](bool c, std::string n) { check(c, (n + " " + label).c_str()); };
    unsigned before = imm.flushes;
    verify(imm.GetData(q, destination.data() + 8, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) == (i == 5 ? S_OK : S_FALSE), "pending status");
    verify(destination == original, "zero-size pending destination unchanged");
    destination = original;
    verify(imm.flushes == before, "DONOTFLUSH preserved");
    verify(imm.GetData(q, nullptr, q->GetDataSize(), 0) == E_INVALIDARG, "nonzero null invalid");
    verify(imm.GetData(q, destination.data() + 8, q->GetDataSize() + 1, 0) == E_INVALIDARG, "wrong size invalid");
    verify(destination == original, "invalid destination unchanged");
  }
  check(imm.GetData(nullptr, nullptr, 0, 0) == E_INVALIDARG, "null async invalid");
  // Ordinary pending polls flush; event/disjoint stalls still override DONOTFLUSH.
  unsigned before = imm.flushes;
  check(imm.GetData(&timestamp, nullptr, 0, 0) == S_FALSE && imm.flushes == before + 1, "pending flush progress");
  for (unsigned i = 0; i < kEventStallThreshold + 2; ++i)
    imm.GetData(&event, nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH);
  check(imm.flushes > before + 1, "event stall progress overrides DONOTFLUSH");
  {
    MTLD3D11EventQueryImpl<BOOL> invalid(nullptr, &event_desc);
    check(imm.GetData(&invalid, destination.data() + 8, 0, 0) == DXGI_ERROR_INVALID_CALL && destination == original,
          "undefined event status-only no write");
  }
  unsigned transition_va = WrapperCounts<VisibilityResultQuery>::allocations;
  unsigned transition_ta = WrapperCounts<TimestampQuery>::allocations;
  imm.cmd_queue.signaled = 1;
  uint64_t sample = 37; oq->issue(1, &sample, 1); pq->issue(1, &sample, 1); tq->issue(83);
  for (unsigned i = 0; i < queries.size(); ++i) {
    auto q = queries[i];
    auto label = std::to_string(i);
    auto verify = [&](bool c, std::string n) { check(c, (n + " " + label).c_str()); };
    destination = original;
    verify(imm.GetData(q, destination.data() + 8, 0, 0) == S_OK, "ready status");
    verify(destination == original, "zero-size destination unchanged");
    verify(imm.GetData(q, nullptr, 0, 0) == S_OK, "ready null status");
    destination = original;
    verify(imm.GetData(q, destination.data() + 8, q->GetDataSize(), 0) == S_OK, "exact size ready");
    verify(std::equal(destination.begin(), destination.begin() + 8, original.begin()) &&
           std::equal(destination.begin() + 8 + q->GetDataSize(), destination.end(), original.begin() + 8 + q->GetDataSize()), "exact size guards");
    uint64_t actual = 0; std::memcpy(&actual, destination.data() + 8, q->GetDataSize() < 8 ? q->GetDataSize() : 8);
    verify(actual == (i == 0 || i == 2 ? 1u : i == 1 ? 37u : i == 3 ? 83u : i == 4 ? 1000000000u : 0u), "exact size value");
  }
  unsigned transition_vd = WrapperCounts<VisibilityResultQuery>::allocations - transition_va;
  unsigned transition_td = WrapperCounts<TimestampQuery>::allocations - transition_ta;
  std::cout << "first_ready_allocations visibility=" << transition_vd << " timestamp=" << transition_td << '\n';
  check(transition_vd == 2 && transition_td == 1, "first completion rotates once per generation");
  unsigned va = WrapperCounts<VisibilityResultQuery>::allocations, ta = WrapperCounts<TimestampQuery>::allocations;
  for (unsigned i = 0; i < 100; ++i) {
    check(imm.GetData(&occlusion, nullptr, 0, 0) == S_OK, "cached visibility status");
    check(imm.GetData(&timestamp, nullptr, 0, 0) == S_OK, "cached timestamp status");
  }
  unsigned vd = WrapperCounts<VisibilityResultQuery>::allocations - va, td = WrapperCounts<TimestampQuery>::allocations - ta;
  std::cout << "cached_read_allocations visibility=" << vd << " timestamp=" << td << " reads_each=100\n";
  check(vd == 0, "cached visibility allocation bounded");
  check(td == 0, "cached timestamp allocation bounded");
  {
    OcclusionQuery fresh; fresh.desc_.Query = D3D11_QUERY_OCCLUSION;
    MTLD3D11TimestampQueryImpl fresh_ts; fresh_ts.desc_.Query = D3D11_QUERY_TIMESTAMP;
    va = WrapperCounts<VisibilityResultQuery>::allocations; ta = WrapperCounts<TimestampQuery>::allocations;
    imm.GetData(&fresh, nullptr, 0, 0); imm.GetData(&fresh_ts, nullptr, 0, 0);
    check(va == WrapperCounts<VisibilityResultQuery>::allocations && ta == WrapperCounts<TimestampQuery>::allocations,
          "initial cached reads allocate no wrappers");
  }
  // The ready backend can still be retained by readbacks. Reissue must never reset it.
  uint64_t old_value = 0;
  Rc<VisibilityResultQuery> next = static_cast<MTLD3D11OcclusionQuery &>(occlusion).Begin();
  check(next != oq && oq->getValue(&old_value) && old_value == 37, "completed backend generation preserved");
  next->begin(2, 0); static_cast<MTLD3D11OcclusionQuery &>(occlusion).End(); next->end(2, 1);
  check(imm.GetData(&occlusion, nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_FALSE, "reissue pending does not reuse ready value");
  next->issue(2, &sample, 1);
  check(imm.GetData(&occlusion, nullptr, 0, 0) == S_OK, "reissue progress");
  Rc<VisibilityResultQuery> deferred = new VisibilityResultQuery;
  deferred->begin(3, 0); deferred->end(3, 1);
  static_cast<MTLD3D11OcclusionQuery &>(occlusion).DoDeferredQuery(deferred.ptr());
  check(imm.GetData(&occlusion, nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_FALSE, "deferred replay pending");
  deferred->issue(3, &sample, 1);
  check(imm.GetData(&occlusion, nullptr, 0, 0) == S_OK, "deferred replay progress");
  Rc<VisibilityResultQuery> replay = new VisibilityResultQuery;
  replay->begin(5, 0); replay->end(5, 1);
  static_cast<MTLD3D11OcclusionQuery &>(occlusion).DoDeferredQuery(replay.ptr());
  deferred->issue(3, &sample, 1);
  check(imm.GetData(&occlusion, nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_FALSE,
        "old deferred completion cannot publish replay");
  replay->issue(5, &sample, 1);
  check(imm.GetData(&occlusion, nullptr, 0, 0) == S_OK, "second deferred replay progress");
  Rc<TimestampQuery> next_ts = static_cast<MTLD3D11TimestampQuery &>(timestamp).End();
  check(next_ts != tq, "completed timestamp generation preserved");
  tq->issue(99);
  check(imm.GetData(&timestamp, nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_FALSE,
        "old timestamp completion cannot publish reissue");
  next_ts->issue(103);
  check(imm.GetData(&timestamp, nullptr, 0, 0) == S_OK, "timestamp reissue progress");
  // An immediately-ready empty query can still have a retained old readback.
  {
    OcclusionQuery empty; empty.desc_.Query = D3D11_QUERY_OCCLUSION;
    auto &api = static_cast<MTLD3D11OcclusionQuery &>(empty);
    Rc<VisibilityResultQuery> old = api.Begin();
    old->begin(6, 0); api.End(); old->end(6, 0);
    std::vector<Rc<VisibilityResultQuery>> list{old};
    auto readback = std::make_unique<VisibilityResultReadback>(WMT::Device{}, 6, 0, list);
    check(imm.GetData(&empty, nullptr, 0, 0) == S_OK, "empty status-only ready");
    Rc<VisibilityResultQuery> fresh = api.Begin();
    check(fresh != old, "empty retained readback generation preserved");
    fresh->begin(7, 0); api.End(); fresh->end(7, 1);
    // A bad rotation would make late readback issue seq 6 into generation 7.
    if (fresh != old) readback.reset();
    check(imm.GetData(&empty, nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_FALSE,
          "late empty readback cannot publish new generation");
    fresh->issue(7, &sample, 1);
    check(imm.GetData(&empty, nullptr, 0, 0) == S_OK, "empty reissue progress");
    // Keep a failed mutation's cleanup from issuing into its deliberately wrong generation.
    if (readback) readback->queries.clear();
  }
  // API destruction must leave in-flight completion ownership usable.
  unsigned deleted = WrapperCounts<VisibilityResultQuery>::deletions;
  Rc<VisibilityResultQuery> retained;
  {
    OcclusionQuery dying;
    retained = static_cast<MTLD3D11OcclusionQuery &>(dying).Begin();
    retained->begin(4, 0); static_cast<MTLD3D11OcclusionQuery &>(dying).End(); retained->end(4, 1);
  }
  check(WrapperCounts<VisibilityResultQuery>::deletions == deleted, "backend retained beyond API lifetime");
  retained->issue(4, &sample, 1);
  check(retained->getValue(&old_value) && old_value == sample, "retained completion progress");
  retained = nullptr;
  check(WrapperCounts<VisibilityResultQuery>::deletions == deleted + 1, "retained backend final release");
}

int main(int argc, char **argv) {
  bool race_only = argc == 2 && std::string(argv[1]) == "race";
  bool frontier_only = argc == 2 && std::string(argv[1]) == "frontier";
  bool empty_only = argc == 2 && std::string(argv[1]) == "empty";
  if (!empty_only && !frontier_only) publication_race();
  if (!race_only && !frontier_only) functional();
  if (!race_only && !empty_only) { generations(); concurrent_encoder_finish_poll(); frontier(); }
  if (failures) return 1;
  std::cout << "PASS query publication" << (race_only ? " race" : " scenarios") << '\n';
}
