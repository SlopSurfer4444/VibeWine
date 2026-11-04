#pragma once
// Deliberately no Metal/Wine/GPU linkage. Production class and methods follow
// this seam in the generated fixture. Mocks retain command/descriptor evidence.
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

enum WMTPixelFormat : unsigned { FloatFormat=8, UIntFormat=9, SIntFormat=10, DepthFormat=11, StencilFormat=12 };
enum { WMTCompareFunctionAlways=1, WMTPrimitiveTopologyClassTriangle=2,
       WMTLoadActionLoad=3, WMTStoreActionStore=4, WMTPrimitiveTypeTriangle=5,
       WMTRenderCommandSetPSO=6, WMTRenderCommandSetViewport=7, WMTRenderCommandSetDSSO=8,
       WMTRenderCommandSetScissorRect=9, WMTRenderCommandSetFragmentBytes=10, WMTRenderCommandDraw=11 };
inline unsigned error_logs=0;
#define ERR(...) (++error_logs)
inline void require(bool condition,const char *reason) { if(!condition) throw std::runtime_error(reason); }

namespace WMT {
struct Function { int handle=0; };
struct DepthStencilState { int handle=0; };
struct Error { struct Text { const char *getUTF8String() const { return "injected creation failure"; } }; Text description() const { return {}; } };
struct PipelineData { WMTPixelFormat format; unsigned samples; int fragment; unsigned identity; };
struct RenderPipelineState {
  std::shared_ptr<PipelineData> value;
  explicit operator bool() const { return bool(value); }
  bool operator==(std::nullptr_t) const { return !value; }
};
template<class T> struct Reference : T {
  Reference()=default;
  Reference(T value):T(std::move(value)) {}
};
struct Library {
  Reference<Function> newFunction(const char *name) {
    const std::string value(name);
    int id=value=="vs_clear_rt"?1:value=="fs_clear_rt_depth"?2:value=="fs_clear_rt_float"?3:
           value=="fs_clear_rt_sint"?4:value=="fs_clear_rt_uint"?5:0;
    require(id!=0,"unknown clear shader requested"); return Function{id};
  }
};
struct DeviceState { unsigned attempts=0, next=0; unsigned failures=0; std::vector<PipelineData> descriptors; };
struct Device;
}
struct WMTDepthStencilInfo { struct { bool enabled=false; } front_stencil,back_stencil;
 int depth_compare_function=0; bool depth_write_enabled=false; };
struct WMTRenderPipelineInfo {
 unsigned raster_sample_count=0; WMT::Function vertex_function,fragment_function;
 WMTPixelFormat depth_pixel_format=WMTPixelFormat(0);
 struct { WMTPixelFormat pixel_format=WMTPixelFormat(0); } colors[8];
 bool rasterization_enabled=false; int input_primitive_topology=0;
};
namespace WMT {
inline void InitializeRenderPipelineInfo(WMTRenderPipelineInfo &value) { value={}; }
struct Device {
 std::shared_ptr<DeviceState> state=std::make_shared<DeviceState>();
 Reference<DepthStencilState> newDepthStencilState(const WMTDepthStencilInfo &info) {
   return DepthStencilState{info.depth_write_enabled?2:1};
 }
 Reference<RenderPipelineState> newRenderPipelineState(const WMTRenderPipelineInfo &info, Reference<Error> &) {
   ++state->attempts;
   const auto format=info.depth_pixel_format!=WMTPixelFormat(0)?info.depth_pixel_format:info.colors[0].pixel_format;
   state->descriptors.push_back({format,info.raster_sample_count,info.fragment_function.handle,0});
   require(info.vertex_function.handle==1 && info.rasterization_enabled && info.input_primitive_topology==WMTPrimitiveTopologyClassTriangle,
           "fixed pipeline descriptor changed");
   if(state->failures) { --state->failures; return RenderPipelineState{}; }
   return RenderPipelineState{std::make_shared<PipelineData>(PipelineData{format,info.raster_sample_count,info.fragment_function.handle,++state->next})};
 }
};
}

