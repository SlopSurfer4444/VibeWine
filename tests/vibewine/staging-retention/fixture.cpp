#include "mocks.hpp"
// Fault/count all C++ allocations only while a bounded source operation is under test.
static thread_local long fail_after = -1;
static thread_local size_t allocation_attempts = 0;
void *operator new(std::size_t size) {
  if (fail_after >= 0) {
    ++allocation_attempts;
    if (fail_after-- == 0) throw std::bad_alloc();
  }
  if (auto pointer = std::malloc(size ? size : 1)) return pointer;
  throw std::bad_alloc();
}
void operator delete(void *pointer) noexcept { std::free(pointer); }
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void *pointer) noexcept { ::operator delete(pointer); }
void operator delete(void *pointer, std::size_t) noexcept { ::operator delete(pointer); }
void operator delete[](void *pointer, std::size_t) noexcept { ::operator delete(pointer); }
// Inspect only slot count/current identity; all state transitions use production methods.
#define private public
#include "staging-under-test.hpp"
#undef private

using namespace dxmt;
constexpr size_t length = 4096;
using Pending = std::vector<std::function<void()>>;

template <class T> static size_t free_capacity(const T &r) {
  if constexpr (requires { r.free_slots_; }) return r.free_slots_.capacity();
  else return 0; // Allows compilation of the immutable predecessor in baseline mode.
}
template <class T> static void check_capacity(const T &r) {
  if constexpr (requires { r.free_slots_; }) assert(free_capacity(r) >= r.buffer_pool.size());
}

static Rc<StagingResource> resource() {
  return new StagingResource({}, length, length, length);
}

static uint64_t rename(const Rc<StagingResource> &r, uint64_t seq, uint64_t coherent, Pending &pending) {
  r->useCopySource(seq);
  assert(r->tryMap(coherent, false, true) == StagingMapResult::Renamable);
  const auto next = r->allocate(coherent);
  check_capacity(*r);
  assert(next != r->immediate_name_);
  std::memcpy(r->mappedMemory(next), r->mappedImmediateMemory(), length);
  r->updateImmediateName(seq, next);
  // Same strong resource + numeric ID capture as immediate-context rename.
  pending.emplace_back([r, next] { r->buffer()->rename(r->allocation(next)); });
  r->unmap();
  return next;
}

static void encode(Pending &pending) {
  for (auto &command : pending) command();
  pending.clear(); // Models chunk.reset before advancing CPU coherence.
}

static void retention(bool baseline) {
  auto r = resource();
  Pending pending;
  std::memset(r->mappedImmediateMemory(), 0x4b, length);
  std::vector<uint64_t> ids{r->immediate_name_};
  for (unsigned i = 0; i < 32; ++i) ids.push_back(rename(r, 10, 0, pending));
  r->useCopySource(10);
  assert(live_allocations == 33);
  assert(r->tryMap(9, false, true) == StagingMapResult::Renamable);
  assert(live_allocations == 33); // One before retirement: no premature frees.
  for (auto id : ids) {
    auto allocation = r->allocation(id);
    assert(allocation && allocation->bytes.front() == 0x4b && allocation->bytes.back() == 0x4b);
  }
  encode(pending);
  assert(r->tryMap(10, false, true) == StagingMapResult::Mappable);
  assert(live_allocations == 33); // Fresh completed copies remain reusable.
  r->tryMap(309, false, true);
  assert(live_allocations == 33); // Age 299: preserve the reuse window.
  r->tryMap(310, false, true);
  const auto expected = baseline ? 33u : 2u;
  assert(live_allocations == expected);
  for (unsigned i = 0; i < 10000; ++i) assert(r->tryMap(310, false, true) == StagingMapResult::Mappable);
  assert(live_allocations == expected);
  std::cout << "retention: " << live_allocations << " allocations after coherent map\n";
}

static void frontier_and_queued_ids() {
  auto r = resource();
  Pending older, newer;
  for (unsigned i = 0; i < 4; ++i) rename(r, 10, 0, older);
  std::vector<uint64_t> still_pending{r->immediate_name_};
  for (unsigned i = 0; i < 3; ++i) still_pending.push_back(rename(r, 1000, 0, newer));
  r->useCopySource(1000);
  encode(older);
  assert(r->tryMap(9, false, true) == StagingMapResult::Renamable);
  assert(live_allocations == 8);
  assert(r->tryMap(10, false, true) == StagingMapResult::Renamable);
  assert(live_allocations == 8);
  r->tryMap(309, false, true);
  assert(live_allocations == 8);
  r->tryMap(310, false, true);
  assert(live_allocations == 5); // Three retired-but-inflight + current + one completed spare.
  for (auto id : still_pending) assert(r->allocation(id));
  encode(newer); // Would dereference a null/aliased slot if trimming crossed the frontier.
  assert(r->tryMap(1000, false, true) == StagingMapResult::Mappable);
  assert(live_allocations == 5);
  r->tryMap(1300, false, true);
  assert(live_allocations == 2);
}

