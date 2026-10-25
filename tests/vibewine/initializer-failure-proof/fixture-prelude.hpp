#pragma once
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iterator>
#include <list>
#include <mutex>
#include <new>
#include <unordered_map>
#include <vector>
#include "ftl.hpp"
#include "Metal.hpp"
#include "util_math.hpp"
#include "util_likely.hpp"
#define WARN(...) ((void)0)
namespace dxmt { using mutex=std::mutex; }
struct Observation { unsigned fills=0,copies=0,clears=0,invalid=0,null_handles=0,commits=0,new_buffers=0,updates=0; uint64_t signal=0,pending=0; int fail_buffer_at=0; bool fail_all=false; };
inline Observation obs;
extern "C" {
void NSObject_retain(obj_handle_t) {}
void NSObject_release(obj_handle_t) {}
obj_handle_t NSAutoreleasePool_alloc_init() {return 1;}
obj_handle_t MTLDevice_newCommandQueue(obj_handle_t,uint64_t) {return 2;}
obj_handle_t MTLDevice_newSharedEvent(obj_handle_t) {return 3;}
obj_handle_t MTLCommandQueue_commandBuffer(obj_handle_t) {return 4;}
void MTLCommandBuffer_commit(obj_handle_t) {++obs.commits;obs.signal=obs.pending;}
void MTLCommandBuffer_encodeSignalEvent(obj_handle_t,obj_handle_t,uint64_t value) {obs.pending=value;}
uint64_t MTLSharedEvent_signaledValue(obj_handle_t) {return obs.signal;}
bool MTLSharedEvent_waitUntilSignaledValue(obj_handle_t,uint64_t value,uint64_t) {return obs.signal>=value;}
obj_handle_t MTLCommandBuffer_blitCommandEncoder(obj_handle_t) {return 5;}
obj_handle_t MTLCommandBuffer_renderCommandEncoder(obj_handle_t,WMTRenderPassInfo *info) {++obs.clears;assert(info->colors[0].texture||info->depth.texture||info->stencil.texture);return 6;}
void MTLCommandEncoder_endEncoding(obj_handle_t) {}
obj_handle_t MTLDevice_newBuffer(obj_handle_t,WMTBufferInfo *info) {++obs.new_buffers;info->gpu_address=0;return obs.fail_all||int(obs.new_buffers)==obs.fail_buffer_at ? 0:100+obs.new_buffers;}
void MTLBuffer_updateContents(obj_handle_t buffer,uint64_t,WMTConstMemoryPointer,uint64_t) {assert(buffer);++obs.updates;}
void MTLBlitCommandEncoder_encodeCommands(obj_handle_t,const wmtcmd_base *cmd) {
 unsigned visited=0;
 for(;cmd;cmd=(const wmtcmd_base*)cmd->next.ptr) {
  assert(++visited<100000);
  switch(cmd->type) {
  case WMTBlitCommandNop:break;
  case WMTBlitCommandFillBuffer: {auto p=(const wmtcmd_blit_fillbuffer*)cmd;++obs.fills;obs.null_handles+=!p->buffer;break;}
  case WMTBlitCommandCopyFromBufferToTexture: {auto p=(const wmtcmd_blit_copy_from_buffer_to_texture*)cmd;++obs.copies;obs.null_handles+=!p->src||!p->dst;break;}
  case WMTBlitCommandCopyFromBufferToTextureWithBlitOption: {auto p=(const wmtcmd_blit_copy_from_buffer_to_texture_withblitoption*)cmd;++obs.copies;obs.null_handles+=!p->src||!p->dst;break;}
  default:++obs.invalid;break;
  }
 }
}
}
