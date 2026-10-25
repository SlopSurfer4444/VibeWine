#pragma once

#include "Metal.hpp"
#include "dxmt_buffer.hpp"
#include "dxmt_command_constants.hpp"
#include "dxmt_texture.hpp"
#include "rc/util_rc_ptr.hpp"
#include <array>
#include <unordered_map>

namespace dxmt {
struct TileBarrierPSOKey {
  WMTPixelFormat color_formats[8];
  unsigned raster_sample_count;
};
} // namespace dxmt

namespace std {
template <> struct hash<dxmt::TileBarrierPSOKey> {
  size_t
  operator()(const dxmt::TileBarrierPSOKey &v) const noexcept {
    constexpr size_t binsize = sizeof(v);
    return std::hash<string_view>{}({reinterpret_cast<const char *>(&v), binsize});
  };
};

template <> struct equal_to<dxmt::TileBarrierPSOKey> {
  bool
  operator()(const dxmt::TileBarrierPSOKey &x, const dxmt::TileBarrierPSOKey &y) const {
    constexpr size_t binsize = sizeof(x);
    return std::string_view({reinterpret_cast<const char *>(&x), binsize}) ==
           std::string_view({reinterpret_cast<const char *>(&y), binsize});
  }
};
}; // namespace std

namespace dxmt {

class ArgumentEncodingContext;

class InternalCommandLibrary {
public:
  InternalCommandLibrary(WMT::Device device);

  WMT::Library
  getLibrary() {
    return library_;
  }

private:
  WMT::Reference<WMT::Library> library_;
};

class EmulatedCommandContext {
public:
  EmulatedCommandContext(WMT::Device device, InternalCommandLibrary &lib, ArgumentEncodingContext &ctx);

  void
  ClearBufferUint(
      WMT::Buffer buffer, uint64_t byte_offset, uint64_t elements_uint, const std::array<uint32_t, 4> &value
  ) {
    setComputePipelineState(clear_buffer_uint_pipeline, {32, 1, 1});
    setComputeBuffer(buffer, byte_offset, 0);
    setComputeBytes(value.data(), 16, 1);
    setComputeBytes(&elements_uint, 4, 2);
    dispatchThreads({elements_uint, 1, 1});
  }

  void
  ClearTextureBufferUint(WMT::Texture texture, uint32_t offset, uint32_t size, const std::array<uint32_t, 4> &value) {
    setComputePipelineState(clear_texture_buffer_uint_pipeline, {32, 1, 1});
    setComputeTexture(texture, 0);
    struct CLEAR_UINT_DATA {
      const std::array<uint32_t, 4> value;
      uint32_t offset;
      uint32_t size;
      uint32_t padding[2];
    } data {value, offset, size};
    setComputeBytes(&data, sizeof(data), 1);
    dispatchThreads({size, 1, 1});
  }

  void
  ClearBufferFloat(
      WMT::Buffer buffer, uint64_t byte_offset, uint64_t elements_uint, const std::array<float, 4> &value
  ) {
    // just reinterpret float as uint
    setComputePipelineState(clear_buffer_uint_pipeline, {32, 1, 1});
    setComputeBuffer(buffer, byte_offset, 0);
    setComputeBytes(value.data(), 16, 1);
    setComputeBytes(&elements_uint, 4, 2);
    dispatchThreads({elements_uint, 1, 1});
  }

  void
  ClearTextureBufferFloat(WMT::Texture texture, uint32_t offset, uint32_t size, const std::array<float, 4> &value) {
    setComputePipelineState(clear_texture_buffer_float_pipeline, {32, 1, 1});
    struct CLEAR_FLOAT_DATA {
      const std::array<float, 4> value;
      uint32_t offset;
      uint32_t size;
      uint32_t padding[2];
    } data {value, offset, size};
    setComputeTexture(texture, 0);
    setComputeBytes(&data, sizeof(data), 1);
    dispatchThreads({size, 1, 1});
  }

  void
  ClearTexture2DUint(WMT::Texture texture, const std::array<uint32_t, 4> &value) {
    setComputePipelineState(clear_texture_2d_uint_pipeline, {8, 4, 1});
    setComputeTexture(texture, 0);
    setComputeBytes(value.data(), 16, 1);
    dispatchThreads({texture.width(), texture.height(), 1});
  }

  void
  ClearTexture2DFloat(WMT::Texture texture, const std::array<float, 4> &value) {
    setComputePipelineState(clear_texture_2d_float_pipeline, {8, 4, 1});
    setComputeTexture(texture, 0);
    setComputeBytes(value.data(), 16, 1);
    dispatchThreads({texture.width(), texture.height(), 1});
  }

  void
  ClearTexture2DArrayUint(WMT::Texture texture, const std::array<uint32_t, 4> &value) {
    setComputePipelineState(clear_texture_2d_array_uint_pipeline, {8, 4, 1});
    setComputeTexture(texture, 0);
    setComputeBytes(value.data(), 16, 1);
    dispatchThreads({texture.width(), texture.height(), texture.arrayLength()});
  }