static void repeated_bursts() {
  auto r = resource();
  size_t peak_slots = 0;
  const auto before_created = created_allocations.load();
  for (uint64_t seq = 1; seq <= 100; ++seq) {
    Pending pending;
    const auto pattern = static_cast<unsigned char>(seq);
    std::memset(r->mappedImmediateMemory(), pattern, length);
    for (unsigned i = 0; i < 32; ++i) rename(r, seq, seq - 1, pending);
    peak_slots = std::max(peak_slots, r->buffer_pool.size());
    assert(peak_slots == 33); // Vacant numeric slots must be reused, not appended forever.
    encode(pending);
    assert(r->tryMap(seq, false, true) == StagingMapResult::Mappable);
    assert(live_allocations == 33);
    assert(created_allocations == before_created + 32); // No allocate/free churn within TTL.
    auto current = r->allocation(r->immediate_name_);
    for (auto byte : current->bytes) assert(byte == pattern);
  }
  r->tryMap(400, false, true);
  assert(live_allocations == 2);
  std::cout << "repeated bursts: peak slots=" << peak_slots << ", live=" << live_allocations << "\n";
}

static void external_ownership() {
  auto r = resource();
  Pending pending;
  std::vector<Rc<BufferAllocation>> external;
  external.push_back(r->allocation(r->immediate_name_));
  for (unsigned i = 0; i < 32; ++i) external.push_back(r->allocation(rename(r, 1, 0, pending)));
  encode(pending);
  r->tryMap(301, false, true);
  assert(live_allocations == 33); // Trimming releases pool ownership, never external ownership.
  size_t trimmed_but_externally_owned = 0;
  for (size_t id = 0; id < r->buffer_pool.size(); ++id)
    if (!r->buffer_pool[id]) {
      assert(external[id] && external[id]->bytes.size() == length);
      ++trimmed_but_externally_owned;
    }
  assert(trimmed_but_externally_owned == 31);
  for (auto &allocation : external) allocation->bytes.back() = 0xa5;
  external.clear();
  assert(live_allocations == 2);
  auto held = r->allocation(r->immediate_name_);
  r = nullptr;
  assert(live_allocations == 1 && held->bytes.back() == 0xa5);
}

static void vacant_slots_reused_after_aging() {
  auto r = resource();
  uint64_t coherent = 0;
  for (uint64_t seq : {1u, 302u, 603u}) {
    Pending pending;
    std::vector<uint64_t> ids{r->immediate_name_};
    for (unsigned i = 0; i < 32; ++i) {
      auto next = rename(r, seq, coherent, pending);
      assert(std::find(ids.begin(), ids.end(), next) == ids.end());
      ids.push_back(next);
    }
    assert(r->buffer_pool.size() == 33 && live_allocations == 33);
    for (auto id : ids) assert(r->allocation(id));
    encode(pending);
    coherent = seq + 300;
    r->tryMap(coherent, false, true);
    assert(live_allocations == 2);
  }
}

static void reserved_id_survives_trim() {
  auto r = resource();
  Pending pending;
  for (unsigned i = 0; i < 32; ++i) rename(r, 1, 0, pending);
  encode(pending);
  // allocate removes a reusable ID from the retirement queue before updateImmediateName.
  const auto reserved = r->allocate(1);
  const auto reserved_serial = r->allocation(reserved)->serial;
  assert(reserved != r->immediate_name_);
  r->tryMap(301, false, true);
  assert(r->allocation(reserved) && r->allocation(reserved)->serial == reserved_serial);
  r->updateImmediateName(2, reserved);
  r->buffer()->rename(r->allocation(reserved));
  r->tryMap(302, false, true);
  assert(r->allocation(r->immediate_name_)->serial == reserved_serial);
  assert(live_allocations == 2);
}

static void deferred_resource_capture() {
  auto r = resource();
  const auto initial_serial = r->buffer()->current->serial;
  size_t observed_serial = 0;
  // Deferred copy commands capture Rc<StagingResource> and resolve buffer() on execution.
  std::function<void()> replay = [r, &observed_serial] { observed_serial = r->buffer()->current->serial; };
  Pending pending;
  for (unsigned i = 0; i < 32; ++i) rename(r, 1, 0, pending);
  encode(pending);
  r->tryMap(301, false, true);
  auto expected_serial = r->buffer()->current->serial;
  assert(expected_serial != initial_serial);
  r = nullptr;
  replay();
  assert(observed_serial == expected_serial && live_allocations == 2);
  replay = {};
  assert(live_allocations == 0);
}

