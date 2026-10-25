#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>
static void require(bool ok, const char *why) {
  if (!ok) { std::cerr << "CONTRACT: " << why << '\n'; std::_Exit(1); }
}
struct Stats {
  size_t attempts = 0, allocations = 0, frees = 0, copied = 0, live = 0, peak = 0, last_request = 0;
  size_t expected_copy_size = 0;
  bool fail = false;
  std::map<void *, size_t> objects;
  std::vector<size_t> requests;
} stats;
static void *test_malloc(size_t bytes) {
  ++stats.attempts; stats.last_request = bytes;
  if (stats.fail) return nullptr;
  require(bytes <= 8 * 1024 * 1024, "no giant fixture allocation");
  auto *p = std::malloc(bytes);
  require(p != nullptr, "fixture host allocation");
  ++stats.allocations; stats.requests.push_back(bytes); stats.objects[p] = bytes;
  stats.live += bytes; stats.peak = std::max(stats.peak, stats.live);
  return p;
}
static void test_free(void *p) {
  if (!p) return;
  auto found = stats.objects.find(p);
  require(found != stats.objects.end(), "free owned current allocation");
  ++stats.frees; stats.live -= found->second; stats.objects.erase(found);
  std::free(p);
}
static void *test_memcpy(void *dst, const void *src, size_t bytes) {
  require(bytes == stats.expected_copy_size, "copy exactly previously occupied bytes");
  stats.copied += bytes;
  return std::memcpy(dst, src, bytes);
}
#include "dxbc_instructions.hpp"
struct Tiny { uint8_t value; };
struct alignas(8) Medium { std::array<uint64_t, 3> value; };
struct alignas(8) Wide { std::array<uint64_t, 6> value; };
struct alignas(8) Big { std::array<uint64_t, 512> value; };
static_assert(alignof(Wide) <= alignof(std::max_align_t));
using List = dxmt::PackedVariantList<Tiny, Medium, Wide>;
using Value = List::variant;
static std::vector<Value> expected;
static void verify(const List &list) {
  size_t index = 0;
  list.for_each([&](const auto &v) {
    require(index < expected.size(), "visitor count upper bound");
    using T = std::decay_t<decltype(v)>;
    require(std::holds_alternative<T>(expected[index]), "visitor type and order");
    require(std::memcmp(&v, &std::get<T>(expected[index]), sizeof(T)) == 0, "visitor payload bytes");
    require(reinterpret_cast<uintptr_t>(&v) % alignof(T) == 0, "fundamental payload alignment");
    ++index;
  });
  require(index == expected.size(), "visitor count");
}
static size_t aligned(size_t n, size_t a) { return (n + a - 1) & ~(a - 1); }
static void append(List &list, Value value) {
  const auto before = list.test_state();
  stats.expected_copy_size = before.used;
  size_t required = std::visit([&](const auto &v) {
    return aligned(aligned(before.used + sizeof(uint32_t), alignof(decltype(v))) + sizeof(v), sizeof(uint32_t));
  }, value);
  auto attempts = stats.attempts, copied = stats.copied;
  list.push_back(value); expected.push_back(value);
  const auto after = list.test_state();
  require(after.used == required, "packed byte layout unchanged");
  if (required <= before.capacity)
    require(stats.attempts == attempts && stats.copied == copied, "append fitting capacity allocates and copies nothing");
  else {
    require(stats.attempts == attempts + 1, "one allocation per required expansion");
#if DXMT_PACKED_IR_GROWTH
    auto choice = before.capacity ? before.capacity + before.capacity / 2 : size_t{1024};
    require(after.capacity == std::max(required, choice), "1.5x-or-required allocation choice");
    require(after.capacity == 1024 || after.capacity < required + (required + 1) / 2,
            "retained capacity below 1.5x required except 1024 floor");
#else
    auto choice = before.capacity ? before.capacity : size_t{1024};
    while (choice < required) choice += 1024;
    require(after.capacity == choice, "default retains historical exact capacity choice");
#endif
  }
}
static void reset() {
  require(stats.objects.empty() && stats.live == 0, "all fixture allocations released");
  stats = Stats{}; expected.clear();
}
static void corpus() {
  reset();
  {
    List list;
    require(!list.any([](const auto &) { return true; }), "empty any");
    for (uint32_t i = 0; i < 12000; ++i) {
      if (i % 3 == 0) append(list, Tiny{uint8_t(i)});
      else if (i % 3 == 1) append(list, Medium{{i, i * uint64_t{17}, ~uint64_t{i}}});
      else append(list, Wide{{i, i + 1u, i * uint64_t{31}, ~uint64_t{i}, i ^ 0xa5a5a5a5u, uint64_t{i} << 32}});
    }
    verify(list);
    size_t visits = 0;
    require(list.any([&](const auto &) { return ++visits == 99; }) && visits == 99, "any early stop");
    require(!list.any([](const auto &) { return false; }), "any complete miss");
    const auto end = list.test_state();
    std::cout << "METRICS {\"count\":" << expected.size() << ",\"used\":" << end.used
              << ",\"capacity\":" << end.capacity << ",\"allocations\":" << stats.allocations
              << ",\"copied\":" << stats.copied << ",\"peak\":" << stats.peak << "}\n";
    for (auto bytes : stats.requests) std::cout << "ALLOC " << bytes << '\n';
#if DXMT_PACKED_IR_GROWTH
    require(stats.allocations < 32 && stats.copied < end.used * 3, "amortized allocation/copy work bound");
#endif
  }
  reset();
  {
    dxmt::PackedVariantList<Tiny, Big> list;
    list.push_back(Tiny{7});
    stats.expected_copy_size = list.test_state().used;
    Big big{}; for (size_t i = 0; i < big.value.size(); ++i) big.value[i] = i * 43;
    list.push_back(big);
    size_t visits = 0;
    list.for_each([&](const auto &v) {
      using T = std::decay_t<decltype(v)>;
      if constexpr (std::is_same_v<T, Big>) require(v.value == big.value, "large append exact payload");
      else require(v.value == 7, "prefix survives large append");
      ++visits;
    });
    require(visits == 2, "large append count");
  }
  reset();
  {
    // Real production InstructionList and instruction payload types. This does
    // not run LLVM/AIR conversion or claim shader binary equivalence.
    using namespace dxmt::dxbc;
    InstructionList list;
    for (size_t i = 0; i < 2048; ++i) {
      stats.expected_copy_size = list.test_state().used;
      if (i % 4 == 0) list.push_back(InstNop{});
      else if (i % 4 == 1) list.push_back(InstEmit{});
      else if (i % 4 == 2) list.push_back(InstCut{});
      else list.push_back(InstSync{InstSync::UAVBoundary::group, bool(i & 4), true});
    }
    size_t index = 0;
    list.for_each([&](const auto &v) {
      using T = std::decay_t<decltype(v)>;
      if constexpr (std::is_same_v<T, InstNop>) require(index % 4 == 0, "actual Nop order");
      else if constexpr (std::is_same_v<T, InstEmit>) require(index % 4 == 1, "actual Emit order");
      else if constexpr (std::is_same_v<T, InstCut>) require(index % 4 == 2, "actual Cut order");
      else if constexpr (std::is_same_v<T, InstSync>)
        require(index % 4 == 3 && v.uav_boundary == InstSync::UAVBoundary::group &&
                v.tgsm_memory_barrier == bool(index & 4) && v.tgsm_execution_barrier, "actual Sync payload/order");
      else require(false, "unexpected actual instruction type");
      ++index;
    });
    require(index == 2048, "actual instruction count");
  }
  reset();
}
static List *failing_list;
static List::TestState failing_state;
static size_t restore_used, failure_attempts, failure_copied, failure_frees, failure_live, failure_request;
static void prepare_failure(List &list, size_t real_used, bool allocator, size_t request = 0) {
  failing_list = &list; failing_state = list.test_state(); restore_used = real_used;
  failure_attempts = stats.attempts + size_t(allocator); failure_copied = stats.copied;
  failure_frees = stats.frees; failure_live = stats.live; failure_request = request;
  stats.fail = true;
  std::set_terminate([] {
    const auto state = failing_list->test_state();
    require(state.data == failing_state.data && state.capacity == failing_state.capacity && state.used == failing_state.used,
            "failure preserves original pointer/capacity/size before termination");
    require(stats.attempts == failure_attempts && stats.copied == failure_copied && stats.frees == failure_frees && stats.live == failure_live,
            "failure performs no copy/free/live mutation");
    if (failure_request) require(stats.last_request == failure_request, "overflow fallback requests exact required capacity");
    failing_list->test_set_size(restore_used); verify(*failing_list);
    std::cout << "PASS failure observed intact old visitor data before termination\n" << std::flush;
    std::_Exit(77);
  });
}
static void failure(const std::string &mode) {
  List list;
  if (mode != "oom-empty")
    for (uint32_t i = 0; i < 128; ++i) append(list, Tiny{uint8_t(i)});
  const auto actual = list.test_state();
  const auto maximum = std::numeric_limits<size_t>::max();
  if (mode == "oom-empty" || mode == "oom-growth") {
    prepare_failure(list, actual.used, true);
    list.push_back(Tiny{7});
  } else if (mode == "overflow-tag") {
    list.test_set_size(maximum - 3); prepare_failure(list, actual.used, false); list.push_back(Tiny{7});
  } else if (mode == "overflow-payload-alignment") {
    list.test_set_size(maximum - 7); prepare_failure(list, actual.used, false); list.push_back(Wide{});
  } else if (mode == "overflow-payload-size") {
    list.test_set_size(maximum - 31); prepare_failure(list, actual.used, false); list.push_back(Medium{});
  } else if (mode == "overflow-trailing-alignment") {
    list.test_set_size(maximum - 5); prepare_failure(list, actual.used, false); list.push_back(Tiny{7});
  } else if (mode == "overflow-growth") {
#if DXMT_PACKED_IR_GROWTH
    const auto capacity = maximum - 2048;
    list.test_set_capacity(capacity); prepare_failure(list, actual.used, true, capacity + 1024);
    list.test_ensure(capacity + 1024);
#else
    prepare_failure(list, actual.used, false); list.test_ensure(maximum);
#endif
  } else if (mode == "growth-largest-safe") {
#if DXMT_PACKED_IR_GROWTH
    const auto capacity = (maximum / 3) * 2;
    list.test_set_capacity(capacity); prepare_failure(list, actual.used, true, maximum);
    list.test_ensure(capacity + 1);
#else
    const auto required = maximum - 1023;
    prepare_failure(list, actual.used, true, required); list.test_ensure(required);
#endif
  } else require(false, "known failure mode");
  require(false, "failure path must terminate");
}
int main(int argc, char **argv) {
  if (argc > 1) failure(argv[1]); else corpus();
}