namespace dxmt {
template<class T> using Rc=std::shared_ptr<T>;
using TextureViewKey=unsigned;
struct Texture {
 WMTPixelFormat format; unsigned samples,width_value,height_value,layers; unsigned array_checks=0;
 WMTPixelFormat pixelFormat(TextureViewKey) const { return format; }
 unsigned sampleCount() const { return samples; }
 TextureViewKey checkViewUseArray(TextureViewKey view,bool array) { require(array,"array normalization lost");++array_checks;return view+1; }
 unsigned width(TextureViewKey) const { return width_value; }
 unsigned height(TextureViewKey) const { return height_value; }
 unsigned arrayLength(TextureViewKey) const { return layers; }
};
inline unsigned DepthStencilPlanarFlags(WMTPixelFormat format) { return format==DepthFormat?1:format==StencilFormat?2:0; }
inline bool IsIntegerFormat(WMTPixelFormat format) { return format==UIntFormat || format==SIntFormat; }
inline WMTPixelFormat MTLGetUnsignedIntegerFormat(WMTPixelFormat) { return UIntFormat; }
struct InternalCommandLibrary { WMT::Library getLibrary() { return {}; } };
enum class PipelineStage { Pixel };
enum class ResourceAccess { Write };
struct Attachment { Rc<Texture> attachment; unsigned depth_plane=99; int load_action=0,store_action=0; };
struct PassInfo { Attachment depth,colors[8]; unsigned render_target_width=0,render_target_height=0,render_target_array_length=0,default_raster_sample_count=0; };
struct wmtcmd_render_setpso { int type=0; WMT::RenderPipelineState pso; };
struct wmtcmd_render_setviewport { int type=0; struct { double x,y,width,height,near_value,far_value; } viewport{}; };
struct wmtcmd_render_setdsso { int type=0,dsso=0,stencil_ref=0; };
struct wmtcmd_render_setscissorrect { int type=0; struct { unsigned x,y,width,height; } scissor_rect{}; };
struct wmtcmd_render_setbytes { int type=0; struct { void *ptr=nullptr; void set(void *p){ptr=p;} } bytes; size_t length=0;unsigned index=99; };
struct wmtcmd_render_draw { int type=0,primitive_type=0;unsigned vertex_start=99,vertex_count=0,base_instance=99,instance_count=0; };
struct ArgumentEncodingContext {
 PassInfo pass; bool active=false; unsigned starts=0,ends=0,commands=0,draws=0;
 wmtcmd_render_setpso pso; wmtcmd_render_setviewport viewport; wmtcmd_render_setdsso depth;
 wmtcmd_render_setscissorrect scissor; wmtcmd_render_setbytes bytes; wmtcmd_render_draw draw;
 alignas(16) std::array<unsigned char,64> heap{};
 PassInfo *startRenderPass(unsigned,unsigned,unsigned,unsigned) { require(!active,"nested clear render pass");active=true;++starts;pass={};return &pass; }
 template<PipelineStage,ResourceAccess=ResourceAccess::Write> void unused();
 template<PipelineStage> Rc<Texture> access(Rc<Texture> texture,TextureViewKey,ResourceAccess) { return texture; }
 template<class T> T &encodeRenderCommand() {
   require(active,"render command without pass");++commands;
   if constexpr(std::is_same_v<T,wmtcmd_render_setpso>) return pso;
   else if constexpr(std::is_same_v<T,wmtcmd_render_setviewport>) return viewport;
   else if constexpr(std::is_same_v<T,wmtcmd_render_setdsso>) return depth;
   else if constexpr(std::is_same_v<T,wmtcmd_render_setscissorrect>) return scissor;
   else if constexpr(std::is_same_v<T,wmtcmd_render_setbytes>) return bytes;
   else { static_assert(std::is_same_v<T,wmtcmd_render_draw>);++draws;return draw; }
 }
 void *allocate_cpu_heap(size_t size,size_t alignment) { require(size<=heap.size() && alignment==16,"clear color allocation contract");return heap.data(); }
 PassInfo *currentRenderEncoder() { require(active,"no active render encoder");return &pass; }
 void endPass() { require(active,"ending missing render pass");active=false;++ends; }
};
} // namespace dxmt
