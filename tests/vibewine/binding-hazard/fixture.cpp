// Exercise the production BindingSet, Com, and extracted SRV/VB declarations.
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "com/com_pointer.hpp"
#include "dxmt_binding_set.hpp"

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(#condition); } while (0)
using UINT = unsigned;
struct CountedResource : IUnknown {
  bool hazard_free;
  unsigned references = 0, acquisitions = 0, releases = 0;
  explicit CountedResource(bool free) : hazard_free(free) {}
  void AddRefPrivate() { ++references; ++acquisitions; }
  void ReleasePrivate() { CHECK(references != 0); --references; ++releases; }
  bool hazardsFree() const { return hazard_free; }
};
struct D3D11ShaderResourceView : CountedResource { using CountedResource::CountedResource; };
struct D3D11ResourceCommon : CountedResource { using CountedResource::CountedResource; };
#include "production-bindings.hpp"
using namespace dxmt;

template<class Set> std::vector<size_t> bound_slots(const Set &set) {
  std::vector<size_t> slots;
  for (auto [slot, value] : set) { CHECK(value.RawPointer); slots.push_back(slot); }
  return slots;
}
template<class Set> std::vector<size_t> hazard_slots(const Set &set) {
  std::vector<size_t> slots;
  for (auto it = set.hazard_begin(); it != set.hazard_end(); it++) {
    auto [slot, value] = *it; CHECK(value.RawPointer); slots.push_back(slot);
  }
  return slots;
}
template<class E> struct TestTraits;
template<> struct TestTraits<SRV_B> {
  using Resource = D3D11ShaderResourceView;
  using Set = SRVBindingSet;
  static auto &reference(SRV_B &entry) { return entry.SRV; }
};
template<> struct TestTraits<VERTEX_BUFFER_B> {
  using Resource = D3D11ResourceCommon;
  using Set = BindingSet<VERTEX_BUFFER_B, 32>;
  static auto &reference(VERTEX_BUFFER_B &entry) { return entry.Buffer; }
};
template<class E> E &bind_resource(typename TestTraits<E>::Set &set, size_t slot,
                                  typename TestTraits<E>::Resource &resource, bool &replaced) {
  // Matches production callers: redundancy sees RawPointer before retained
  // references (and VB metadata) are initialized after an accepted replacement.
  auto &entry = set.bind(slot, {&resource}, replaced, !resource.hazardsFree());
  if (replaced) {
    TestTraits<E>::reference(entry) = &resource;
    if constexpr (std::is_same_v<E, VERTEX_BUFFER_B>) { entry.Stride = 16; entry.Offset = 32; }
  }
  return entry;
}
template<class E> void occupied_transition(bool old_hazard, bool new_hazard, size_t slot = 0) {
  typename TestTraits<E>::Resource old_resource(!old_hazard), next_resource(!new_hazard);
  {
    typename TestTraits<E>::Set set;
    bool replaced = false;
    auto &old_entry = bind_resource<E>(set, slot, old_resource, replaced);
    CHECK(replaced && old_resource.references == 1);
    CHECK(hazard_slots(set) == (old_hazard ? std::vector<size_t>{slot} : std::vector<size_t>{}));
    set.clear_dirty(); replaced = false;
    auto &entry = bind_resource<E>(set, slot, next_resource, replaced);
    CHECK(&entry == &old_entry && replaced && set.test_bound(slot) && set.test_dirty(slot));
    CHECK(entry.RawPointer == &next_resource && TestTraits<E>::reference(entry).ptr() == &next_resource);
    CHECK(old_resource.references == 0 && old_resource.releases == 1);
    CHECK(next_resource.references == 1 && next_resource.acquisitions == 1);
    CHECK(bound_slots(set) == std::vector<size_t>{slot});
    CHECK(hazard_slots(set) == (new_hazard ? std::vector<size_t>{slot} : std::vector<size_t>{}));
  }
  CHECK(old_resource.references == 0 && next_resource.references == 0);
  CHECK(next_resource.releases == 1);
}
template<class E> void redundant_contract() {
  typename TestTraits<E>::Resource resource(false);
  typename TestTraits<E>::Set set;
  bool replaced = false;
  auto &stored = bind_resource<E>(set, 0, resource, replaced);
  set.clear_dirty(); replaced = false;
  // A contradictory hazard argument on an otherwise redundant bind remains a
  // no-op. Production callers derive this argument from resource creation flags.
  E incoming{}; incoming.RawPointer = &resource;
  TestTraits<E>::reference(incoming) = &resource;
  auto &same = set.bind(0, std::move(incoming), replaced, false);
  CHECK(&same == &stored && !replaced && !set.any_dirty());
  CHECK(hazard_slots(set) == std::vector<size_t>{0});
  CHECK(TestTraits<E>::reference(incoming).ptr() == &resource);
  CHECK(resource.references == 2 && resource.releases == 0);
  if constexpr (std::is_same_v<E, VERTEX_BUFFER_B>) CHECK(same.Stride == 16 && same.Offset == 32);
  // The API only sets replacement on acceptance; it does not clear caller state.
  replaced = true;
  set.bind(0, std::move(incoming), replaced, false);
  CHECK(replaced && !set.any_dirty() && resource.releases == 0);
}
static void sparse_and_masks() {
  std::array<D3D11ShaderResourceView, 5> resources = {
      D3D11ShaderResourceView(true), D3D11ShaderResourceView(false), D3D11ShaderResourceView(false),
      D3D11ShaderResourceView(true), D3D11ShaderResourceView(false)};
  D3D11ShaderResourceView hazard_resource(false), free_resource(true);
  SRVBindingSet set;
  CHECK(!set.any_bound() && !set.any_dirty() && hazard_slots(set).empty());
  const std::array<size_t, 5> slots = {0, 7, 63, 64, 127};
  for (size_t i = 0; i < slots.size(); ++i) {
    bool replaced = false; bind_resource<SRV_B>(set, slots[i], resources[i], replaced); CHECK(replaced);
  }
  CHECK(bound_slots(set) == std::vector<size_t>(slots.begin(), slots.end()));
  CHECK(hazard_slots(set) == (std::vector<size_t>{7, 63, 127}));
  CHECK(set.any_bound_masked(uint64_t(1) << 63));
  CHECK(!set.any_bound_masked(uint64_t(1) << 62));
  CHECK(set.all_bound_masked(uint32_t(1) | (uint32_t(1) << 7)));
  CHECK(set.any_dirty_masked(uint16_t(1 << 7)));
  CHECK(set.any_dirty_masked(uint64_t(1) << 63));
  CHECK(set.any_dirty_masked(uint64_t(1) << 63, uint64_t(0)));
  set.clear_dirty(127);
  CHECK(!set.any_dirty_masked(uint64_t(1) << 63, uint64_t(0)));
  CHECK(set.any_dirty_masked(uint64_t(1), uint64_t(0)));
  set.clear_dirty_mask(uint32_t(1) | (uint32_t(1) << 7));
  CHECK(!set.any_dirty_masked(uint16_t(0xff)));
  set.clear_dirty(); CHECK(!set.any_dirty());
  set.set_dirty(127); CHECK(set.test_dirty(127));
  // Replace across qword boundaries in both directions, then iterate while
  // unbinding as ResolveSRVHazard does. No duplicate or omitted next slot.
  bool replaced = false; bind_resource<SRV_B>(set, 64, hazard_resource, replaced);
  replaced = false; bind_resource<SRV_B>(set, 63, free_resource, replaced);
  CHECK(hazard_slots(set) == (std::vector<size_t>{7, 64, 127}));
  for (auto it = set.hazard_begin(); it != set.hazard_end(); it++) CHECK(set.unbind((*it).first));
  CHECK(hazard_slots(set).empty());
  CHECK(bound_slots(set) == (std::vector<size_t>{0, 63}));
  set.clear_dirty(); CHECK(!set.unbind(64) && !set.any_dirty());
  CHECK(set.unbind(63) && set.test_dirty(63) && !set.at(63).SRV);
  replaced = false; bind_resource<SRV_B>(set, 63, hazard_resource, replaced);
  CHECK(replaced && hazard_slots(set) == std::vector<size_t>{63});
}
static void move_sets() {
  D3D11ShaderResourceView first(false), second(true), old(false);
  {
    SRVBindingSet source;
    bool replaced = false; bind_resource<SRV_B>(source, 127, first, replaced);
    replaced = false; bind_resource<SRV_B>(source, 64, second, replaced);
    source.clear_dirty();
    SRVBindingSet moved(std::move(source));
    CHECK(bound_slots(moved) == (std::vector<size_t>{64, 127}));
    CHECK(hazard_slots(moved) == std::vector<size_t>{127});
    CHECK(moved.test_dirty(64) && moved.test_dirty(127) && !moved.test_dirty(0));
    CHECK(first.references == 1 && second.references == 1);
    SRVBindingSet assigned;
    replaced = false; bind_resource<SRV_B>(assigned, 0, old, replaced);
    assigned = std::move(moved);
    CHECK(old.references == 0 && old.releases == 1);
    CHECK(bound_slots(assigned) == (std::vector<size_t>{64, 127}));
    CHECK(hazard_slots(assigned) == std::vector<size_t>{127});
    CHECK(assigned.test_dirty(64) && assigned.test_dirty(127) && !assigned.test_dirty(0));
    CHECK(first.references == 1 && first.acquisitions == 1 && first.releases == 0);
    CHECK(second.references == 1 && second.acquisitions == 1 && second.releases == 0);
  }
  CHECK(first.references == 0 && second.references == 0 && old.references == 0);
  CHECK(first.releases == 1 && second.releases == 1);
}
static void default_hazard() {
  D3D11ShaderResourceView resource(true);
  SRVBindingSet set;
  bool replaced = false;
  auto &entry = set.bind(127, {&resource}, replaced);
  entry.SRV = &resource;
  CHECK(replaced && hazard_slots(set) == std::vector<size_t>{127});
}
static void incoming_reference_move() {
  D3D11ShaderResourceView old_resource(false), next_resource(false);
  {
    SRVBindingSet set;
    bool replaced = false; bind_resource<SRV_B>(set, 0, old_resource, replaced);
    SRV_B incoming{&next_resource, &next_resource};
    CHECK(next_resource.references == 1);
    set.clear_dirty(); replaced = false;
    auto &entry = set.bind(0, std::move(incoming), replaced, true);
    CHECK(replaced && set.test_dirty(0) && !incoming.SRV);
    CHECK(entry.RawPointer == &next_resource && entry.SRV.ptr() == &next_resource);
    CHECK(old_resource.references == 0 && old_resource.releases == 1);
    CHECK(next_resource.references == 1 && next_resource.acquisitions == 1 && next_resource.releases == 0);
  }
  CHECK(next_resource.references == 0 && next_resource.releases == 1);
}
int main(int argc, char **argv) {
  if (argc != 2) return 3;
  const std::string test = argv[1];
  try {
    if (test == "srv-false-true") occupied_transition<SRV_B>(false, true);
    else if (test == "srv-true-false") occupied_transition<SRV_B>(true, false);
    else if (test == "vb-false-true") occupied_transition<VERTEX_BUFFER_B>(false, true);
    else if (test == "vb-true-false") occupied_transition<VERTEX_BUFFER_B>(true, false);
    else if (test == "srv-slot127-false-true") occupied_transition<SRV_B>(false, true, 127);
    else if (test == "srv-slot127-true-false") occupied_transition<SRV_B>(true, false, 127);
    else if (test == "srv-false-false") occupied_transition<SRV_B>(false, false);
    else if (test == "srv-true-true") occupied_transition<SRV_B>(true, true);
    else if (test == "vb-false-false") occupied_transition<VERTEX_BUFFER_B>(false, false);
    else if (test == "vb-true-true") occupied_transition<VERTEX_BUFFER_B>(true, true);
    else if (test == "srv-redundant") redundant_contract<SRV_B>();
    else if (test == "vb-redundant") redundant_contract<VERTEX_BUFFER_B>();
    else if (test == "sparse-masks-unbind") sparse_and_masks();
    else if (test == "move") move_sets();
    else if (test == "default-hazard") default_hazard();
    else if (test == "incoming-reference-move") incoming_reference_move();
    else return 3;
    std::cout << "PASS " << test << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL " << test << ": " << error.what() << '\n';
    return 2;
  }
}
