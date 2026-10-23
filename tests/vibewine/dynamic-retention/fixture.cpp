#include "mocks.hpp"
#define private public
#include "dynamic-under-test.hpp"
#undef private
using namespace dxmt;

template<class D> struct Traits;
template<> struct Traits<DynamicBuffer> {
  using Resource = Buffer;
  static void publish(DynamicBuffer &d, uint64_t seq, Rc<BufferAllocation> next, bool owned = false) {
    d.updateImmediateName(seq, std::move(next), 0, owned);
  }
};
template<> struct Traits<DynamicLinearTexture> {
  using Resource = Texture;
  static void publish(DynamicLinearTexture &d, uint64_t seq, Rc<TextureAllocation> next, bool owned = false) {
    d.updateImmediateName(seq, std::move(next), owned);
  }
};
template<class D> struct Subject {
  typename Traits<D>::Resource resource;
  Rc<D> dynamic = new D(&resource, {});
  auto rename(uint64_t seq, uint64_t coherent) {
    auto next = dynamic->allocate(coherent);
    Traits<D>::publish(*dynamic, seq, next);
    resource.rename(dynamic->immediateName());
    return next;
  }
  void burst(unsigned count = 32, uint64_t seq = 1) {
    for (unsigned i = 0; i < count; ++i) rename(seq, 0);
  }
};

template<class D> void baseline() {
  Subject<D> s;
  s.burst();
  assert(live_allocations == 33);
  for (unsigned seq = 1000; seq < 2000; ++seq) s.rename(seq, seq - 1);
  assert(live_allocations == 33);
}

