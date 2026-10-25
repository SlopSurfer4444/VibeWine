#include "mocks.hpp"
#include <cstdlib>
#include <new>
static thread_local bool fail_new = false;
void *operator new(std::size_t size) {
  if (fail_new) throw std::bad_alloc();
  if (auto ptr = std::malloc(size ? size : 1)) return ptr;
  throw std::bad_alloc();
}
void operator delete(void *ptr) noexcept { std::free(ptr); }
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void *ptr) noexcept { ::operator delete(ptr); }
void operator delete(void *ptr, std::size_t) noexcept { ::operator delete(ptr); }
void operator delete[](void *ptr, std::size_t) noexcept { ::operator delete(ptr); }
#define private public
#include "owners-under-test.hpp"
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
  Rc<D> dynamic;
  Subject(std::shared_ptr<IdleRetirementDomain> domain) {
    resource.rename(resource.allocate({}));
    dynamic = new D(&resource, {}
#if DXMT_IDLE_RETIREMENT
        , domain
#endif
    );
    (void)domain;
  }
  auto rename(uint64_t seq, uint64_t coherent = 0) {
    auto next = dynamic->allocate(coherent);
    Traits<D>::publish(*dynamic, seq, next);
    resource.rename(dynamic->immediateName());
    return next;
  }
  void burst(unsigned count = 32, uint64_t seq = 1) { while (count--) rename(seq); }
};

#if DXMT_IDLE_RETIREMENT
template<class D> void dynamic_tests() {
  { // No subsequent Map or allocate: completion heartbeat alone retires excess.
    auto domain = std::make_shared<IdleRetirementDomain>();
    Subject<D> s(domain); s.burst();
    const auto current = s.dynamic->immediateName();
    auto sample = domain->sweep(300);
    assert(live_allocations == 33 && sample.completed_idle.count == 32);
    fail_new = true;
    sample = domain->sweep(301);
    fail_new = false;
    assert(live_allocations == 2 && sample.released_count == 31);
    assert(sample.released_bytes == 31 * 4096 && sample.current.count == 1);
    assert(s.dynamic->immediateName().ptr() == current.ptr());
    const auto created = created_allocations.load();
    s.rename(302, 301);
    assert(created_allocations == created); // Sweep preserved a warm spare.
  }
  assert(live_allocations == 0);
  { // Recorded/deferred refs survive dropping the pool's independent reference.
    auto domain = std::make_shared<IdleRetirementDomain>();
    Subject<D> s(domain); s.burst();
    auto recorded = s.dynamic->fifo.back().allocation;
    recorded->bytes[0] = 0xa5;
    s.rename(1000); auto inflight = s.dynamic->immediateName(); s.rename(1001);
    const auto current = s.dynamic->immediateName();
    auto sample = domain->sweep(400);
    assert(recorded->bytes[0] == 0xa5 && sample.inflight.count == 2);
    assert(s.dynamic->immediateName().ptr() == current.ptr());
    auto command_owned = s.dynamic->allocate(0);
    command_owned->bytes[0] = 0x5a;
    Traits<D>::publish(*s.dynamic, 1002, command_owned, true);
    s.resource.rename(s.dynamic->immediateName());
    s.rename(1003); // command-owned previous name stays outside FIFO.
    domain->sweep(2000);
    assert(command_owned->bytes[0] == 0x5a);
    s.dynamic->recycle(2001, std::move(command_owned));
    domain->sweep(2400);
  }
  assert(live_allocations == 0);
  { // Queue domain sequence isolation; a fast different queue cannot retire it.
    auto slow = std::make_shared<IdleRetirementDomain>();
    auto fast = std::make_shared<IdleRetirementDomain>();
    Subject<D> a(slow), b(fast); a.burst(); b.burst();
    fast->sweep(~uint64_t(0));
    assert(a.dynamic->fifo.size() == 32);
    slow->sweep(1); assert(a.dynamic->fifo.size() == 31); // warm spare only
    assert(live_allocations == 35);
  }
  assert(live_allocations == 0);
}

static void staging_tests() {
  auto domain = std::make_shared<IdleRetirementDomain>();
  Rc<StagingResource> r = new StagingResource({}, 4096, 4096, 4096, domain);
  std::vector<std::function<void()>> pending;
  for (unsigned i = 0; i < 32; ++i) {
    auto next = r->allocate(0);
    r->updateImmediateName(1, next);
    pending.emplace_back([r, next] { r->buffer()->rename(r->allocation(next)); });
  }
  const auto current_id = r->immediate_name_;
  domain->sweep(0); assert(live_allocations == 33);
  for (auto &command : pending) command();
  pending.clear(); // Models actual chunk.reset before publishing coherent seq.
  auto external = r->allocation(r->fifo.back().id);
  external->bytes[0] = 0x83;
  domain->sweep(300); assert(live_allocations == 33);
  const auto reserved = r->allocate(0); // Reserved is not FIFO, current, or idle.
  fail_new = true;
  auto sample = domain->sweep(301);
  fail_new = false;
  assert(sample.unclassified.count == 1); // protected allocated-but-unpublished slot
  assert(r->allocation(reserved) && r->allocation(current_id));
  assert(external->bytes[0] == 0x83 && live_allocations == 4);
  assert(r->immediate_name_ == current_id);
  external = nullptr;
  auto next = r->allocate(301);
  assert(next != reserved && r->buffer_pool.size() == 33); // reuse a freed stable ID
}

