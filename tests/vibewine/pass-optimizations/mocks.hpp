// CPU adapters only. Commands, fences and encoder structs come from production.
#pragma once
#include "Metal.hpp"
#include "dxmt_deptrack.hpp"
#include "util_flags.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>
#include <functional>
#include <unordered_map>

extern "C" inline void NSObject_retain(obj_handle_t) {}
extern "C" inline void NSObject_release(obj_handle_t) {}

static std::unordered_map<obj_handle_t,std::pair<uint64_t,uint64_t>> native_extents;
extern "C" inline uint64_t MTLTexture_width(obj_handle_t handle) { return native_extents.at(handle).first; }
extern "C" inline uint64_t MTLTexture_height(obj_handle_t handle) { return native_extents.at(handle).second; }
namespace dxmt {
/* TEXTURE_ALLOCATION_FLAGS */
/* TEXTURE_VIEW_KEY */
class Texture;
struct TextureAllocation {
  Texture *descriptor{};
  WMT::Texture object{101};
  void *mappedMemory{};
  Flags<TextureAllocationFlag> allocation_flags{};
  WMT::Texture texture() const { return object; }
  Flags<TextureAllocationFlag> flags() const { return allocation_flags; }
};
struct TextureView {
  WMT::Texture texture{101};
  TextureAllocation *allocation{};
  TextureViewKey key{};
};
struct TextureViewRef {
  TextureView *value{};
  TextureViewRef() = default;
  TextureViewRef(TextureView *v): value(v) {}
  TextureViewRef(TextureView &v): value(&v) {}
  explicit operator bool() const { return value != nullptr; }
  TextureView *operator->() const { return value; }
  TextureView *ptr() const { return value; }
  WMT::Texture texture() const { return value ? value->texture : WMT::Texture{}; }
  bool operator==(const TextureViewRef &v) const { return value == v.value; }
  bool operator!=(const TextureViewRef &v) const { return value != v.value; }
};
class Texture {
public:
  WMTTextureType type{WMTTextureType2D};
  unsigned samples{1}, w{64}, h{32}, d{1}, arrays{1}, mips{1};
  WMTTextureType textureType() const { return type; }
  WMTTextureType textureType(TextureViewKey) const { return type; }
  unsigned sampleCount() const { return samples; }
  unsigned width() const { return w; }
  unsigned width(TextureViewKey k) const { return std::max(w >> k.mip_start, 1u); }
  unsigned height() const { return h; }
  unsigned height(TextureViewKey k) const { return std::max(h >> k.mip_start, 1u); }
  unsigned depth() const { return d; }
  unsigned arrayLength() const { return arrays; }
  unsigned arrayLength(TextureViewKey k) const { return k.array_end - k.array_start; }
  unsigned miplevelCount() const { return mips; }
  WMTPixelFormat pixelFormat(TextureViewKey) const { return WMTPixelFormatRGBA8Unorm; }
  TextureViewKey checkViewUseFormat(TextureViewKey k, WMTPixelFormat) const { return k; }
  TextureView &view(TextureViewKey, TextureAllocation *) { std::abort(); }
};
struct TileBarrierPSOKey {};
/* ENCODER_STRUCTS */
/* ENCODER_LIST_OP */
} // namespace dxmt
/* HELPER */
namespace dxmt {
struct Statistics { unsigned clear_pass_optimized{}, resolve_pass_optimized{}, render_pass_optimized{}; };
class ArgumentEncodingContext {
public:
  Statistics statistics{};
  template<class Fn> void updateFrameStatistics(Fn fn) { fn(statistics); }
  void optimize(std::vector<EncoderData *> &input) {
    auto **encoders = input.data();
    auto encoder_count = static_cast<unsigned>(input.size());
    /* OPTIMIZER */
  }
  DXMT_ENCODER_LIST_OP checkEncoderRelation(EncoderData *, EncoderData *);
  bool hasDataDependency(EncoderData *, EncoderData *);
  bool isEncoderSignatureMatched(RenderEncoderData *, RenderEncoderData *);
  RenderEncoderColorAttachmentData *isClearColorSignatureMatched(ClearEncoderData *, RenderEncoderData *);
  RenderEncoderDepthAttachmentData *isClearDepthSignatureMatched(ClearEncoderData *, RenderEncoderData *);
  RenderEncoderStencilAttachmentData *isClearStencilSignatureMatched(ClearEncoderData *, RenderEncoderData *);
  struct ResolveSignatureMatchResult { RenderEncoderColorAttachmentData *src{}; TextureViewRef dst{}; };
  ResolveSignatureMatchResult isResolveSignatureMatched(RenderEncoderData *, ResolveEncoderData *);
};
/* CONTEXT_METHODS */
} // namespace dxmt
