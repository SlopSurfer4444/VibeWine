// Native key/cache fixture: production key declarations and hash/equality bodies.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <new>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include "util_hash.hpp"
#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(#condition); } while (0)
using UINT = unsigned;
#include "production-enums.hpp"
struct Shader {};
using ManagedShader = Shader *;
struct InputLayout {};
using ManagedInputLayout = InputLayout *;
// These blend declarations only make the unchanged production comparator parse.
// SO tests use the same blend pointer (or nullptr) and do not enter this branch.
struct BlendTarget {
  uint32_t RenderTargetWriteMask, BlendEnable, LogicOpEnable, BlendOp,
      BlendOpAlpha, SrcBlend, SrcBlendAlpha, DestBlend, DestBlendAlpha, LogicOp;
};
struct D3D11_BLEND_DESC1 {
  uint32_t IndependentBlendEnable, AlphaToCoverageEnable;
  BlendTarget RenderTarget[8];
};
struct IMTLD3D11BlendState {
  void GetDesc1(D3D11_BLEND_DESC1 *) const { throw std::runtime_error("Unexpected blend comparison"); }
};
#include "production-so-desc.hpp"
struct IMTLD3D11StreamOutputLayout { MTL_STREAM_OUTPUT_DESC desc; };
#include "production-pso-key.hpp"

using Key = MTL_GRAPHICS_PIPELINE_DESC;
using Layout = IMTLD3D11StreamOutputLayout;
static_assert(std::is_trivially_copyable_v<Key>);
// The production SO descriptor comparator hashes element bytes, so establish
// that the four semantic uint32 fields have no padding on this tested target.
static_assert(sizeof(MTL_SHADER_STREAM_OUTPUT_ELEMENT_DESC) == 4 * sizeof(uint32_t));
static_assert(offsetof(MTL_SHADER_STREAM_OUTPUT_ELEMENT_DESC, Register) == 0);
static_assert(offsetof(MTL_SHADER_STREAM_OUTPUT_ELEMENT_DESC, Component) == 4);
static_assert(offsetof(MTL_SHADER_STREAM_OUTPUT_ELEMENT_DESC, OutputSlot) == 8);
static_assert(offsetof(MTL_SHADER_STREAM_OUTPUT_ELEMENT_DESC, Offset) == 12);

