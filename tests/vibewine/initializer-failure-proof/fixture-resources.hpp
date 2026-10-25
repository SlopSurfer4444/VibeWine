#pragma once
namespace dxmt {
class BufferAllocation: public Allocation { public: WMT::Buffer buffer(){WMT::Buffer b;b.handle=500;return b;} private: void free() override {} };
class TextureAllocation: public Allocation { public: WMT::Texture texture(){WMT::Texture t;t.handle=600;return t;} private: void free() override {} };
struct Texture {
 unsigned w=4,h=4,d=1; WMTPixelFormat format=WMTPixelFormatRGBA8Unorm; WMTTextureUsage use=WMTTextureUsageShaderRead; WMTTextureType kind=WMTTextureType2D;
 unsigned width()const{return w;}unsigned height()const{return h;}unsigned depth()const{return d;}
 WMTPixelFormat pixelFormat()const{return format;}WMTTextureUsage usage()const{return use;}WMTTextureType textureType()const{return kind;}
};
uint32_t DepthStencilPlanarFlags(WMTPixelFormat f) {return f==WMTPixelFormatDepth32Float_Stencil8?3:0;}
uint32_t MTLGetTexelSize(WMTPixelFormat) {return 4;}
constexpr uint32_t MTL_DXGI_FORMAT_EMULATED_D24=256;
}
