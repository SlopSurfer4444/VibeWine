#pragma once

#include "airconv_public.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace dxmt::worker {

using Bytes = std::vector<uint8_t>;
inline constexpr uint32_t MaxRecipeNodes = 32;
inline constexpr uint32_t MaxRecipeFunctionBytes = 4096;
inline constexpr uint32_t MaxRecipeRootSignatureBytes = 1024 * 1024;
inline constexpr uint32_t MaxRecipeIAElements = 32;
inline constexpr uint32_t MaxRecipeSOElements = 512;
inline constexpr size_t MaxRecipeBytes = 2 * MaxRecipeRootSignatureBytes + 128 * 1024;

// Nodes live in this object; args() always repairs internal links and pointees.
// In particular, moving/copying OwnedRecipe is safe provided args() is called
// again before passing the chain to SM50Compile. Do not retain args() across a move.
struct OwnedRecipe {
  std::string function;
  std::vector<uint32_t> order;
  SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA streamOutput{};
  SM50_SHADER_COMMON_DATA common{};
  SM50_SHADER_PSO_PIXEL_SHADER_DATA pixel{};
  SM50_SHADER_IA_INPUT_LAYOUT_DATA inputLayout{};
  SM50_SHADER_GS_PASS_THROUGH_DATA passThrough{};
  SM50_SHADER_PSO_GEOMETRY_SHADER_DATA geometry{};
  SM50_SHADER_PSO_TESSELLATOR_DATA tessellator{};
  SM50_SHADER_ROOT_SIGNATURE_DATA rootSignature{};
  SM50_SHADER_ROOT_SIGNATURE_DATA rootSignature2{};
  std::vector<SM50_STREAM_OUTPUT_ELEMENT> streamOutputElements;
  std::vector<SM50_IA_INPUT_ELEMENT> inputElements;
  Bytes rootSignatureBytes;
  Bytes rootSignature2Bytes;

  SM50_SHADER_COMPILATION_ARGUMENT_DATA *node(uint32_t type) {
    switch (type) {
    case 1: return reinterpret_cast<SM50_SHADER_COMPILATION_ARGUMENT_DATA *>(&streamOutput);
    case 2: return reinterpret_cast<SM50_SHADER_COMPILATION_ARGUMENT_DATA *>(&common);
    case 3: return reinterpret_cast<SM50_SHADER_COMPILATION_ARGUMENT_DATA *>(&pixel);
    case 4: return reinterpret_cast<SM50_SHADER_COMPILATION_ARGUMENT_DATA *>(&inputLayout);
    case 5: return reinterpret_cast<SM50_SHADER_COMPILATION_ARGUMENT_DATA *>(&passThrough);
    case 6: return reinterpret_cast<SM50_SHADER_COMPILATION_ARGUMENT_DATA *>(&geometry);
    case 7: return reinterpret_cast<SM50_SHADER_COMPILATION_ARGUMENT_DATA *>(&tessellator);
    case 8: return reinterpret_cast<SM50_SHADER_COMPILATION_ARGUMENT_DATA *>(&rootSignature);
    case 9: return reinterpret_cast<SM50_SHADER_COMPILATION_ARGUMENT_DATA *>(&rootSignature2);
    default: return nullptr;
    }
  }

  SM50_SHADER_COMPILATION_ARGUMENT_DATA *args() {
    streamOutput.num_elements = static_cast<uint32_t>(streamOutputElements.size());
    streamOutput.elements = streamOutputElements.empty() ? nullptr : streamOutputElements.data();
    inputLayout.num_elements = static_cast<uint32_t>(inputElements.size());
    inputLayout.elements = inputElements.empty() ? nullptr : inputElements.data();
    rootSignature.bytecode_length = rootSignatureBytes.size();
    rootSignature.bytecode = rootSignatureBytes.empty() ? nullptr : rootSignatureBytes.data();
    rootSignature2.bytecode_length = rootSignature2Bytes.size();
    rootSignature2.bytecode = rootSignature2Bytes.empty() ? nullptr : rootSignature2Bytes.data();
    for (size_t i = 0; i < order.size(); ++i) {
      auto *p = node(order[i]);
      if (!p) return nullptr;
      p->type = static_cast<SM50_SHADER_COMPILATION_ARGUMENT_TYPE>(order[i]);
      p->next = i + 1 < order.size() ? node(order[i + 1]) : nullptr;
    }
    return order.empty() ? nullptr : node(order.front());
  }
};