struct LayoutCache {
  // Same production descriptor key/hash/equality and stable unique_ptr ownership
  // as StateObjectCache. Native fixture substitutes COM creation, not the keys.
  std::unordered_map<MTL_STREAM_OUTPUT_DESC, std::unique_ptr<Layout>> entries;
  Layout *intern(const MTL_STREAM_OUTPUT_DESC &desc) {
    if (auto it = entries.find(desc); it != entries.end()) return it->second.get();
    auto value = std::make_unique<Layout>(); value->desc = desc;
    auto *pointer = value.get(); entries.emplace(desc, std::move(value)); return pointer;
  }
};
static MTL_STREAM_OUTPUT_DESC base_layout() {
  return {0, {32, 64, 96, 128}, {{1, 0, 0, 0}, {2, 1, 1, 16}}};
}
static std::vector<MTL_STREAM_OUTPUT_DESC> differing_layouts() {
  const auto base = base_layout();
  std::vector<MTL_STREAM_OUTPUT_DESC> cases;
  auto add = [&](auto mutate) { auto next = base; mutate(next); cases.push_back(std::move(next)); };
  add([](auto &d) { d.RasterizedStream = 1; });
  for (unsigned slot = 0; slot < 4; ++slot) add([slot](auto &d) { d.Strides[slot] += 4; });
  add([](auto &d) { ++d.Elements[0].Register; });
  add([](auto &d) { ++d.Elements[0].Component; });
  add([](auto &d) { ++d.Elements[0].OutputSlot; });
  add([](auto &d) { d.Elements[0].Offset += 4; });
  add([](auto &d) { d.Elements.push_back({3, 2, 2, 8}); });
  add([](auto &d) { d.Elements.pop_back(); });
  add([](auto &d) { std::swap(d.Elements[0], d.Elements[1]); });
  return cases;
}
static Key key(Layout *layout = nullptr) {
  Key desc{};
  desc.SOLayout = layout;
  desc.NumColorAttachments = 1;
  desc.ColorAttachmentFormats[0] = WMTPixelFormatBGRA8Unorm;
  desc.SampleCount = 1;
  desc.RasterizationEnabled = true;
  desc.SampleMask = ~0u;
  desc.GSPassthrough = ~0u;
  return desc;
}
static void layout_canonicalization() {
  LayoutCache layouts;
  const auto base = base_layout();
  auto *a = layouts.intern(base);
  auto equivalent = base;
  equivalent.Elements.reserve(128); // capacity/address do not affect semantics
  CHECK(std::equal_to<MTL_STREAM_OUTPUT_DESC>{}(base, equivalent));
  CHECK(std::hash<MTL_STREAM_OUTPUT_DESC>{}(base) == std::hash<MTL_STREAM_OUTPUT_DESC>{}(equivalent));
  CHECK(layouts.intern(equivalent) == a);
  for (const auto &desc : differing_layouts()) {
    CHECK(!std::equal_to<MTL_STREAM_OUTPUT_DESC>{}(base, desc));
    CHECK(layouts.intern(desc) != a);
  }
  CHECK(layouts.entries.size() == 13);
}
static void cache_separation() {
  LayoutCache layouts;
  auto *a = layouts.intern(base_layout());
  auto changed = base_layout(); changed.Strides[0] += 4; changed.Elements[0].Component = 2;
  auto *b = layouts.intern(changed);
  CHECK(a != b);
  std::unordered_map<Key, unsigned> cache;
  auto lookup = [&](Key desc) { auto [it, inserted] = cache.try_emplace(desc, unsigned(cache.size() + 1)); return it->second; };
  const auto first = lookup(key(a));
  const auto second = lookup(key(b));
  const auto again = lookup(key(a));
  CHECK(first != second && first == again && cache.size() == 2);
  CHECK(!std::equal_to<Key>{}(key(a), key(b)));
}
static void every_semantic_field() {
  LayoutCache layouts;
  auto *a = layouts.intern(base_layout());
  std::unordered_map<Key, unsigned> cache;
  cache.emplace(key(a), 0);
  std::set<size_t> observed_hashes{std::hash<Key>{}(key(a))};
  unsigned index = 1;
  for (const auto &desc : differing_layouts()) {
    auto *b = layouts.intern(desc);
    CHECK(!std::equal_to<Key>{}(key(a), key(b)));
    CHECK(cache.emplace(key(b), index++).second);
    observed_hashes.insert(std::hash<Key>{}(key(b)));
  }
  CHECK(cache.size() == 13);
  // Require SO identity to influence the production hash without assuming
  // collision freedom for every pair of unequal descriptors.
  CHECK(observed_hashes.size() > 1);
}
static void equivalent_and_default() {
  LayoutCache layouts;
  auto *a = layouts.intern(base_layout());
  auto copy = base_layout(); copy.Elements.reserve(64);
  auto first = key(a), equivalent = key(layouts.intern(copy));
  CHECK(std::equal_to<Key>{}(first, equivalent));
  CHECK(std::hash<Key>{}(first) == std::hash<Key>{}(equivalent));
  std::unordered_map<Key, unsigned> cache;
  CHECK(cache.emplace(first, 1).second);
  CHECK(!cache.emplace(equivalent, 2).second && cache.size() == 1);
  Key empty{}, empty_copy{};
  CHECK(std::equal_to<Key>{}(empty, empty_copy));
  CHECK(std::hash<Key>{}(empty) == std::hash<Key>{}(empty_copy));
  auto no_so = key(), with_so = key(a);
  CHECK(!std::equal_to<Key>{}(no_so, with_so));
  CHECK(cache.emplace(no_so, 3).second && cache.size() == 2);
}
static void padding_and_inactive_fields() {
  alignas(Key) std::array<unsigned char, sizeof(Key)> a_bytes, b_bytes;
  a_bytes.fill(0x25); b_bytes.fill(0xd3);
  auto *a = ::new (a_bytes.data()) Key;
  auto *b = ::new (b_bytes.data()) Key;
  // Assign each semantic member separately: do not copy object padding.
  for (auto *desc : {a, b}) {
    desc->VertexShader = nullptr; desc->HullShader = nullptr; desc->DomainShader = nullptr;
    desc->GeometryShader = nullptr; desc->PixelShader = nullptr;
    desc->BlendState = nullptr; desc->InputLayout = nullptr; desc->SOLayout = nullptr;
    desc->NumColorAttachments = 1;
    for (auto &format : desc->ColorAttachmentFormats) format = WMTPixelFormatBGRA8Unorm;
    desc->DepthStencilFormat = WMTPixelFormatInvalid; desc->TopologyClass = WMTPrimitiveTopologyClassTriangle;
    desc->RasterizationEnabled = true; desc->SampleCount = 1; desc->GSStripTopology = false;
    desc->IndexBufferFormat = SM50_INDEX_BUFFER_FORMAT_NONE; desc->SampleMask = ~0u; desc->GSPassthrough = ~0u;
  }
  CHECK(std::memcmp(a, b, sizeof(Key)) != 0); // only representation padding differs
  CHECK(std::equal_to<Key>{}(*a, *b));
  CHECK(std::hash<Key>{}(*a) == std::hash<Key>{}(*b));
  for (unsigned i = 1; i < 8; ++i) b->ColorAttachmentFormats[i] = WMTPixelFormatRGBA8Unorm;
  CHECK(std::memcmp(a, b, sizeof(Key)) != 0);
  CHECK(std::equal_to<Key>{}(*a, *b));
  CHECK(std::hash<Key>{}(*a) == std::hash<Key>{}(*b));
  a->~Key(); b->~Key();
}
int main(int argc, char **argv) {
  if (argc != 2) return 3;
  const std::string test = argv[1];
  try {
    if (test == "layout-canonicalization") layout_canonicalization();
    else if (test == "cache-separation") cache_separation();
    else if (test == "every-semantic-field") every_semantic_field();
    else if (test == "equivalent-default") equivalent_and_default();
    else if (test == "padding-inactive") padding_and_inactive_fields();
    else return 3;
    std::cout << "PASS " << test << '\n'; return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL " << test << ": " << error.what() << '\n'; return 2;
  }
}