static void bounded_and_races() {
  { // Fair resource cursor, entry/release limits, unregister cursor/head/tail.
    auto domain = std::make_shared<IdleRetirementDomain>();
    std::vector<std::unique_ptr<Subject<DynamicBuffer>>> subjects;
    for (unsigned i = 0; i < 50; ++i) {
      subjects.push_back(std::make_unique<Subject<DynamicBuffer>>(domain));
      subjects.back()->burst(160);
    }
    auto sample = domain->sweep(301);
    assert(sample.scanned_resources <= 16 && sample.released_count <= 64);
    assert(sample.unclassified.count > 0);
    subjects.erase(subjects.begin() + 20); subjects.pop_back(); subjects.erase(subjects.begin());
    for (unsigned i = 0; i < 300; ++i) domain->sweep(301);
    assert(live_allocations == subjects.size() * 2);
  }
  assert(live_allocations == 0);
  { // An oversized oldest head must not monopolize every resource sweep.
    auto domain = std::make_shared<IdleRetirementDomain>();
    std::vector<std::unique_ptr<Subject<DynamicBuffer>>> subjects;
    for (unsigned i = 0; i < IdleRetirementDomain::kScanResources; ++i) {
      subjects.push_back(std::make_unique<Subject<DynamicBuffer>>(domain));
      subjects.back()->burst(160);
    }
    // Last registration is first in the intrusive list, exactly one full
    // resource cycle wide. Its long, unreleasable tail exhausts entry budget.
    for (const auto &entry : subjects.back()->dynamic->fifo.entries())
      entry.allocation->native_size = IdleRetirementDomain::kReleaseBytes + 1;
    for (unsigned i = 0; i < 100; ++i) domain->sweep(301);
    assert(subjects.back()->dynamic->fifo.size() == 159);
    for (unsigned i = 0; i + 1 < subjects.size(); ++i)
      assert(subjects[i]->dynamic->fifo.empty());
  }
  assert(live_allocations == 0);
  { // More than registry cap: explicit exclusions, no strong keepalive/leak.
    auto domain = std::make_shared<IdleRetirementDomain>();
    std::vector<std::unique_ptr<Subject<DynamicBuffer>>> subjects;
    for (unsigned i = 0; i < IdleRetirementDomain::kResourceLimit + 1; ++i)
      subjects.push_back(std::make_unique<Subject<DynamicBuffer>>(domain));
    auto sample = domain->sweep(1000);
    assert(sample.registered_resources == 4096 && sample.excluded_resources == 1);
    subjects.clear(); sample = domain->sweep(1000);
    assert(sample.registered_resources == 0 && sample.excluded_resources == 0);
  }
  assert(live_allocations == 0);
  { // External domain owner can die first; registrations retain domain only.
    auto domain = std::make_shared<IdleRetirementDomain>();
    std::weak_ptr<IdleRetirementDomain> weak = domain;
    auto s = std::make_unique<Subject<DynamicBuffer>>(domain);
    domain.reset(); assert(!weak.expired());
    s.reset(); assert(weak.expired());
  }
  { // Busy owner is skipped instead of waiting on its resource lock.
    auto domain = std::make_shared<IdleRetirementDomain>();
    Subject<DynamicBuffer> s(domain); s.burst();
    std::lock_guard lock(s.dynamic->mutex_);
    auto sample = domain->sweep(1000);
    assert(sample.skipped_busy == 1 && sample.released_count == 0);
  }
  { // Destruction/unregister racing callbacks must never visit torn-down state.
    auto domain = std::make_shared<IdleRetirementDomain>();
    std::atomic<bool> stop = false;
    std::thread sweeper([&] { while (!stop.load()) domain->sweep(1000); });
    for (unsigned i = 0; i < 1000; ++i) {
      Subject<DynamicBuffer> s(domain); s.burst(4);
      auto next = s.dynamic->allocate(0); s.dynamic->recycle(1, std::move(next));
    }
    stop = true; sweeper.join();
    assert(domain->sweep(1000).registered_resources == 0);
  }
  assert(live_allocations == 0);
  { // Byte cap prevents releasing oversized allocations or guessing zero size.
    IdleRetirementDomain::Budget budget;
    assert(!budget.release(0));
    assert(!budget.release(IdleRetirementDomain::kReleaseBytes + 1));
    assert(budget.release(IdleRetirementDomain::kReleaseBytes));
    assert(!budget.release(1));
  }
}
#endif

int main(int argc, char **) {
  (void)argc;
#if DXMT_IDLE_RETIREMENT
  dynamic_tests<DynamicBuffer>(); dynamic_tests<DynamicLinearTexture>();
  staging_tests(); assert(live_allocations == 0);
  bounded_and_races();
  std::cout << "PASS: idle-only maintenance, ownership, staging IDs, domain isolation, budgets/cap, unregister/races\n";
#else
  {
    auto domain = std::make_shared<IdleRetirementDomain>();
    Subject<DynamicBuffer> s(domain); s.burst();
    domain->sweep(1000);
    if (argc > 1) return live_allocations == 2 ? 0 : 2;
    assert(live_allocations == 33); // Old behavior fails new 2-allocation target.
  }
  assert(live_allocations == 0);
  {
    auto domain = std::make_shared<IdleRetirementDomain>();
    Subject<DynamicLinearTexture> s(domain); s.burst();
    domain->sweep(1000); assert(live_allocations == 33);
  }
  assert(live_allocations == 0);
  {
    auto domain = std::make_shared<IdleRetirementDomain>();
    Rc<StagingResource> r = new StagingResource({}, 4096, 4096, 4096);
    for (unsigned i = 0; i < 32; ++i) {
      auto next = r->allocate(0); r->updateImmediateName(1, next);
      r->buffer()->rename(r->allocation(next));
    }
    domain->sweep(1000); assert(live_allocations == 33);
  }
  assert(live_allocations == 0);
  std::cout << "BASELINE reproduced: queue completion alone retains 33 allocations (candidate target 2)\n";
#endif
}
