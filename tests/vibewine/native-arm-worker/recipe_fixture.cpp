#include "airconv_worker_recipe.hpp"
#include <cassert>
#include <iostream>
#include <random>
using namespace dxmt::worker;
static void set32(Bytes &b, size_t at, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) b[at + i] = uint8_t(value >> (8 * i));
}
int main() {
  OwnedRecipe source;
  source.function = "main";
  source.order = {9, 8, 7, 6, 5, 4, 3, 2, 1};
  source.rootSignatureBytes = {0, 1, 255, 31};
  source.rootSignature2Bytes = {99, 3};
  source.tessellator.max_potential_tess_factor = 64;
  source.geometry.strip_topology = true;
  source.passThrough.Data = {255, 3, 29, 2};
  source.passThrough.RasterizationDisabled = true;
  source.common.metal_version = SM50_SHADER_METAL_320;
  source.common.flags = static_cast<SM50_SHADER_FLAG>(3);
  source.pixel.sample_mask = 0xaabbccdd;
  source.pixel.dual_source_blending = true;
  source.pixel.disable_depth_output = true;
  source.pixel.unorm_output_reg_mask = 0xff;
  for (unsigned i = 0; i < 8; ++i) source.pixel.pixel_formats[i] = i + 70;
  source.inputLayout.slot_mask = 0x80000001;
  source.inputLayout.index_buffer_format = SM50_INDEX_BUFFER_FORMAT_UINT32;
  SM50_IA_INPUT_ELEMENT ia{};
  ia.reg = 31; ia.slot = 31; ia.aligned_byte_offset = 0xfedcba98; ia.format = 42;
  ia.step_function = 1; ia.step_rate = 0x7fffffff;
  source.inputElements.push_back(ia);
  source.streamOutput.num_output_slots = 0;
  for (unsigned i = 0; i < 4; ++i) source.streamOutput.strides[i] = (i + 1) * 16;
  source.streamOutputElements = {{31, 3, 3, 64}, {UINT32_MAX, 0, 0, 8}};
  Bytes wire;
  std::string why;
  assert(encodeRecipe(source.args(), source.function.c_str(), wire, why));
  assert(wire[0] == 'W' && wire[1] == 'C' && wire[2] == 'R' && wire[3] == '1');
  OwnedRecipe decoded;
  assert(decodeRecipe(wire, decoded, why));
  assert(decoded.order == source.order && decoded.function == "main");
  assert(decoded.rootSignatureBytes == source.rootSignatureBytes);
  assert(decoded.rootSignature2Bytes == source.rootSignature2Bytes);
  assert(decoded.inputElements[0].step_rate == 0x7fffffff);
  assert(decoded.inputElements[0].aligned_byte_offset == 0xfedcba98);
  assert(decoded.passThrough.Data.ViewportArrayIndexReg == 29);
  assert(decoded.pixel.pixel_formats[7] == 77);
  assert(decoded.streamOutputElements[1].reg_id == UINT32_MAX);
  auto copied = decoded;
  auto moved = std::move(copied);
  Bytes encodedAgain;
  assert(encodeRecipe(moved.args(), moved.function.c_str(), encodedAgain, why));
  assert(encodedAgain == wire);
  for (size_t n = 0; n < wire.size(); ++n) {
    Bytes prefix(wire.begin(), wire.begin() + n);
    assert(!decodeRecipe(prefix, decoded, why));
  }
  Bytes trailing = wire; trailing.push_back(0);
  assert(!decodeRecipe(trailing, decoded, why));
  Bytes unknown = wire; set32(unknown, 20, 10);
  assert(!decodeRecipe(unknown, decoded, why));
  Bytes duplicate = wire; set32(duplicate, 30, 9);
  assert(!decodeRecipe(duplicate, decoded, why));
  Bytes embeddedNul = wire; embeddedNul[16] = 0;
  assert(!decodeRecipe(embeddedNul, decoded, why));
  Bytes invalidBoolean = wire; set32(invalidBoolean, 54, 2);
  assert(!decodeRecipe(invalidBoolean, decoded, why));
  Bytes hugeRoot = wire; set32(hugeRoot, 24, MaxRecipeRootSignatureBytes + 1);
  assert(!decodeRecipe(hugeRoot, decoded, why));
  assert(!encodeRecipe(nullptr, nullptr, encodedAgain, why));
  assert(!encodeRecipe(nullptr, "", encodedAgain, why));
  const std::string longestName(MaxRecipeFunctionBytes, 'f');
  assert(encodeRecipe(nullptr, longestName.c_str(), encodedAgain, why));
  assert(decodeRecipe(encodedAgain, decoded, why));
  assert(!encodeRecipe(nullptr, (longestName + 'f').c_str(), encodedAgain, why));
  source.common.next = &source.common;
  source.common.type = SM50_SHADER_COMMON;
  assert(!encodeRecipe(reinterpret_cast<SM50_SHADER_COMPILATION_ARGUMENT_DATA *>(&source.common), "main", encodedAgain, why));
  SM50_SHADER_COMMON_DATA common2 = source.common;
  source.common.next = &common2; common2.next = nullptr;
  assert(!encodeRecipe(reinterpret_cast<SM50_SHADER_COMPILATION_ARGUMENT_DATA *>(&source.common), "main", encodedAgain, why));
  source.args(); source.inputLayout.num_elements = MaxRecipeIAElements + 1;
  assert(!encodeRecipe(source.node(4), "main", encodedAgain, why));
  source.args(); source.streamOutput.num_elements = MaxRecipeSOElements + 1;
  assert(!encodeRecipe(source.node(1), "main", encodedAgain, why));
  source.args(); source.rootSignature.bytecode_length = MaxRecipeRootSignatureBytes + 1;
  assert(!encodeRecipe(source.node(8), "main", encodedAgain, why));
  source.args(); source.rootSignature.bytecode = nullptr;
  assert(!encodeRecipe(source.node(8), "main", encodedAgain, why));
  source.args(); source.inputLayout.elements = nullptr;
  assert(!encodeRecipe(source.node(4), "main", encodedAgain, why));
  // Maximum accepted root signatures and array lengths round trip together.
  source.rootSignatureBytes.resize(MaxRecipeRootSignatureBytes, 0xab);
  source.rootSignature2Bytes.resize(MaxRecipeRootSignatureBytes, 0xcd);
  source.inputElements.resize(MaxRecipeIAElements, ia);
  source.streamOutputElements.resize(MaxRecipeSOElements, {31, 3, 3, 64});
  assert(encodeRecipe(source.args(), source.function.c_str(), encodedAgain, why));
  assert(decodeRecipe(encodedAgain, decoded, why));
  assert(decoded.rootSignature2Bytes.size() == MaxRecipeRootSignatureBytes);
  // Bounded malformed-input smoke under ASan/UBSan.
  std::mt19937 rng(12345);
  for (unsigned trial = 0; trial < 20000; ++trial) {
    Bytes random(rng() % 1024);
    for (auto &b : random) b = uint8_t(rng());
    decodeRecipe(random, decoded, why);
    auto mutation = wire;
    for (unsigned edit = 0; edit < 1 + rng() % 5; ++edit) mutation[rng() % mutation.size()] = uint8_t(rng());
    if (decodeRecipe(mutation, decoded, why)) {
      assert(encodeRecipe(decoded.args(), decoded.function.c_str(), encodedAgain, why));
      assert(encodedAgain == mutation);
    }
  }
  std::cout << "recipe codec: all 9 nodes, bounds, copy/move, truncations, 20000 malformed trials passed\n";
}