template<class D> void tests() {
  { // Completion is not enough to evict a recent burst; preserve its reuse window.
    Subject<D> s; s.burst();
    auto created = created_allocations;
    s.rename(2, 1); assert(live_allocations == 33);
    s.rename(301, 300); assert(live_allocations == 33); // age 299
    s.rename(302, 301); assert(live_allocations == 4); // age 300, seq2/301 entries remain recent
    s.rename(602, 601); assert(live_allocations == 3); // seq302 entry remains recent
    s.rename(1001, 1000); assert(live_allocations == 2);
    assert(created_allocations == created);
  }
  assert(live_allocations == 0);
  { // A younger in-flight entry survives trimming older completed allocations.
    Subject<D> s; s.burst();
    s.rename(500, 0);
    auto inflight = s.dynamic->immediateName();
    s.rename(501, 0);
    auto current = s.dynamic->immediateName();
    const auto inflight_id = inflight->serial;
    const auto current_id = current->serial;
    auto next = s.dynamic->allocate(400);
    assert(s.dynamic->immediateName()->serial == current_id);
    assert(s.dynamic->fifo.size() == 2);
    assert(s.dynamic->fifo.front().will_free_at == 500);
    assert(s.dynamic->fifo.back().allocation->serial == inflight_id);
    assert(s.dynamic->fifo.back().will_free_at == 501);
    assert(next->serial != inflight_id && next->serial != current_id);
  }
  assert(live_allocations == 0);
  { // Dropping a pool reference cannot invalidate an outstanding rename closure/Rc.
    Subject<D> s; s.burst();
    auto external = s.dynamic->fifo.back().allocation;
    external->bytes[0] = 0xa5;
    auto next = s.dynamic->allocate(400);
    assert(s.dynamic->fifo.empty());
    assert(external->bytes[0] == 0xa5 && live_allocations == 3);
    external = nullptr;
    assert(live_allocations == 2);
  }
  assert(live_allocations == 0);
  { // A deferred command list retains ownership until recycle; current-name
    // handback must not enqueue that same current allocation twice.
    Subject<D> s;
    auto owned = s.dynamic->allocate(0);
    owned->bytes[0] = 0x5a;
    Traits<D>::publish(*s.dynamic, 1, owned, true);
    s.resource.rename(s.dynamic->immediateName());
    auto next = s.dynamic->allocate(400);
    assert(owned->bytes[0] == 0x5a);
    Traits<D>::publish(*s.dynamic, 401, next);
    s.resource.rename(s.dynamic->immediateName());
    assert(s.dynamic->fifo.empty()); // previous name still belongs to command list
    s.dynamic->recycle(402, std::move(owned));
    auto before = created_allocations;
    auto reused = s.dynamic->allocate(402);
    assert(reused->bytes[0] == 0x5a && created_allocations == before);
    Traits<D>::publish(*s.dynamic, 403, reused, true);
    s.resource.rename(s.dynamic->immediateName());
    auto current = s.dynamic->immediateName();
    auto queued = s.dynamic->fifo.size();
    s.dynamic->recycle(404, std::move(current));
    assert(!s.dynamic->owned_by_command_list_ && s.dynamic->fifo.size() == queued);
  }
  assert(live_allocations == 0);
  { // A future retirement near UINT64_MAX must not underflow into an old age.
    // A large completed ID must also trim older entries without wraparound.
    Subject<D> s;
    constexpr uint64_t maximum = ~uint64_t(0);
    auto future = s.dynamic->allocate(0);
    const auto future_id = future->serial;
    s.dynamic->recycle(maximum, std::move(future));
    auto old = s.dynamic->allocate(0);
    const auto old_id = old->serial;
    s.dynamic->recycle(1, std::move(old));
    {
      auto extra = s.dynamic->allocate(7);
      assert(s.dynamic->fifo.size() == 2);
      assert(s.dynamic->fifo.front().allocation->serial == future_id);
      assert(s.dynamic->fifo.back().allocation->serial == old_id);
      assert(extra->serial != future_id && extra->serial != old_id);
    }
    auto reserved = s.dynamic->allocate(maximum);
    assert(reserved->serial == future_id);
    assert(s.dynamic->fifo.empty() && live_allocations == 2);
  }
  assert(live_allocations == 0);
  { // Out-of-order retirement can conservatively retain an older entry behind
    // a newer in-flight head; completion of the head permits normal cleanup.
    Subject<D> s;
    auto newer = s.dynamic->allocate(0);
    const auto newer_id = newer->serial;
    s.dynamic->recycle(500, std::move(newer));
    auto older = s.dynamic->allocate(0);
    const auto older_id = older->serial;
    s.dynamic->recycle(1, std::move(older));
    {
      auto extra = s.dynamic->allocate(400);
      assert(s.dynamic->fifo.size() == 2);
      assert(s.dynamic->fifo.front().allocation->serial == newer_id);
      assert(s.dynamic->fifo.back().allocation->serial == older_id);
      assert(extra->serial != newer_id && extra->serial != older_id);
    }
    auto reserved = s.dynamic->allocate(500);
    assert(reserved->serial == newer_id);
    assert(s.dynamic->fifo.empty() && live_allocations == 2);
  }
  assert(live_allocations == 0);
  { // Repeated recent bursts reuse their highwater instead of reallocating.
    Subject<D> s; s.burst();
    auto created = created_allocations;
    for (unsigned cycle = 0; cycle < 100; ++cycle)
      for (unsigned i = 0; i < 32; ++i) s.rename(2 + cycle, 1 + cycle);
    assert(created_allocations == created && live_allocations == 33);
    s.rename(1000, 999);
    assert(live_allocations == 2);
  }
  assert(live_allocations == 0);
}
int main(int argc, char **) {
  if (argc > 1) { baseline<DynamicBuffer>(); assert(live_allocations == 0); baseline<DynamicLinearTexture>(); }
  else { tests<DynamicBuffer>(); tests<DynamicLinearTexture>(); }
  assert(live_allocations == 0);
  std::cout << (argc > 1 ? "PASS baseline: both dynamic pools retain 33 allocations after 1000 completed cycles\n" : "PASS: age/completion, current/inflight, external ownership, command-list ownership, burst reuse, destruction\n");
}
