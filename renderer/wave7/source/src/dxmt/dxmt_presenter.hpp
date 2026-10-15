#pragma once
#include "Metal.hpp"
#include "dxmt_command.hpp"
#include "rc/util_rc.hpp"
#include "winemetal.h"
#include <memory>
#include <mutex>

namespace dxmt {

struct DXMTPresentMetadata {
  float edr_scale;
  float max_content_luminance;
  float max_display_luminance;
};

constexpr uint32_t DXMT_GAMMA_CP_COUNT = 1024;

struct DXMTGammaRamp {
  float red[DXMT_GAMMA_CP_COUNT];
  float green[DXMT_GAMMA_CP_COUNT];
  float blue[DXMT_GAMMA_CP_COUNT];
  uint64_t version;
};

class Presenter : public RcObject {
public:
  Presenter(
      WMT::Device device, WMT::MetalLayer layer, InternalCommandLibrary &lib, float scale_factor, uint8_t sample_count,
      uint64_t swapchain_serial = 0
  );

  ~Presenter();

  // RcObject supplies only the counter; Rc<T> delegates final destruction to T.
  void decRef() {
    if (RcObject::decRef() == 0)
      delete this;
  }

  bool supportsCheckedLayerConfiguration() const { return checked_layer_configuration_; }

  bool changeLayerProperties(
      WMTPixelFormat format, WMTColorSpace colorspace, double width, double height, uint8_t sample_count
  );

  bool changeLayerColorSpace(WMTColorSpace colorspace);

  void changeHDRMetadata(const WMTHDRMetadata *metadata);

  void changeGammaRamp(const DXMTGammaRamp *gamma_ramp);

  // Published once, retained by every queued frame that uses this revision.
  // Native layer mutation is performed only by the ordered encoding stream.
  struct PresentationConfiguration {
    uint64_t revision;
    WMTLayerProps layer_props;
    WMTColorSpace colorspace;
    WMT::Reference<WMT::Texture> gamma_lut_texture;
    WMT::Reference<WMT::RenderPipelineState> present_blit;
    WMT::Reference<WMT::RenderPipelineState> present_scale;
  };

  struct PresentState {
    DXMTPresentMetadata metadata;
    uint64_t frame_id;
    uint32_t trace_generation;
    std::shared_ptr<const PresentationConfiguration> configuration;
  };

  PresentState synchronizeLayerProperties();

  WMT::MetalDrawable encodeCommands(
      WMT::CommandBuffer cmdbuf, WMT::Texture backbuffer, const PresentationConfiguration &configuration,
      DXMTPresentMetadata metadata, uint32_t trace_generation,
      std::function<void(WMT::RenderCommandEncoder)> &&wait_fences,
      std::function<void(WMT::RenderCommandEncoder)> &&update_fences
  );

  // Called only on the single encoding thread, before native present.
  // The native callback owns its session; it never captures this object.
  void observePresentation(WMT::MetalDrawable drawable, uint64_t frame_id,
                           uint64_t revision, uint32_t generation);

private:
  void drainPresentationFeedback();
  void closePresentationFeedback();
  bool buildRenderPipelineState(bool is_pq, bool with_hdr_metadata, bool is_ms, bool gamma_enable);

  WMT::Device device_;
  WMT::Reference<WMT::MetalLayer> layer_;
  std::mutex configuration_mutex_;
  std::shared_ptr<const PresentationConfiguration> configuration_;
  uint64_t configuration_revision_ = 0;
  // Accessed only by the device queue's single encoding thread.
  uint64_t encoded_configuration_revision_ = 0;
  bool checked_layer_configuration_ = false; // fixed before publication
  bool configuration_dirty_ = true;
  InternalCommandLibrary &lib_;
  WMTLayerProps layer_props_{};
  uint32_t sample_count_;
  WMTColorSpace colorspace_ = WMTColorSpaceSRGB;
  WMTHDRMetadata hdr_metadata_;
  bool has_hdr_metadata_ = false;
  WMTPixelFormat source_format_ = WMTPixelFormatInvalid;
  uint64_t display_setting_version_ = 0;
  WMTColorSpace display_colorspace_ = WMTColorSpaceSRGB;
  WMTHDRMetadata display_hdr_metadata_;
  WMTEDRValue display_edr_value_{0.0, 1.0};
  uint64_t gamma_version_ = 0;
  std::array<float, DXMT_GAMMA_CP_COUNT * 4> gamma_lut_rgba_;
  WMT::Reference<WMT::Texture> gamma_lut_texture_;
  WMT::Reference<WMT::RenderPipelineState> present_blit_;
  WMT::Reference<WMT::RenderPipelineState> present_scale_;
  bool pso_valid_ = false;
  uint64_t frame_requested_ = 0;
  const uint64_t swapchain_serial_;
  uint64_t feedback_session_ = 0;
  uint32_t feedback_generation_ = 0;
  bool feedback_capability_checked_ = false;
  uint64_t feedback_capabilities_ = 0;
};
} // namespace dxmt