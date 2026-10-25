// The native/context boundary is substituted. The full production
// CopyTextureBitcast method, real wire structs and real intrusive Rc are used.
#pragma once
#include "Metal.hpp"
#include "rc/util_rc_ptr.hpp"
#include "util_math.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
using dxmt::Rc;
using dxmt::align;
inline void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(std::string("behavioral assertion: ") + message);
}
#define UNREACHABLE throw std::runtime_error("unexpected production path");
#define ERR(...) ++errors
static unsigned errors;
constexpr uint32_t MTL_DXGI_FORMAT_BC = 1;
constexpr uint32_t MTL_DXGI_FORMAT_EMULATED_LINEAR_DEPTH_STENCIL = 2;
constexpr uint32_t MTL_DXGI_FORMAT_EMULATED_D24 = 4;
static WMT::Buffer native_buffer(obj_handle_t handle) { WMT::Buffer b; b.handle = handle; return b; }
static WMT::Texture native_texture(obj_handle_t handle) { WMT::Texture t; t.handle = handle; return t; }
struct Counted {
  unsigned refs = 0;
  void incRef() { ++refs; }
};
struct Allocation final : Counted {
  WMT::Buffer native;
  explicit Allocation(obj_handle_t handle) : native(native_buffer(handle)) {}
  WMT::Buffer buffer() const { return native; }
  void decRef() { if (--refs == 0) delete this; }
};
struct Buffer final : Counted {
  Rc<Allocation> allocation;
  uint64_t offset;
  Buffer(obj_handle_t handle, uint64_t offset) : allocation(new Allocation(handle)), offset(offset) {}
  void decRef() { if (--refs == 0) delete this; }
};
inline unsigned live_staging;
struct StagingResource final : Counted {
  Rc<Buffer> backing;
  uint32_t bytesPerRow, bytesPerImage;
  uint64_t length;
  StagingResource(obj_handle_t handle, uint32_t row, uint32_t image, uint64_t length, uint64_t offset=0) :
      backing(new Buffer(handle, offset)), bytesPerRow(row), bytesPerImage(image), length(length) { ++live_staging; }
  ~StagingResource() { --live_staging; }
  void decRef() { if (--refs == 0) delete this; }
  Rc<Buffer> &buffer() { return backing; }
};
struct Resource { Rc<StagingResource> staging; };
struct Subresource { uint32_t Format=1, MipLevel=0, ArraySlice=0; };
struct Format { uint32_t Flag=0, BytesPerTexel=4; };
struct TextureCopyCommand {
  Resource *pSrc=nullptr, *pDst=nullptr;
  uint32_t SrcSubresource=0, DstSubresource=0;
  Subresource Src, Dst;
  WMTOrigin SrcOrigin{}, DstOrigin{};
  WMTSize SrcSize{};
  Format SrcFormat, DstFormat;
  bool DimensionIncompatible=false;
};
struct Texture final : Counted {
  void decRef() { if (--refs == 0) delete this; }
  WMTPixelFormat pixelFormat() { return WMTPixelFormatRGBA8Unorm; }
};
static WMTPixelFormat Forget_sRGB(WMTPixelFormat format) { return format; }
enum class CommandBufferState { BlitEncoderActive, ReadbackBlitEncoderActive, UpdateBlitEncoderActive };
struct ResourceAccess { enum { Read=1, Write=2 }; };
struct Access { obj_handle_t handle; unsigned offset, length; int flags; };
struct ArgumentEncodingContext {
  std::vector<wmtcmd_blit_copy_from_buffer_to_buffer> copies;
  std::vector<Access> accesses;
  std::vector<Rc<Allocation>> retained;
  struct DepthStencil {
    template<class... T> void copyFromTexture(T&&...) { throw std::runtime_error("unexpected depth texture"); }
    template<class... T> void copyFromBuffer(T&&...) { throw std::runtime_error("unexpected depth buffer"); }
  } blit_depth_stencil_cmd;
  std::pair<Allocation *,uint64_t> access(const Rc<Buffer> &buffer, unsigned offset, unsigned length, int flags) {
    accesses.push_back({buffer->allocation->native.handle,offset,length,flags});
    retained.push_back(buffer->allocation);
    return {buffer->allocation.ptr(), buffer->offset};
  }
  WMT::Texture access(const Rc<Texture> &, uint32_t, uint32_t, int) {
    throw std::runtime_error("unexpected texture access");
  }
  std::pair<WMT::Buffer,uint64_t> allocateTempBuffer(size_t, unsigned) {
    throw std::runtime_error("unexpected temporary buffer");
  }
  template<class T> T &encodeBlitCommand() {
    if constexpr (std::is_same_v<T,wmtcmd_blit_copy_from_buffer_to_buffer>) {
      copies.emplace_back(); return copies.back();
    } else {
      throw std::runtime_error("unexpected command kind");
    }
  }
};
struct FixtureContext {
  bool promote_flush=false;
  std::vector<std::string> events;
  std::vector<std::function<void(ArgumentEncodingContext &)>> queued;
  Rc<StagingResource> GetStagingResource(Resource *resource, uint32_t) { return resource->staging; }
  Rc<Texture> GetTexture(Resource *) { return nullptr; }
  void SwitchToBlitEncoder(CommandBufferState state) {
    require(state==CommandBufferState::BlitEncoderActive,"staging blit encoder kind"); events.push_back("switch");
  }
  void InvalidateCurrentPass(bool) { throw std::runtime_error("unexpected invalidation"); }
  void UseCopyDestination(const Rc<StagingResource> &) { events.push_back("destination"); }
  void UseCopySource(const Rc<StagingResource> &) { events.push_back("source"); }
  template<class F> void EmitOP(F &&f) { events.push_back("emit"); queued.emplace_back(std::forward<F>(f)); }
  // Full production method inserted by run.py, including all branch dispatch.
#include "method-under-test.hpp"
};