namespace recipe_detail {
inline bool fail(std::string &why, const char *message) { why = message; return false; }
inline void u32(Bytes &out, uint32_t v) {
  for (unsigned shift = 0; shift < 32; shift += 8) out.push_back(static_cast<uint8_t>(v >> shift));
}
struct Reader {
  const Bytes &bytes;
  size_t offset = 0;
  bool u32(uint32_t &v) {
    if (bytes.size() - offset < 4) return false;
    v = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) v |= uint32_t(bytes[offset++]) << shift;
    return true;
  }
  bool boolean(bool &v) {
    uint32_t raw;
    if (!u32(raw) || raw > 1) return false;
    v = raw != 0;
    return true;
  }
  bool raw(Bytes &out, uint32_t count) {
    if (count > bytes.size() - offset) return false;
    out.assign(bytes.begin() + offset, bytes.begin() + offset + count);
    offset += count;
    return true;
  }
};
} // namespace recipe_detail

// Wire v1: magic, version, function byte count, node count, function bytes,
// then node type followed by its explicit u32 LE fields. Arrays have explicit
// counts; root signatures are opaque bytecode. No native structs/pointers/padding
// or implementation-defined bitfield layout enter the wire representation.
inline bool encodeRecipe(SM50_SHADER_COMPILATION_ARGUMENT_DATA *args, const char *function,
                         Bytes &out, std::string &why) {
  using namespace recipe_detail;
  why.clear();
  out.clear();
  if (!function) return fail(why, "null function name");
  size_t functionSize = 0;
  while (functionSize <= MaxRecipeFunctionBytes && function[functionSize]) ++functionSize;
  if (!functionSize || functionSize > MaxRecipeFunctionBytes) return fail(why, "invalid function name length");
  std::array<SM50_SHADER_COMPILATION_ARGUMENT_DATA *, MaxRecipeNodes> nodes{};
  uint32_t count = 0, seen = 0;
  for (auto *p = args; p; p = static_cast<SM50_SHADER_COMPILATION_ARGUMENT_DATA *>(p->next)) {
    if (count == MaxRecipeNodes) return fail(why, "too many recipe nodes");
    for (uint32_t i = 0; i < count; ++i)
      if (nodes[i] == p) return fail(why, "cyclic recipe chain");
    uint32_t type = static_cast<uint32_t>(p->type);
    if (type < 1 || type > 9) return fail(why, "unsupported recipe node type");
    if (seen & (1u << type)) return fail(why, "duplicate recipe node type");
    seen |= 1u << type;
    nodes[count++] = p;
  }
  Bytes wire;
  u32(wire, 0x31524357); // WCR1
  u32(wire, 1);
  u32(wire, static_cast<uint32_t>(functionSize));
  u32(wire, count);
  wire.insert(wire.end(), function, function + functionSize);
  for (uint32_t i = 0; i < count; ++i) {
    auto *p = nodes[i];
    u32(wire, static_cast<uint32_t>(p->type));
    switch (p->type) {
    case SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT: {
      const auto &s = *reinterpret_cast<SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA *>(p);
      if (s.num_output_slots > 4 || s.num_elements > MaxRecipeSOElements || (s.num_elements && !s.elements))
        return fail(why, "invalid stream output bounds");
      u32(wire, s.num_output_slots); u32(wire, s.num_elements);
      for (uint32_t stride : s.strides) u32(wire, stride);
      for (uint32_t j = 0; j < s.num_elements; ++j) {
        const auto &e = s.elements[j];
        if ((e.reg_id != UINT32_MAX && e.reg_id >= 32) || e.component > 3 || e.output_slot >= 4)
          return fail(why, "invalid stream output element");
        u32(wire, e.reg_id); u32(wire, e.component); u32(wire, e.output_slot); u32(wire, e.offset);
      }
      break;
    }
    case SM50_SHADER_COMMON: {
      const auto &s = *reinterpret_cast<SM50_SHADER_COMMON_DATA *>(p);
      uint32_t flags;
      static_assert(sizeof(flags) == sizeof(s.flags));
      std::memcpy(&flags, &s.flags, sizeof(flags));
      if ((s.metal_version != SM50_SHADER_METAL_310 && s.metal_version != SM50_SHADER_METAL_320) ||
          (flags & ~3u)) return fail(why, "unsupported common shader settings");
      u32(wire, s.metal_version); u32(wire, flags); break;
    }
    case SM50_SHADER_PSO_PIXEL_SHADER: {
      const auto &s = *reinterpret_cast<SM50_SHADER_PSO_PIXEL_SHADER_DATA *>(p);
      u32(wire, s.sample_mask); u32(wire, s.dual_source_blending); u32(wire, s.disable_depth_output);
      u32(wire, s.unorm_output_reg_mask);
      for (uint32_t format : s.pixel_formats) u32(wire, format);
      break;
    }
    case SM50_SHADER_IA_INPUT_LAYOUT: {
      const auto &s = *reinterpret_cast<SM50_SHADER_IA_INPUT_LAYOUT_DATA *>(p);
      uint32_t indexFormat;
      static_assert(sizeof(indexFormat) == sizeof(s.index_buffer_format));
      std::memcpy(&indexFormat, &s.index_buffer_format, sizeof(indexFormat));
      if (indexFormat > 2 || s.num_elements > MaxRecipeIAElements ||
          (s.num_elements && !s.elements)) return fail(why, "invalid input layout bounds");
      u32(wire, indexFormat); u32(wire, s.slot_mask); u32(wire, s.num_elements);
      for (uint32_t j = 0; j < s.num_elements; ++j) {
        const auto &e = s.elements[j];
        if (e.reg >= 32 || e.slot >= 32) return fail(why, "invalid input layout element");
        u32(wire, e.reg); u32(wire, e.slot); u32(wire, e.aligned_byte_offset); u32(wire, e.format);
        u32(wire, e.step_function); u32(wire, e.step_rate);
      }
      break;
    }
    case SM50_SHADER_GS_PASS_THROUGH: {
      const auto &s = *reinterpret_cast<SM50_SHADER_GS_PASS_THROUGH_DATA *>(p);
      // The four byte members are the semantics consumed by the converter.
      u32(wire, s.Data.RenderTargetArrayIndexReg); u32(wire, s.Data.RenderTargetArrayIndexComponent);
      u32(wire, s.Data.ViewportArrayIndexReg); u32(wire, s.Data.ViewportArrayIndexComponent);
      u32(wire, s.RasterizationDisabled); break;
    }
    case SM50_SHADER_PSO_GEOMETRY_SHADER:
      u32(wire, reinterpret_cast<SM50_SHADER_PSO_GEOMETRY_SHADER_DATA *>(p)->strip_topology); break;
    case SM50_SHADER_PSO_TESSELLATOR:
      u32(wire, reinterpret_cast<SM50_SHADER_PSO_TESSELLATOR_DATA *>(p)->max_potential_tess_factor); break;
    case SM50_SHADER_ROOT_SIGNATURE:
    case SM50_SHADER_ROOT_SIGNATURE2: {
      const auto &s = *reinterpret_cast<SM50_SHADER_ROOT_SIGNATURE_DATA *>(p);
      if (!s.bytecode || !s.bytecode_length || s.bytecode_length > MaxRecipeRootSignatureBytes)
        return fail(why, "invalid root signature bounds");
      u32(wire, static_cast<uint32_t>(s.bytecode_length));
      const auto *begin = static_cast<const uint8_t *>(s.bytecode);
      wire.insert(wire.end(), begin, begin + s.bytecode_length);
      break;
    }
    default: return fail(why, "unsupported recipe node type");
    }
  }
  if (wire.size() > MaxRecipeBytes) return fail(why, "recipe too large");
  out.swap(wire);
  return true;
}

