// Production TextureView classes/constructors, owners and containers are inserted
// by run.py. Only the native boundary and allocation descriptor setup are mocks.
#pragma once
#include "Metal.hpp"
#include "dxmt_residency.hpp"
#include "rc/util_rc_ptr.hpp"
#include "util_svector.hpp"
#include "util_likely.hpp"
#include "util_flags.hpp"
#include <cassert>
#include <optional>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <new>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
inline void require(bool value,const char *message) {
  if(!value) throw std::runtime_error(std::string("behavioral assertion: ")+message);
}
static thread_local int fail_new_after=-1;
void *operator new(std::size_t bytes) {
  if(fail_new_after==0) throw std::bad_alloc();
  if(fail_new_after>0) --fail_new_after;
  if(auto *p=std::malloc(bytes?bytes:1)) return p;
  throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p,std::size_t) noexcept { std::free(p); }
void *operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete[](void *p) noexcept { ::operator delete(p); }
void operator delete[](void *p,std::size_t) noexcept { ::operator delete(p); }
struct NativeEvent {
  obj_handle_t parent,result;
  WMTPixelFormat format;WMTTextureType type;
  uint16_t first_level,levels,first_slice,slices;
  WMTTextureSwizzleChannels swizzle;
};
static std::array<NativeEvent,4096> native_events;
static std::array<unsigned,4096> native_refs;
static size_t native_count;
static std::atomic<unsigned> descriptor_comparisons=0;
struct BufferNativeEvent { obj_handle_t parent,result;WMTTextureInfo info;uint64_t offset,row; };
static std::array<BufferNativeEvent,4096> buffer_events;
static size_t buffer_count;
static bool apple8=true;
static obj_handle_t next_handle=100;
static bool nil_next_view;
static std::function<void()> native_hook;
extern "C" void NSObject_retain(obj_handle_t handle) {
  if(handle) {require(handle<native_refs.size(),"bounded native handle");++native_refs[handle];}
}
extern "C" void NSObject_release(obj_handle_t handle) {
  if(handle) {require(handle<native_refs.size() && native_refs[handle],"balanced native ownership");--native_refs[handle];}
}
extern "C" obj_handle_t MTLTexture_newTextureView(obj_handle_t texture,WMTPixelFormat format,WMTTextureType type,
    uint16_t first_level,uint16_t levels,uint16_t first_slice,uint16_t slices,WMTTextureSwizzleChannels swizzle,
    uint64_t *gpu_id) {
  if(native_hook) native_hook();
  auto handle=nil_next_view?0:next_handle++;
  nil_next_view=false;
  if(handle) native_refs[handle]=1;
  *gpu_id=handle?handle+10000:0;
  require(native_count<native_events.size(),"bounded native events");
  native_events[native_count++]={texture,handle,format,type,first_level,levels,first_slice,slices,swizzle};
  return handle;
}
extern "C" bool MTLDevice_supportsFamily(obj_handle_t,WMTGPUFamily) { return apple8; }
extern "C" obj_handle_t MTLBuffer_newTexture(obj_handle_t buffer,WMTTextureInfo *info,uint64_t offset,uint64_t row) {
  if(native_hook) native_hook();
  auto handle=nil_next_view?0:next_handle++;nil_next_view=false;
  if(handle) native_refs[handle]=1;
  info->gpu_resource_id=handle?handle+10000:0;
  require(buffer_count<buffer_events.size(),"bounded buffer native events");
  buffer_events[buffer_count++]={buffer,handle,*info,offset,row};return handle;
}
namespace dxmt {
using shared_mutex=std::shared_mutex;
using mutex=std::mutex;
// ACTUAL_BUFFER_TYPES
class BufferAllocation final {
public:
  WMT::Reference<WMT::Buffer> obj_;
  WMTBufferInfo info_{};
  uint32_t version_=0;
  Flags<BufferAllocationFlag> flags_;
  small_vector<std::unique_ptr<BufferView>,1> cached_view_;
  uint32_t suballocation_size_=256,suballocation_count_=4;
  unsigned refs=0;
  BufferAllocation(obj_handle_t handle,Flags<BufferAllocationFlag> flags={}) : obj_(handle),flags_(flags) {native_refs[handle]=1;info_.options=WMTResourceStorageModeShared;}
  Flags<BufferAllocationFlag> flags() const {return flags_;}
  void incRef() {++refs;}
  void decRef() {if(!--refs)delete this;}
};
// ACTUAL_BUFFER_OWNER
// ACTUAL_TEXEL_SIZE
// ACTUAL_BUFFER_METHODS
// ACTUAL_VIEW_TYPES
class TextureAllocation final {
public:
  Texture *descriptor;
  uint64_t gpuResourceID;
  unsigned refs=0;
  uint32_t version_=0;
  small_vector<TextureViewRef,4> cached_view_;
  WMT::Reference<WMT::Texture> native;
  TextureAllocation(Texture *descriptor,obj_handle_t handle) : descriptor(descriptor),gpuResourceID(handle+10000),native(handle) {
    native_refs[handle]=1;
  }
  void incRef() { ++refs; }
  void decRef() { if(--refs==0) delete this; }
  WMT::Texture texture() const { return native; }
};
class Texture {
public:
  WMTTextureInfo info_{};
  Rc<TextureAllocation> current_;
  uint32_t version_=1;
  small_vector<TextureViewDescriptor,4> viewDescriptors_;
  shared_mutex mutex_;
  TextureViewKey fullView;
  Texture() {
    info_.mipmap_level_count=4;
    viewDescriptors_.push_back({.format=WMTPixelFormatRGBA8Unorm,.type=WMTTextureType2DArray,
                               .firstMiplevel=0,.miplevelCount=4,.firstArraySlice=0,.arraySize=6});
    fullView=TextureViewKey(viewDescriptors_[0],0,4);
  }
  unsigned miplevelCount() const { return info_.mipmap_level_count; }
  // ACTUAL_OWNER_DECLARATIONS
};
// ACTUAL_METHODS
} // namespace dxmt
