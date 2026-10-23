#include "mocks.hpp"
#include "ring-under-test.hpp"
#include <string_view>

using Ring = dxmt::RingBumpState<CountedAllocator, 64>;
constexpr auto grace_calls = dxmt::kStagingBlockLifetime + 1;
constexpr uint64_t all = UINT64_MAX;

void idle(Ring &ring, uint64_t coherent, size_t calls = grace_calls) {
  for (size_t i = 0; i < calls; ++i)
    ring.free_blocks(coherent);
}

void same_sequence_burst(bool baseline) {
  Counts counts;
  Ring ring{CountedAllocator{counts}};
  CountedAllocator::Block *warm = nullptr;
  for (unsigned i = 0; i < 10; ++i)
    warm = &ring.allocate(1, 0, 64, 1).first;
  auto *payload = warm->payload.get();
  const auto warm_id = payload->id;
  idle(ring, 1);
  assert(counts.live == 10); // Original code retains all ten through call 301.
  ring.free_blocks(1);
  if (baseline) {
    assert(counts.live == 9); // Only its former front starts each grace period.
    std::cout << "baseline reproduced: 10 retained after301; 9 after302\n";
  } else {
    assert(counts.live == 1);
    assert(warm->payload.get() == payload && counts.alive[warm_id]);
    idle(ring, 1, 1000);
    assert(counts.live == 1);
    // Reuse the spare after coherence has advanced. No new allocation needed.
    auto &reused = ring.allocate(3, 2, 64, 1).first;
    assert(reused.payload->id == warm_id && counts.created == 10);
  }
  ring.free_blocks(all);
  assert(counts.live == 0);
}

void pending_tail_and_stable_references() {
  Counts counts;
  Ring ring{CountedAllocator{counts}};
  for (unsigned i = 0; i < 10; ++i)
    ring.allocate(1, 0, 64, 1);
  auto &pending = ring.allocate(2, 0, 16, 1).first;
  auto *address = &pending;
  auto *payload = pending.payload.get();
  payload->value = 1234;
  idle(ring, 1, 1000);
  assert(counts.live == 2 && &pending == address);
  assert(pending.payload.get() == payload && payload->value == 1234);
  auto [tail, offset] = ring.allocate(2, 1, 16, 1);
  assert(&tail == address && offset == 16);
  ring.free_blocks(2);
  // The old, already aged spare can now retire; the newly completed tail is
  // retained in place as the newest warm standard block.
  assert(counts.live == 1 && pending.payload.get() == payload);
  idle(ring, 2, 1000);
  assert(counts.live == 1 && pending.payload.get() == payload);
  ring.free_blocks(all);
  assert(counts.live == 0);
}

void recently_reused_tail_resets_grace() {
  Counts counts;
  Ring ring{CountedAllocator{counts}};
  ring.allocate(1, 0, 64, 1);
  auto &tail = ring.allocate(1, 0, 16, 1).first;
  auto id = tail.payload->id;
  idle(ring, 1);
  auto [same_tail, offset] = ring.allocate(2, 1, 16, 1);
  assert(&same_tail == &tail && offset == 16);
  ring.allocate(3, 1, 64, 1); // A newer standard block becomes the warm reserve.
  ring.free_blocks(3); // Old first block expires, recently used tail must not.
  assert(counts.live == 2 && counts.alive[id]);
  idle(ring, 3, dxmt::kStagingBlockLifetime);
  assert(counts.live == 2 && counts.alive[id]);
  ring.free_blocks(3);
  assert(counts.live == 1 && !counts.alive[id]);
}

void front_reuse_resets_grace() {
  Counts counts;
  Ring ring{CountedAllocator{counts}};
  ring.allocate(1, 0, 64, 1);
  idle(ring, 1);
  auto &reused = ring.allocate(3, 2, 64, 1).first;
  auto id = reused.payload->id;
  ring.allocate(4, 2, 64, 1);
  idle(ring, 4);
  assert(counts.live == 2 && counts.alive[id]);
  ring.free_blocks(4);
  assert(counts.live == 1 && !counts.alive[id]);
}