inline bool decodeRecipe(const Bytes &wire, OwnedRecipe &out, std::string &why) {
  using namespace recipe_detail;
  why.clear();
  if (wire.size() > MaxRecipeBytes) return fail(why, "recipe too large");
  Reader r{wire};
  uint32_t magic, version, functionSize, count;
  if (!r.u32(magic) || magic != 0x31524357 || !r.u32(version) || version != 1 ||
      !r.u32(functionSize) || !functionSize || functionSize > MaxRecipeFunctionBytes ||
      !r.u32(count) || count > MaxRecipeNodes || functionSize > wire.size() - r.offset)
    return fail(why, "invalid recipe header");
  OwnedRecipe decoded;
  decoded.function.assign(reinterpret_cast<const char *>(wire.data() + r.offset), functionSize);
  r.offset += functionSize;
  if (decoded.function.find('\0') != std::string::npos) return fail(why, "embedded NUL in function name");
  uint32_t seen = 0;
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t type;
    if (!r.u32(type) || type < 1 || type > 9) return fail(why, "unsupported or truncated recipe node");
    if (seen & (1u << type)) return fail(why, "duplicate recipe node type");
    seen |= 1u << type;
    decoded.order.push_back(type);
    switch (type) {
    case 1: {
      auto &s = decoded.streamOutput;
      if (!r.u32(s.num_output_slots) || s.num_output_slots > 4 || !r.u32(s.num_elements) ||
          s.num_elements > MaxRecipeSOElements) return fail(why, "invalid stream output bounds");
      for (auto &stride : s.strides) if (!r.u32(stride)) return fail(why, "truncated stream output strides");
      decoded.streamOutputElements.resize(s.num_elements);
      for (auto &e : decoded.streamOutputElements)
        if (!r.u32(e.reg_id) || !r.u32(e.component) || !r.u32(e.output_slot) || !r.u32(e.offset))
          return fail(why, "truncated stream output element");
      break;
    }
    case 2: {
      uint32_t metal, flags;
      if (!r.u32(metal) || !r.u32(flags) || (flags & ~3u) ||
          (metal != SM50_SHADER_METAL_310 && metal != SM50_SHADER_METAL_320))
        return fail(why, "invalid common settings");
      decoded.common.metal_version = static_cast<SM50_SHADER_METAL_VERSION>(metal);
      decoded.common.flags = static_cast<SM50_SHADER_FLAG>(flags);
      break;
    }
    case 3: {
      auto &s = decoded.pixel;
      if (!r.u32(s.sample_mask) || !r.boolean(s.dual_source_blending) || !r.boolean(s.disable_depth_output) ||
          !r.u32(s.unorm_output_reg_mask)) return fail(why, "invalid pixel settings");
      for (auto &format : s.pixel_formats) if (!r.u32(format)) return fail(why, "truncated pixel formats");
      break;
    }
    case 4: {
      auto &s = decoded.inputLayout;
      uint32_t indexFormat;
      if (!r.u32(indexFormat) || indexFormat > 2 || !r.u32(s.slot_mask) || !r.u32(s.num_elements) ||
          s.num_elements > MaxRecipeIAElements) return fail(why, "invalid input layout bounds");
      s.index_buffer_format = static_cast<SM50_INDEX_BUFFER_FORMAT>(indexFormat);
      decoded.inputElements.resize(s.num_elements);
      for (auto &e : decoded.inputElements) {
        uint32_t stepFunction, stepRate;
        if (!r.u32(e.reg) || !r.u32(e.slot) || !r.u32(e.aligned_byte_offset) || !r.u32(e.format) ||
            !r.u32(stepFunction) || stepFunction > 1 || !r.u32(stepRate) || stepRate > 0x7fffffff)
          return fail(why, "invalid input layout element");
        e.step_function = stepFunction; e.step_rate = stepRate;
      }
      break;
    }
    case 5: {
      auto &s = decoded.passThrough;
      uint32_t a, b, c, d;
      if (!r.u32(a) || a > 255 || !r.u32(b) || b > 255 || !r.u32(c) || c > 255 || !r.u32(d) || d > 255 ||
          !r.boolean(s.RasterizationDisabled)) return fail(why, "invalid geometry pass through settings");
      s.Data = {static_cast<uint8_t>(a), static_cast<uint8_t>(b), static_cast<uint8_t>(c), static_cast<uint8_t>(d)};
      break;
    }
    case 6:
      if (!r.boolean(decoded.geometry.strip_topology)) return fail(why, "invalid geometry settings");
      break;
    case 7:
      if (!r.u32(decoded.tessellator.max_potential_tess_factor)) return fail(why, "truncated tessellator settings");
      break;
    case 8:
    case 9: {
      uint32_t length;
      if (!r.u32(length) || !length || length > MaxRecipeRootSignatureBytes ||
          !r.raw(type == 8 ? decoded.rootSignatureBytes : decoded.rootSignature2Bytes, length))
        return fail(why, "invalid root signature bounds");
      break;
    }
    }
  }
  if (r.offset != wire.size()) return fail(why, "trailing recipe bytes");
  // Reuse encoder validation, and require a canonical byte-for-byte round trip.
  Bytes canonical;
  if (!encodeRecipe(decoded.args(), decoded.function.c_str(), canonical, why)) return false;
  if (canonical != wire) return fail(why, "noncanonical recipe");
  out = std::move(decoded);
  out.args();
  return true;
}

} // namespace dxmt::worker
