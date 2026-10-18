#include "cs2_compile_lab.hpp"
#include "cs2_pipeline_archive.hpp"
#include "Metal.hpp"
#include "d3d11_private.h"
#include "d3d11_pipeline.hpp"
#include "d3d11_device.hpp"
#include "d3d11_shader.hpp"
#include "log/log.hpp"
#include <atomic>

namespace dxmt {

class MTLCompiledGraphicsPipelineImpl
    : public MTLCompiledGraphicsPipeline {
public:
  MTLCompiledGraphicsPipelineImpl(MTLD3D11Device *pDevice,
                              MTL_GRAPHICS_PIPELINE_DESC *pDesc)
      : num_rtvs(pDesc->NumColorAttachments),
        depth_stencil_format(pDesc->DepthStencilFormat),
        topology_class(pDesc->TopologyClass), device_(pDevice),
        pBlendState(pDesc->BlendState),
        RasterizationEnabled(pDesc->RasterizationEnabled),
        SampleCount(pDesc->SampleCount) {
    uint32_t unorm_output_reg_mask = 0;
    for (unsigned i = 0; i < num_rtvs; i++) {
      rtv_formats[i] = pDesc->ColorAttachmentFormats[i];
      unorm_output_reg_mask |= (uint32_t(IsUnorm8RenderTargetFormat(pDesc->ColorAttachmentFormats[i])) << i);
    }

    if (pDesc->SOLayout) {
      VertexShader =
          pDesc->VertexShader->get_shader(ShaderVariantVertexStreamOutput{
              pDesc->InputLayout, (uint64_t)pDesc->SOLayout});
    } else {
      VertexShader = pDesc->VertexShader->get_shader(ShaderVariantVertex{
          pDesc->InputLayout, pDesc->GSPassthrough, !pDesc->RasterizationEnabled});
    }

    if (pDesc->PixelShader) {
      PixelShader = pDesc->PixelShader->get_shader(ShaderVariantPixel{
          pDesc->SampleMask, pDesc->BlendState->IsDualSourceBlending(),
          depth_stencil_format == WMTPixelFormatInvalid,
          unorm_output_reg_mask});
      ps_valid_render_targets = pDesc->PixelShader->reflection().PSValidRenderTargets;
    } else {
      PixelShader = nullptr;
      ps_valid_render_targets = 0;
    }
  }

  void GetPipeline(MTL_COMPILED_GRAPHICS_PIPELINE *pPipeline) final {
    cs2lab::wait(ready_, "graphics");
    *pPipeline = {state_};
  }

  ThreadpoolWork *RunThreadpoolWork() {

    TRACE("Start compiling 1 PSO");

    WMT::Reference<WMT::Error> err;
    MTL_COMPILED_SHADER vs{}, ps{};
    if (!VertexShader->GetShader(&vs)) {
      return VertexShader;
    }
    if (PixelShader && !PixelShader->GetShader(&ps)) {
      return PixelShader;
    }

    WMTRenderPipelineInfo info;
    std::memset(&info,0,sizeof(info));
    WMT::InitializeRenderPipelineInfo(info);

    info.vertex_function = vs.Function;

    if (PixelShader) {
      info.fragment_function = ps.Function;
    }
    info.rasterization_enabled = RasterizationEnabled;

    for (unsigned i = 0; i < num_rtvs; i++) {
      if (rtv_formats[i] == WMTPixelFormatInvalid)
        continue;
      info.colors[i].pixel_format = rtv_formats[i];
    }

    if (depth_stencil_format != WMTPixelFormatInvalid) {
      info.depth_pixel_format = depth_stencil_format;
    }
    if (DepthStencilPlanarFlags(depth_stencil_format) & 2) {
      info.stencil_pixel_format = depth_stencil_format;
    }

    if (pBlendState) {
      pBlendState->SetupMetalPipelineDescriptor((WMTRenderPipelineBlendInfo *)&info, num_rtvs, ps_valid_render_targets);
    }

    info.input_primitive_topology = topology_class;
    info.raster_sample_count = SampleCount;
    info.immutable_vertex_buffers = (1 << 16) | (1 << 29) | (1 << 30);
    info.immutable_fragment_buffers = (1 << 29) | (1 << 30);

    recipe_=cs2recipe::make(info,vs.ingredient,ps.ingredient);
    { cs2trace::Scope trace_pso{.type=5, .object=(uint64_t)this};
    state_ = cs2archive::create(device_->GetMTLDevice(), info, vs.CacheKey, ps.CacheKey, err, archive_pending_, (uint64_t)this);
    }

    if (state_ == nullptr) {
      ERR("Failed to create PSO: ", err.description().getUTF8String());
      return this;
    }

    TRACE("Compiled 1 PSO");

    return this;
  }

  bool GetIsDone() { return ready_; }

  void SetIsDone(bool state) {
    ready_.store(state);
    ready_.notify_all();
  }

  std::function<void()> TakePostCompletionWork() override {
    if (!archive_pending_.archive && !cs2recipe::enabled(recipe_))
      return {};
    // The worker may publish ready and run continuations before this executes.
    // Keep the archive reference, path and recipe independent of this object.
    return [pending = std::move(archive_pending_), recipe = recipe_]() mutable {
      pending.save();
      cs2recipe::save(recipe);
    };
  }

private:
  cs2archive::Pending archive_pending_;
  cs2recipe::Recipe recipe_{};
  UINT num_rtvs;
  UINT ps_valid_render_targets;
  WMTPixelFormat rtv_formats[8];
  WMTPixelFormat depth_stencil_format;
  WMTPrimitiveTopologyClass topology_class;
  MTLD3D11Device *device_;
  std::atomic_bool ready_;
  CompiledShader *VertexShader;
  CompiledShader *PixelShader;
  IMTLD3D11BlendState *pBlendState;
  WMT::Reference<WMT::RenderPipelineState> state_;
  bool RasterizationEnabled;
  UINT SampleCount;
};

std::unique_ptr<MTLCompiledGraphicsPipeline>
CreateGraphicsPipeline(MTLD3D11Device *pDevice,
                       MTL_GRAPHICS_PIPELINE_DESC *pDesc) {
  return std::make_unique<MTLCompiledGraphicsPipelineImpl>(pDevice, pDesc);
}

class MTLCompiledComputePipelineImpl
    : public MTLCompiledComputePipeline {
public:
  MTLCompiledComputePipelineImpl(MTLD3D11Device *pDevice, ManagedShader shader)
      : device_(pDevice) {
    ComputeShader = shader->get_shader(ShaderVariantDefault{});
    uint32_t total_tgsize = shader->reflection().ThreadgroupSize[0] *
                            shader->reflection().ThreadgroupSize[1] *
                            shader->reflection().ThreadgroupSize[2];
    // FIXME: might be different on AMD GPU, if it's ever supported
    tgsize_is_multiple_of_sgwidth = (total_tgsize % 32) == 0;
  }

  void GetPipeline(MTL_COMPILED_COMPUTE_PIPELINE *pPipeline) final {
    cs2lab::wait(ready_, "compute");
    *pPipeline = {state_};
  }

  ThreadpoolWork *RunThreadpoolWork() {

    TRACE("Start compiling 1 PSO");

    WMT::Reference<WMT::Error> err;
    MTL_COMPILED_SHADER cs;
    if (!ComputeShader->GetShader(&cs)) {
      return ComputeShader;
    }

    WMTComputePipelineInfo info;
    WMT::InitializeComputePipelineInfo(info);

    info.compute_function = cs.Function;
    info.tgsize_is_multiple_of_sgwidth = tgsize_is_multiple_of_sgwidth;
    info.immutable_buffers = (1 << 29) | (1 << 30);

    { cs2trace::Scope trace_pso{.type=5, .object=(uint64_t)this};
    state_ = device_->GetMTLDevice().newComputePipelineState(info, err);
    }

    if (!state_) {
      ERR("Failed to create compute PSO: ", err.description().getUTF8String());
      return this;
    }

    TRACE("Compiled 1 PSO");

    return this;
  }

  bool GetIsDone() { return ready_; }

  void SetIsDone(bool state) {
    ready_.store(state);
    ready_.notify_all();
  }

private:
  MTLD3D11Device *device_;
  std::atomic_bool ready_;
  CompiledShader *ComputeShader;
  WMT::Reference<WMT::ComputePipelineState> state_;
  bool tgsize_is_multiple_of_sgwidth;
};

std::unique_ptr<MTLCompiledComputePipeline>
CreateComputePipeline(MTLD3D11Device *pDevice, ManagedShader ComputeShader) {
  return std::make_unique<MTLCompiledComputePipelineImpl>(pDevice, ComputeShader);
}

} // namespace dxmt