static void concurrent_lookup() {
  auto r = resource();
  const auto pinned_id = r->immediate_name_;
  const auto pinned_serial = r->allocation(pinned_id)->serial;
  std::atomic<bool> done = false;
  std::thread reader([&] {
    while (!done) {
      auto allocation = r->allocation(pinned_id);
      assert(allocation && allocation->serial == pinned_serial);
    }
  });
  Pending pending;
  for (unsigned i = 0; i < 128; ++i) rename(r, 1, 0, pending);
  done = true;
  reader.join();
  encode(pending);
  r->tryMap(301, false, true);
  assert(live_allocations == 2);
}

static void trim_never_allocates() {
  auto r = resource();
  Pending pending;
  for (unsigned i = 0; i < 32; ++i) rename(r, 1, 0, pending);
  encode(pending);
  check_capacity(*r);
  allocation_attempts = 0;
  fail_after = 0;
  try { r->tryMap(301, false, true); }
  catch (...) { fail_after = -1; throw; }
  fail_after = -1;
  assert(allocation_attempts == 0 && live_allocations == 2);
}

static Rc<StagingResource> prepared_growth() {
  auto r = resource();
  Pending pending;
  rename(r, 1, 0, pending);
  encode(pending);
  assert(r->buffer_pool.size() == 2 && !r->fifo.empty());
  return r;
}

static void growth_failure_preserves_identity() {
  size_t growth_allocations;
  {
    auto r = prepared_growth();
    allocation_attempts = 0;
    fail_after = 1000;
    r->allocate(0);
    fail_after = -1;
    growth_allocations = allocation_attempts;
  }
  assert(live_allocations == 0);
  // Buffer object + backing bytes + freelist reserve + pool growth must all be faulted.
  assert(growth_allocations >= 4);
  bool saw_failure_after_successful_reserve = false;
  for (size_t point = 0; point < growth_allocations; ++point) {
    auto r = prepared_growth();
    const auto pool = r->buffer_pool;
    auto fifo = r->fifo;
    const auto immediate = r->immediate_name_;
    auto current = r->buffer()->current;
    const auto capacity = free_capacity(*r);
    bool failed = false;
    allocation_attempts = 0;
    fail_after = static_cast<long>(point);
    try { r->allocate(0); }
    catch (const std::bad_alloc &) { failed = true; }
    catch (...) { fail_after = -1; throw; }
    fail_after = -1;
    assert(failed && allocation_attempts == point + 1);
    assert(r->buffer_pool.size() == pool.size() && r->immediate_name_ == immediate);
    for (size_t id = 0; id < pool.size(); ++id) assert(r->allocation(id) == pool[id]);
    assert(r->buffer()->current == current && live_allocations == 2);
    auto actual_fifo = r->fifo;
    assert(actual_fifo.size() == fifo.size());
    while (!fifo.empty()) {
      assert(actual_fifo.front().id == fifo.front().id);
      assert(actual_fifo.front().will_free_at == fifo.front().will_free_at);
      actual_fifo.pop(); fifo.pop();
    }
    check_capacity(*r);
    if (free_capacity(*r) > capacity) saw_failure_after_successful_reserve = true;
  }
  assert(saw_failure_after_successful_reserve && live_allocations == 0);
  std::cout << "allocation failures: " << growth_allocations << " growth points preserved identity; trim allocated zero times\n";
}

int main(int argc, char **) {
  const bool baseline = argc > 1;
  retention(baseline);
  assert(live_allocations == 0);
  if (baseline) return 0;
  frontier_and_queued_ids(); assert(live_allocations == 0);
  repeated_bursts(); assert(live_allocations == 0);
  vacant_slots_reused_after_aging(); assert(live_allocations == 0);
  external_ownership(); assert(live_allocations == 0);
  reserved_id_survives_trim(); assert(live_allocations == 0);
  deferred_resource_capture(); assert(live_allocations == 0);
  concurrent_lookup(); assert(live_allocations == 0);
  trim_never_allocates(); assert(live_allocations == 0);
  growth_failure_preserves_identity(); assert(live_allocations == 0);
  std::cout << "PASS: retention, frontier, queued IDs, slot reuse, external Rc, deferred capture, concurrent lookup\n";
}
