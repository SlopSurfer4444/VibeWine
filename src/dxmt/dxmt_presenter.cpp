
#include <algorithm>
#include <cmath>
#include <cstring>
#include "wave7_feedback.h"
#include "Metal.hpp"
#include "dxmt_format.hpp"
#include "dxmt_presenter.hpp"
#include "dxmt_present_geometry.hpp"
#include "../d3d11/cs2_trace.hpp"
#include "util_likely.hpp"


namespace dxmt {

Presenter::Presenter(WMT::Device device, WMT::MetalLayer layer, InternalCommandLibrary &lib, float scale_factor, uint8_t sample_count, uint64_t swapchain_serial) :
    device_(device),
    layer_(layer),
    lib_(lib),
    sample_count_(sample_count),
    swapchain_serial_(swapchain_serial) {
  checked_layer_configuration_ = WMT::MetalLayer::supportsCheckedConfiguration();
  if (!checked_layer_configuration_)
    return;
  layer_.getProps(layer_props_);
  layer_props_.device = device;
  layer_props_.opaque = true;
  layer_props_.display_sync_enabled = false;
  layer_props_.framebuffer_only = false; // how strangely setting it true results in worse performance
  layer_props_.contents_scale = layer_props_.contents_scale * scale_factor;
}

bool
Presenter::changeLayerProperties(
    WMTPixelFormat format, WMTColorSpace colorspace, double width, double height, uint8_t sample_count,
    bool preserve_aspect
) {
  std::lock_guard lock(configuration_mutex_);
  bool should_invalidated = colorspace_ != colorspace;
  should_invalidated |= source_format_ != format;
  configuration_dirty_ |= should_invalidated || sample_count_ != sample_count ||
      layer_props_.drawable_width != width || layer_props_.drawable_height != height ||
      preserve_aspect_ != preserve_aspect;
  preserve_aspect_ = preserve_aspect;
  source_format_ = format;
  should_invalidated |= sample_count_ != sample_count;
  sample_count_ = sample_count;
  layer_props_.pixel_format = Forget_sRGB(format);
  layer_props_.drawable_height = height;
  layer_props_.drawable_width = width;
  colorspace_ = colorspace;
  if (should_invalidated)
    pso_valid_ = false; // defer native changes to the captured frame
  return should_invalidated;
}

bool
Presenter::changeLayerColorSpace(WMTColorSpace colorspace) {
  std::lock_guard lock(configuration_mutex_);
  bool should_invalidated = colorspace_ != colorspace;
  colorspace_ = colorspace;
  configuration_dirty_ |= should_invalidated;
  if (should_invalidated)
    pso_valid_ = false;
  return should_invalidated;
}

void
Presenter::changeHDRMetadata(const WMTHDRMetadata *metadata) {
  std::lock_guard lock(configuration_mutex_);
  configuration_dirty_ = true;
  if (metadata) {
    has_hdr_metadata_ = true;
    hdr_metadata_ = *metadata;
    pso_valid_ = false;
  } else {
    if (has_hdr_metadata_) {
      has_hdr_metadata_ = false;
      pso_valid_ = false;
    }
  }
}

void
Presenter::changeGammaRamp(const DXMTGammaRamp *gamma_ramp) {
  std::lock_guard lock(configuration_mutex_);
  if (!gamma_ramp && gamma_version_ != 0) {
    gamma_version_ = 0;
    gamma_lut_texture_ = nullptr;
    configuration_dirty_ = true;
    pso_valid_ = false;
    return;
  }
  if (gamma_ramp && gamma_version_ != gamma_ramp->version) {
    // Never update a texture referenced by an already queued frame or GPU work.
    WMTTextureInfo texture_info{};
    texture_info.type = WMTTextureType2D;
    texture_info.pixel_format = WMTPixelFormatRGBA32Float;
    texture_info.usage = WMTTextureUsageShaderRead;
    texture_info.options = WMTResourceStorageModeShared;
    texture_info.width = DXMT_GAMMA_CP_COUNT;
    texture_info.height = 1;
    texture_info.depth = 1;
    texture_info.mipmap_level_count = 1;
    texture_info.sample_count = 1;
    texture_info.array_length = 1;
    auto candidate = device_.newTexture(texture_info);
    if (!candidate)
      return; // Keep the last complete gamma configuration; retry next Present.
    gamma_version_ = gamma_ramp->version;
    configuration_dirty_ = true;
    for (uint32_t i = 0; i < DXMT_GAMMA_CP_COUNT; i++) {
      gamma_lut_rgba_[i * 4 + 0] = std::clamp(gamma_ramp->red[i], 0.f, 1.f);
      gamma_lut_rgba_[i * 4 + 1] = std::clamp(gamma_ramp->green[i], 0.f, 1.f);
      gamma_lut_rgba_[i * 4 + 2] = std::clamp(gamma_ramp->blue[i], 0.f, 1.f);
      gamma_lut_rgba_[i * 4 + 3] = 1.0f;
    }
    candidate.replaceRegion(
          {0, 0, 0}, {DXMT_GAMMA_CP_COUNT, 1, 1}, 0, 0,
          gamma_lut_rgba_.data(),
          DXMT_GAMMA_CP_COUNT * sizeof(float) * 4,
          0);
    gamma_lut_texture_ = std::move(candidate);
    pso_valid_ = false;
  }
}

Presenter::PresentState
Presenter::synchronizeLayerProperties() {
  cs2trace::Scope trace{.type=9,.object=(uint64_t)this};
  std::lock_guard lock(configuration_mutex_);
  uint64_t display_setting_version = 0;

  WMTQueryDisplaySettingForLayer(
      layer_.handle, &display_setting_version, &display_colorspace_, &display_hdr_metadata_, &display_edr_value_
  );

  if (display_setting_version != display_setting_version_) {
    display_setting_version_ = display_setting_version;
    configuration_dirty_ = true;
    pso_valid_ = false;
  }

  auto final_colorspace = display_setting_version_ > 0 ? display_colorspace_ : colorspace_;
  auto is_hdr = WMT_COLORSPACE_IS_HDR(final_colorspace);
  auto hdr_metadata = display_setting_version_ > 0 ? &display_hdr_metadata_
                      : has_hdr_metadata_          ? &hdr_metadata_
                                                   : nullptr;
  if (!pso_valid_) {
    pso_valid_ = buildRenderPipelineState(
        final_colorspace == WMTColorSpaceHDR_PQ, is_hdr && hdr_metadata != nullptr,
        sample_count_ > 1, gamma_version_ != 0
    );
    configuration_dirty_ = true;
  }
  if (configuration_dirty_) {
    // A failed pipeline build must not pair a new format with an old pipeline.
    // The caller rejects a null configuration before accepting this Present.
    if (pso_valid_) {
      configuration_ = std::make_shared<const PresentationConfiguration>(PresentationConfiguration{
          ++configuration_revision_, layer_props_, final_colorspace,
          gamma_lut_texture_, present_blit_, present_scale_, preserve_aspect_});
      configuration_dirty_ = false;
    } else {
      configuration_.reset();
    }
  }

  DXMTPresentMetadata metadata;

  metadata.edr_scale = 1.0;
  metadata.max_content_luminance = 10000;
  metadata.max_display_luminance = display_edr_value_.maximum_potential_edr_color_component_value * 100;

  if (is_hdr) {
    metadata.edr_scale = display_edr_value_.maximum_edr_color_component_value /
                display_edr_value_.maximum_potential_edr_color_component_value;

    if (hdr_metadata) {
      metadata.max_content_luminance = std::max<uint32_t>(
          {(uint32_t)metadata.max_display_luminance, hdr_metadata->max_content_light_level,
           hdr_metadata->max_mastering_luminance, hdr_metadata->max_frame_average_light_level}
      );
    }
  }

  if (final_colorspace == WMTColorSpaceHDR_scRGB)
    metadata.edr_scale *= 0.8;

  return {metadata, configuration_ ? ++frame_requested_ : 0, cs2trace::generation(), configuration_};
}

namespace {
W7FBRequest feedbackRequest(uint32_t op) {
  W7FBRequest request{};
  request.size = sizeof(request);
  request.version = W7FB_VERSION;
  request.op = op;
  request.status = W7FB_UNSUPPORTED;
  return request;
}

bool feedbackCall(W7FBRequest &request) {
  // This existing thunk returns zero for unknown properties on the donor bridge.
  // No new PE import or Unix-call ordinal is required for capability discovery.
  return MTLCommandBuffer_property(
      static_cast<obj_handle_t>(reinterpret_cast<uintptr_t>(&request)),
      static_cast<WMTCommandBufferProperty>(W7FB_PROPERTY)) == W7FB_REPLY;
}
} // namespace

Presenter::~Presenter() {
  // Queued PresentData owns the presenter until encoding finishes. Native
  // callbacks retain only their native session; close never waits for the GPU.
  closePresentationFeedback();
}

void Presenter::drainPresentationFeedback() {
  if (!feedback_session_)
    return;
  // Bound PE work even if callbacks continue arriving during the drain.
  for (uint32_t i = 0; i < W7FB_RING_CAPACITY; ++i) {
    auto request = feedbackRequest(W7FB_POLL);
    request.session = feedback_session_;
    if (!feedbackCall(request) || request.status != W7FB_OK)
      break;
    const auto &record = request.record;
    if (record.generation != feedback_generation_ || record.swapchain_serial != swapchain_serial_)
      continue;
    const auto received = cs2trace::now();
    uint64_t time_bits;
    std::memcpy(&time_bits, &record.presented_seconds, sizeof(time_bits));
    const uint32_t flags = record.clock_domain |
        ((std::isfinite(record.presented_seconds) && record.presented_seconds > 0) ? 0x100u : 0u);
    // start/end remain PE receipt time. Host timestamp is an explicitly tagged
    // payload, never subtracted from QPC/steady_clock to invent display latency.
    cs2trace::record(feedback_generation_, 71, received, received, swapchain_serial_, flags,
                     record.frame_id, time_bits);
    cs2trace::record(feedback_generation_, 72, received, received, swapchain_serial_, 0,
                     record.frame_id, record.config_revision);
    cs2trace::record(feedback_generation_, 73, received, received, swapchain_serial_, 0,
                     record.frame_id, (uint64_t(record.drawable_width) << 32) | record.drawable_height);
  }
  auto status = feedbackRequest(W7FB_STATUS);
  status.session = feedback_session_;
  if (feedbackCall(status) && status.status == W7FB_OK) {
    const auto t = cs2trace::now();
    // Separate counters: native ring overflow, rejection, pending and close
    // abandonment are not GPU-drop estimates. The PE trace has its own counters.
    const uint64_t values[] = {status.stats.accepted, status.stats.completed,
        status.stats.abandoned, status.stats.overflow, status.stats.rejected,
        status.stats.pending, status.stats.queued};
    for (uint32_t i = 0; i < std::size(values); ++i)
      cs2trace::record(feedback_generation_, 74, t, t, swapchain_serial_, i,
                       feedback_session_, values[i]);
  }
}

void Presenter::closePresentationFeedback() {
  if (!feedback_session_)
    return;
  drainPresentationFeedback();
  auto request = feedbackRequest(W7FB_CLOSE);
  request.session = feedback_session_;
  const bool replied = feedbackCall(request);
  const auto t = cs2trace::now();
  cs2trace::record(feedback_generation_, 75, t, t, swapchain_serial_,
                   replied ? request.status : uint32_t(W7FB_UNSUPPORTED),
                   request.stats.pending, request.stats.abandoned);
  // Only the negotiated native extension makes CLOSE queued occupancy exact.
  const auto discard_status = !replied || !(feedback_capabilities_ & W7FB_CAP_CLOSE_QUEUED_DISCARD)
      ? uint32_t(W7FB_UNSUPPORTED) : request.status;
  cs2trace::record(feedback_generation_, 78, t, t, swapchain_serial_, discard_status,
                   feedback_session_, discard_status == W7FB_OK ? request.stats.queued : 0);
  feedback_session_ = 0;
}

void Presenter::observePresentation(WMT::MetalDrawable drawable, uint64_t frame_id,
                                   uint64_t revision, uint32_t generation) {
  const auto active_generation = cs2trace::generation();
  if (feedback_session_ && feedback_generation_ != active_generation)
    closePresentationFeedback();
  if (!generation || generation != active_generation || !swapchain_serial_)
    return;
  if (!feedback_capability_checked_) {
    auto request = feedbackRequest(W7FB_QUERY);
    if (feedbackCall(request) && request.status == W7FB_OK)
      feedback_capabilities_ = request.capabilities;
    feedback_capability_checked_ = true;
  }
  if (feedback_generation_ != generation) {
    feedback_generation_ = generation;
    const auto t = cs2trace::now();
    cs2trace::record(generation, 70, t, t, swapchain_serial_,
                     (feedback_capabilities_ & W7FB_CAP_PRESENTED_HANDLER) ? W7FB_OK : W7FB_UNSUPPORTED,
                     W7FB_VERSION, feedback_capabilities_);
  }
  if (!(feedback_capabilities_ & W7FB_CAP_PRESENTED_HANDLER))
    return;
  if (!feedback_session_) {
    auto request = feedbackRequest(W7FB_OPEN);
    request.record.generation = generation;
    request.record.swapchain_serial = swapchain_serial_;
    if (!feedbackCall(request) || request.status != W7FB_OK) {
      const auto t = cs2trace::now();
      cs2trace::record(generation, 76, t, t, swapchain_serial_, request.status, frame_id, 0);
      return;
    }
    feedback_session_ = request.session;
  }
  drainPresentationFeedback();
  if (!drawable)
    return;
  const auto texture = drawable.texture();
  if (!texture)
    return;
  auto request = feedbackRequest(W7FB_REGISTER);
  request.session = feedback_session_;
  request.drawable = drawable;
  request.record.generation = generation;
  request.record.swapchain_serial = swapchain_serial_;
  request.record.frame_id = frame_id;
  request.record.config_revision = revision;
  // The obtained texture, not the requested layer size, identifies the image.
  auto actual_texture = texture;
  request.record.drawable_width = uint32_t(actual_texture.width());
  request.record.drawable_height = uint32_t(actual_texture.height());
  if (!feedbackCall(request) || request.status != W7FB_OK) {
    const auto t = cs2trace::now();
    cs2trace::record(generation, 76, t, t, swapchain_serial_, request.status, frame_id, revision);
    if (request.status == W7FB_UNSUPPORTED) {
      closePresentationFeedback();
      feedback_capabilities_ &= ~W7FB_CAP_PRESENTED_HANDLER;
      cs2trace::record(generation, 70, t, t, swapchain_serial_, W7FB_UNSUPPORTED,
                       W7FB_VERSION, feedback_capabilities_);
    }
  }
}

WMT::MetalDrawable
Presenter::encodeCommands(
    WMT::CommandBuffer cmdbuf, WMT::Texture backbuffer, const PresentationConfiguration &configuration,
    DXMTPresentMetadata metadata, uint32_t trace_generation,
    std::function<void(WMT::RenderCommandEncoder)> &&wait_fences,
    std::function<void(WMT::RenderCommandEncoder)> &&update_fences,
    uint64_t trace_frame_id
) {
  if (!checked_layer_configuration_)
    return {};
  if (encoded_configuration_revision_ != configuration.revision) {
    // A failed native application may have changed some properties. Even the
    // previous revision is no longer safe to reuse until fully reapplied.
    encoded_configuration_revision_ = 0;
    const auto status = layer_.setConfigurationChecked(
        configuration.layer_props, configuration.colorspace, configuration.revision);
    if (status != W7LA_OK) {
      if (trace_generation) {
        const auto t = cs2trace::now();
        cs2trace::record(trace_generation, 77, t, t, swapchain_serial_, status, configuration.revision, 0);
      }
      return {};
    }
    encoded_configuration_revision_ = configuration.revision;
  }
  auto drawable = [&]() {
    cs2trace::PresentationScope exact(98,swapchain_serial_,trace_frame_id,configuration.revision,0,trace_generation);
    cs2trace::Scope trace{.type=12,.object=(uint64_t)this};
    return layer_.nextDrawable();
  }();

  if (!drawable)
    return {};
  auto drawable_texture = drawable.texture();
  if (!drawable_texture || drawable_texture.pixelFormat() != configuration.layer_props.pixel_format)
    return {};
  // A layer resize can be asynchronous. Geometry belongs to the acquired image.
  const double width = drawable_texture.width();
  const double height = drawable_texture.height();
  if (width == 0 || height == 0)
    return {};

  // Texture dimensions are immutable. Each accessor crosses the native bridge,
  // so reuse this frame's values for geometry and pipeline selection.
  const uint64_t source_width = backbuffer.width();
  const uint64_t source_height = backbuffer.height();
  auto viewport = fitPresentViewport(width, height, source_width, source_height,
                                    configuration.preserve_aspect);
  const bool has_bars = viewport.originX != 0 || viewport.originY != 0 ||
                        viewport.width != width || viewport.height != height;
  // Blit shaders use absolute raster position; an offset viewport must sample UVs.
  const bool source_size_matched = !has_bars && source_width == (uint64_t)width &&
                                  source_height == (uint64_t)height;

  WMTRenderPassInfo info;
  WMT::InitializeRenderPassInfo(info);
  info.colors[0].load_action = has_bars ? WMTLoadActionClear : WMTLoadActionDontCare;
  info.colors[0].clear_color = {0, 0, 0, 1};
  info.colors[0].store_action = WMTStoreActionStore;
  info.colors[0].texture = drawable_texture;
  auto encoder = cmdbuf.renderCommandEncoder(info);
  if (!encoder)
    return {};
  wait_fences(encoder);
#if defined(DXMT_PERF_REF_W4_BATCHING) && DXMT_PERF_REF_W4_BATCHING
  // Build-only reference: the same commands through individual wrappers.
  encoder.setFragmentTexture(backbuffer, 0);
  encoder.setFragmentTexture(configuration.gamma_lut_texture, 1);
  encoder.setFragmentBytes(&metadata, sizeof(metadata), 0);
  if (source_size_matched)
    encoder.setRenderPipelineState(configuration.present_blit);
  else
    encoder.setRenderPipelineState(configuration.present_scale);
  encoder.setViewport(viewport);
  encoder.drawPrimitives(WMTPrimitiveTypeTriangle, 0, 3);
#else
  // The bridge walks this list synchronously. Keep its metadata and viewport
  // payloads alive until it returns; fences remain outside and in the same order.
  wmtcmd_render_settexture source{}, gamma{};
  wmtcmd_render_setbytes constants{};
  wmtcmd_render_setpso pipeline{};
  wmtcmd_render_setviewports viewport_command{};
  wmtcmd_render_draw draw{};

  source.type = gamma.type = WMTRenderCommandSetFragmentTexture;
  source.texture = backbuffer;
  source.index = 0;
  source.next.set(&gamma);
  gamma.texture = configuration.gamma_lut_texture;
  gamma.index = 1;
  gamma.next.set(&constants);
  constants.type = WMTRenderCommandSetFragmentBytes;
  constants.bytes.set(&metadata);
  constants.length = sizeof(metadata);
  constants.index = 0;
  constants.next.set(&pipeline);
  pipeline.type = WMTRenderCommandSetPSO;
  pipeline.pso = source_size_matched
                     ? configuration.present_blit.handle : configuration.present_scale.handle;
  pipeline.next.set(&viewport_command);
  viewport_command.type = WMTRenderCommandSetViewports;
  viewport_command.viewports.set(&viewport);
  viewport_command.viewport_count = 1;
  viewport_command.next.set(&draw);
  draw.type = WMTRenderCommandDraw;
  draw.primitive_type = WMTPrimitiveTypeTriangle;
  draw.vertex_start = 0;
  draw.vertex_count = 3;
  draw.instance_count = 1;
  draw.base_instance = 0;
  draw.next.set(nullptr);
  encoder.encodeCommands(reinterpret_cast<const wmtcmd_render_nop *>(&source));
#endif
  update_fences(encoder);
  encoder.endEncoding();

  return drawable;
}

bool
Presenter::buildRenderPipelineState(bool is_pq, bool with_hdr_metadata, bool is_ms, bool gamma_enable) {
  auto pool = WMT::MakeAutoreleasePool();

  auto library = lib_.getLibrary();

  uint32_t true_data = true, false_data = false;
  WMTFunctionConstant constants[6];
  constants[0].data.set(&true_data);
  constants[0].type = WMTDataTypeBool;
  constants[0].index = kPresentFCIndex_BackbufferSizeMatched;
  constants[1].data.set(is_pq ? &true_data : &false_data);
  constants[1].type = WMTDataTypeBool;
  constants[1].index = kPresentFCIndex_HDRPQ;
  constants[2].data.set(with_hdr_metadata ? &true_data : &false_data);
  constants[2].type = WMTDataTypeBool;
  constants[2].index = kPresentFCIndex_WithHDRMetadata;
  constants[3].data.set(Is_sRGBVariant(source_format_) ? &true_data : &false_data);
  constants[3].type = WMTDataTypeBool;
  constants[3].index = kPresentFCIndex_BackbufferIsSRGB;
  constants[4].data.set(is_ms ? &true_data : &false_data);
  constants[4].type = WMTDataTypeBool;
  constants[4].index = kPresentFCIndex_BackbufferIsMS;
  constants[5].data.set(gamma_enable ? &true_data : &false_data);
  constants[5].type  = WMTDataTypeBool;
  constants[5].index = kPresentFCIndex_GammaEnabled;

  WMT::Reference<WMT::Error> error;
  auto vs_present_quad = library.newFunction("vs_present_quad");
  auto fs_present_quad = library.newFunctionWithConstants("fs_present_quad", constants, std::size(constants), error);

  constants[0].data.set(&false_data);
  auto fs_present_quad_scaled =
      library.newFunctionWithConstants("fs_present_quad", constants, std::size(constants), error);
  {
    WMTRenderPipelineInfo present_pipeline;
    WMT::InitializeRenderPipelineInfo(present_pipeline);
    present_pipeline.colors[0].pixel_format = layer_props_.pixel_format;
    present_pipeline.vertex_function = vs_present_quad;
    present_pipeline.fragment_function = fs_present_quad;
    auto blit = device_.newRenderPipelineState(present_pipeline, error);
    present_pipeline.fragment_function = fs_present_quad_scaled;
    auto scale = device_.newRenderPipelineState(present_pipeline, error);
    if (!blit || !scale)
      return false;
    present_blit_ = std::move(blit);
    present_scale_ = std::move(scale);
  }
  return true;
}

} // namespace dxmt