  void
  ClearTexture2DArrayFloat(WMT::Texture texture, const std::array<float, 4> &value) {
    setComputePipelineState(clear_texture_2d_array_float_pipeline, {8, 4, 1});
    setComputeTexture(texture, 0);
    setComputeBytes(value.data(), 16, 1);
    dispatchThreads({texture.width(), texture.height(), texture.arrayLength()});
  }

  void
  ClearTexture3DFloat(WMT::Texture texture, const std::array<float, 4> &value) {
    setComputePipelineState(clear_texture_3d_float_pipeline, {8, 4, 1});
    setComputeTexture(texture, 0);
    setComputeBytes(value.data(), 16, 1);
    dispatchThreads({texture.width(), texture.height(), texture.depth()});
  }

  void
  ClearTexture3DUint(WMT::Texture texture, const std::array<uint32_t, 4> &value) {
    setComputePipelineState(clear_texture_3d_uint_pipeline, {8, 4, 1});
    setComputeTexture(texture, 0);
    setComputeBytes(value.data(), 16, 1);
    dispatchThreads({texture.width(), texture.height(), texture.depth()});
  }

  void
  MarshalGSDispatchArguments(WMT::RenderCommandEncoder encoder, WMT::Buffer commands, uint32_t commands_offset) {
#if defined(DXMT_MARSHAL_PACKETS) && DXMT_MARSHAL_PACKETS && \
    !(defined(DXMT_PERF_REF_W4_BATCHING) && DXMT_PERF_REF_W4_BATCHING)
    MarshalDispatchArgumentsPacket(encoder, gs_draw_arguments_marshal, commands, commands_offset);
#else
    encoder.setRenderPipelineState(gs_draw_arguments_marshal);
    encoder.setVertexBuffer(commands, commands_offset, kCustomBufferArgumentIndex0);
    encoder.drawPrimitives(WMTPrimitiveTypePoint, 0, 1);
    encoder.setVertexBuffer({}, 0, kCustomBufferArgumentIndex0);
#endif
  }

  void
  MarshalTSDispatchArguments(WMT::RenderCommandEncoder encoder, WMT::Buffer commands, uint32_t commands_offset) {
#if defined(DXMT_MARSHAL_PACKETS) && DXMT_MARSHAL_PACKETS && \
    !(defined(DXMT_PERF_REF_W4_BATCHING) && DXMT_PERF_REF_W4_BATCHING)
    MarshalDispatchArgumentsPacket(encoder, ts_draw_arguments_marshal, commands, commands_offset);
#else
    encoder.setRenderPipelineState(ts_draw_arguments_marshal);
    encoder.setVertexBuffer(commands, commands_offset, kCustomBufferArgumentIndex0);
    encoder.drawPrimitives(WMTPrimitiveTypePoint, 0, 1);
    encoder.setVertexBuffer({}, 0, kCustomBufferArgumentIndex0);
#endif
  }

  void
  UseMarshalTaskResources(WMT::RenderCommandEncoder encoder, WMT::Resource draw_arguments,
                          WMT::Resource dispatch_arguments) {
    // Exactly two existing declarations. No heap, extra task traversal or
    // full-packet zeroing; every consumed field is assigned once as in wrappers.
    std::array<wmtcmd_render_useresource, 2> packet;
    packet[0].type = packet[1].type = WMTRenderCommandUseResource;
    packet[0].next.set(&packet[1]);
    packet[1].next.set(nullptr);
    packet[0].resource = draw_arguments;
    packet[0].usage = WMTResourceUsageRead;
    packet[1].resource = dispatch_arguments;
    packet[1].usage = WMTResourceUsageWrite;
    packet[0].stages = packet[1].stages = WMTRenderStageVertex;
    encoder.encodeCommands(reinterpret_cast<const wmtcmd_render_nop *>(packet.data()));
  }

private:
  void
  MarshalDispatchArgumentsPacket(WMT::RenderCommandEncoder encoder, WMT::RenderPipelineState pipeline,
                                 WMT::Buffer commands, uint32_t commands_offset) {
    struct {
      wmtcmd_render_setpso pipeline;
      wmtcmd_render_setbuffer arguments;
      wmtcmd_render_draw draw;
      wmtcmd_render_setbuffer unbind;
    } packet;
    packet.pipeline.type = WMTRenderCommandSetPSO;
    packet.pipeline.next.set(&packet.arguments);
    packet.pipeline.pso = pipeline;
    packet.arguments.type = WMTRenderCommandSetVertexBuffer;
    packet.arguments.next.set(&packet.draw);
    packet.arguments.buffer = commands;
    packet.arguments.offset = commands_offset;
    packet.arguments.index = kCustomBufferArgumentIndex0;
    packet.draw.type = WMTRenderCommandDraw;
    packet.draw.next.set(&packet.unbind);
    packet.draw.primitive_type = WMTPrimitiveTypePoint;
    packet.draw.vertex_start = 0;
    packet.draw.vertex_count = 1;
    packet.draw.base_instance = 0;
    packet.draw.instance_count = 1;
    packet.unbind.type = WMTRenderCommandSetVertexBuffer;
    packet.unbind.next.set(nullptr);
    packet.unbind.buffer = 0;
    packet.unbind.offset = 0;
    packet.unbind.index = kCustomBufferArgumentIndex0;
    // Existing native traversal is synchronous; pointers never escape.
    encoder.encodeCommands(reinterpret_cast<const wmtcmd_render_nop *>(&packet.pipeline));
  }