void interior_cleanup_preserves_current_tail() {
  Counts counts;
  Ring ring{CountedAllocator{counts}};
  auto &warm = ring.allocate(1, 0, 64, 1).first;
  ring.allocate(2, 0, 128, 1);
  auto &tail = ring.allocate(3, 0, 16, 1).first;
  auto *warm_address = &warm;
  auto *tail_address = &tail;
  auto *payload = tail.payload.get();
  payload->value = 999;
  ring.free_blocks(2); // Erase between the warm spare and a producer-owned tail.
  assert(counts.live == 2 && &warm == warm_address && &tail == tail_address);
  assert(tail.payload.get() == payload && payload->value == 999);
  auto [same_tail, offset] = ring.allocate(3, 2, 16, 1);
  assert(&same_tail == tail_address && offset == 16);
}

void adhoc_advance_sentinel_empty() {
  Counts counts;
  Ring ring{CountedAllocator{counts}};
  ring.free_blocks(0);
  ring.free_blocks(all);
  auto &spare = ring.allocate(1, 0, 64, 1).first;
  auto spare_id = spare.payload->id;
  auto &oversized = ring.allocate(2, 0, 128, 1).first;
  auto oversized_id = oversized.payload->id;
  ring.free_blocks(1);
  assert(counts.live == 2 && counts.alive[oversized_id]);
  ring.free_blocks(2); // A warm spare must not shield the completed ad-hoc block.
  assert(counts.live == 1 && counts.alive[spare_id] && !counts.alive[oversized_id]);
  for (unsigned i = 0; i < 4; ++i)
    ring.allocate(3, 2, 64, 1);
  ring.free_blocks(3 + dxmt::kStagingBlockLifetime + 1);
  assert(counts.live == 1); // Advancing coherence retains only the warm reserve.
  // The established sentinel means unconditional teardown, even for pending IDs.
  ring.allocate(1000, 0, 128, 1);
  ring.free_blocks(all);
  assert(counts.live == 0);
  ring.free_blocks(all);
  auto [next, offset] = ring.allocate(1001, 1000, 16, 16);
  assert(next.payload && offset == 0);
}

void concurrent_producer_and_collector() {
  Counts counts;
  Ring ring{CountedAllocator{counts}};
  std::atomic<uint64_t> coherent = 0;
  std::atomic<bool> done = false;
  std::thread collector([&] {
    while (!done.load(std::memory_order_acquire))
      ring.free_blocks(coherent.load(std::memory_order_acquire));
  });
  for (uint64_t seq = 1; seq <= 1000; ++seq) {
    auto &block = ring.allocate(seq, coherent.load(std::memory_order_acquire), 64, 1).first;
    const auto id = block.payload->id;
    block.payload->value = seq;
    std::this_thread::yield();
    assert(counts.alive[id] && block.payload->value == seq);
    // The collector may reclaim this block only after its last producer access.
    coherent.store(seq, std::memory_order_release);
  }
  done.store(true, std::memory_order_release);
  collector.join();
  idle(ring, 1000, grace_calls + 1);
  assert(counts.live == 1);
  ring.free_blocks(all);
  assert(counts.live == 0);
}

int main(int argc, char **argv) {
  bool baseline = argc == 2 && std::string_view(argv[1]) == "baseline";
  same_sequence_burst(baseline);
  if (baseline)
    return 0;
  pending_tail_and_stable_references();
  recently_reused_tail_resets_grace();
  front_reuse_resets_grace();
  interior_cleanup_preserves_current_tail();
  adhoc_advance_sentinel_empty();
  concurrent_producer_and_collector();
  std::cout << "ring retention: all ownership, grace and concurrency regressions passed\n";
}