  void setComputePipelineState(WMT::ComputePipelineState state, const WMTSize &threadgroup_size);

  void dispatchThreads(const WMTSize &grid_size);

  void setComputeBuffer(WMT::Buffer buffer, uint64_t offset, uint8_t index);

  void setComputeTexture(WMT::Texture texture, uint8_t index);

  void setComputeBytes(const void *buf, uint64_t length, uint8_t index);

  ArgumentEncodingContext &ctx;
  WMT::Reference<WMT::ComputePipelineState> clear_texture_1d_uint_pipeline;
  WMT::Reference<WMT::ComputePipelineState> clear_texture_1d_array_uint_pipeline;
  WMT::Reference<WMT::ComputePipelineState> clear_texture_2d_uint_pipeline;
  WMT::Reference<WMT::ComputePipelineState> clear_texture_2d_array_uint_pipeline;
  WMT::Reference<WMT::ComputePipelineState> clear_texture_3d_uint_pipeline;
  WMT::Reference<WMT::ComputePipelineState> clear_texture_buffer_uint_pipeline;
  WMT::Reference<WMT::ComputePipelineState> clear_buffer_uint_pipeline;
  WMT::Reference<WMT::ComputePipelineState> clear_texture_1d_float_pipeline;
  WMT::Reference<WMT::ComputePipelineState> clear_texture_1d_array_float_pipeline;
  WMT::Reference<WMT::ComputePipelineState> clear_texture_2d_float_pipeline;
  WMT::Reference<WMT::ComputePipelineState> clear_texture_2d_array_float_pipeline;
  WMT::Reference<WMT::ComputePipelineState> clear_texture_3d_float_pipeline;
  WMT::Reference<WMT::ComputePipelineState> clear_texture_buffer_float_pipeline;

  WMT::Reference<WMT::RenderPipelineState> gs_draw_arguments_marshal;
  WMT::Reference<WMT::RenderPipelineState> ts_draw_arguments_marshal;
};

class DepthStencilBlitContext {

public:
  DepthStencilBlitContext(WMT::Device device, InternalCommandLibrary &lib, ArgumentEncodingContext &ctx);

  void copyFromBuffer(
      const Rc<Buffer> &src, uint64_t src_offset, uint64_t src_length, uint32_t bytes_per_row, uint32_t bytes_per_image,
      const Rc<Texture> &depth_stencil, uint32_t level, uint32_t slice, bool from_d24s8
  );

  void copyFromTexture(
      const Rc<Texture> &depth_stencil, uint32_t level, uint32_t slice, const Rc<Buffer> &dst, uint64_t dst_offset,
      uint64_t dst_length, uint32_t bytes_per_row, uint32_t bytes_per_image, bool to_d24s8
  );

private:
  ArgumentEncodingContext &ctx_;
  WMT::Device device_;
  WMT::Reference<WMT::DepthStencilState> depth_stencil_state_;
  WMT::Reference<WMT::RenderPipelineState> pso_copy_d24s8_;
  WMT::Reference<WMT::RenderPipelineState> pso_copy_d32s8_;
  WMT::Reference<WMT::ComputePipelineState> pso_copy_to_buffer_d24s8_;
  WMT::Reference<WMT::ComputePipelineState> pso_copy_to_buffer_d32s8_;
};

class MTLFXMVScaleContext {
public:
  MTLFXMVScaleContext(WMT::Device device, InternalCommandLibrary &lib, ArgumentEncodingContext &ctx);

  void dispatch(
      const Rc<Texture> &dilated, TextureViewKey view_dilated, const Rc<Texture> &downscaled,
      TextureViewKey view_downscaled, float mv_scale_x, float mv_scale_y
  );

private:
  ArgumentEncodingContext &ctx_;
  WMT::Device device_;
  WMT::Reference<WMT::ComputePipelineState> pso_downscale_dilated_mv_;
};

constexpr auto kBarrierTileSize = 16;

class TileBarrierContext {
public:
  TileBarrierContext(WMT::Device device, InternalCommandLibrary &lib, ArgumentEncodingContext &ctx);

  void dispatch();

private:
  WMT::RenderPipelineState getPSO(TileBarrierPSOKey &format);

  ArgumentEncodingContext &ctx_;
  WMT::Device device_;
  WMT::Reference<WMT::Function> tile_function_;
  std::unordered_map<TileBarrierPSOKey, WMT::Reference<WMT::RenderPipelineState>> psos_;
};

} // namespace dxmt