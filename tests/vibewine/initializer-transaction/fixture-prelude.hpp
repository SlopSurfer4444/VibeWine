#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iterator>
#include <limits>
#include <list>
#include <mutex>
#include <new>
#include <span>
#include <unordered_map>
#include <vector>
#include "ftl.hpp"
#include "Metal.hpp"
#include "util_math.hpp"
#include "util_likely.hpp"
#include "util_error.hpp"
#define WARN(...) ((void)0)
namespace dxmt { using mutex=std::mutex; }
struct Payload { unsigned type; obj_handle_t src=0,dst=0; uint64_t offset=0,length=0; };
struct UpdateSample { obj_handle_t buffer; uint64_t offset,length; std::array<unsigned char,8> bytes{}; };
struct NativeBatch { std::vector<Payload> commands; unsigned clears=0; uint64_t signal=0; };
struct Observation {
 unsigned fills=0,copies=0,clears=0,invalid=0,null_handles=0,commits=0,new_buffers=0,updates=0;
 uint64_t signal=0; int fail_buffer_at=0; bool fail_all=false,fail_malloc=false,fail_queue=false,fail_event=false,fail_command=false,fail_blit=false,fail_render=false;
 obj_handle_t next_command=100000,current_command=0;
 std::unordered_map<obj_handle_t,NativeBatch> batches;
 std::unordered_map<obj_handle_t,uint64_t> capacities;
 std::vector<Payload> encoded;
 bool capture_updates=false;std::vector<UpdateSample> update_samples;
};
inline Observation obs;
inline void *fixture_malloc(size_t size) { return obs.fail_malloc ? nullptr : std::malloc(size); }
extern "C" {
void NSObject_retain(obj_handle_t) {}
void NSObject_release(obj_handle_t) {}
obj_handle_t NSAutoreleasePool_alloc_init() {return 1;}
obj_handle_t MTLDevice_newCommandQueue(obj_handle_t,uint64_t) {return obs.fail_queue?0:2;}
obj_handle_t MTLDevice_newSharedEvent(obj_handle_t) {return obs.fail_event?0:3;}
obj_handle_t MTLCommandQueue_commandBuffer(obj_handle_t queue) {assert(queue);if(obs.fail_command)return 0;obs.current_command=++obs.next_command;obs.batches.emplace(obs.current_command,NativeBatch{});return obs.current_command;}
void MTLCommandBuffer_commit(obj_handle_t command) {
 assert(command);auto &batch=obs.batches.at(command);++obs.commits;obs.signal=batch.signal;obs.clears+=batch.clears;
 for(auto &p:batch.commands){obs.encoded.push_back(p);if(p.type==WMTBlitCommandFillBuffer)++obs.fills;else ++obs.copies;}
 obs.batches.erase(command);
}
void MTLCommandBuffer_encodeSignalEvent(obj_handle_t command,obj_handle_t event,uint64_t value) {assert(command&&event);obs.batches.at(command).signal=value;}
uint64_t MTLSharedEvent_signaledValue(obj_handle_t event) {assert(event);return obs.signal;}
bool MTLSharedEvent_waitUntilSignaledValue(obj_handle_t event,uint64_t value,uint64_t) {assert(event);return obs.signal>=value;}
obj_handle_t MTLCommandBuffer_blitCommandEncoder(obj_handle_t command) {assert(command);obs.current_command=command;return obs.fail_blit?0:command;}
obj_handle_t MTLCommandBuffer_renderCommandEncoder(obj_handle_t command,WMTRenderPassInfo *info) {assert(command);if(obs.fail_render)return 0;assert(info->colors[0].texture||info->depth.texture||info->stencil.texture);++obs.batches.at(command).clears;return command;}
void MTLCommandEncoder_endEncoding(obj_handle_t encoder) {assert(encoder);}
obj_handle_t MTLDevice_newBuffer(obj_handle_t,WMTBufferInfo *info) {++obs.new_buffers;info->gpu_address=0;if(obs.fail_all||(obs.fail_buffer_at&&int(obs.new_buffers)>=obs.fail_buffer_at))return 0;obj_handle_t h=100+obs.new_buffers;info->gpu_address=h*4096;obs.capacities[h]=info->length;return h;}
void MTLBuffer_updateContents(obj_handle_t buffer,uint64_t offset,WMTConstMemoryPointer data,uint64_t length) {assert(buffer&&data.ptr);assert(obs.capacities.count(buffer));assert(offset<=obs.capacities.at(buffer)&&length<=obs.capacities.at(buffer)-offset);++obs.updates;if(obs.capture_updates&&obs.update_samples.size()<32){UpdateSample sample{buffer,offset,length};std::memcpy(sample.bytes.data(),data.ptr,std::min<uint64_t>(length,sample.bytes.size()));obs.update_samples.push_back(sample);}}
void MTLBlitCommandEncoder_encodeCommands(obj_handle_t encoder,const wmtcmd_base *cmd) {
 assert(encoder);unsigned visited=0;
 for(;cmd;cmd=(const wmtcmd_base*)cmd->next.ptr) {
  assert(++visited<100000);
  Payload p{cmd->type};
  switch(cmd->type) {
  case WMTBlitCommandNop:continue;
  case WMTBlitCommandFillBuffer: {auto c=(const wmtcmd_blit_fillbuffer*)cmd;p.dst=c->buffer;p.offset=c->offset;p.length=c->length;obs.null_handles+=!p.dst;break;}
  case WMTBlitCommandCopyFromBufferToTexture: {auto c=(const wmtcmd_blit_copy_from_buffer_to_texture*)cmd;p.src=c->src;p.dst=c->dst;p.offset=c->src_offset;p.length=c->bytes_per_row*c->size.height;obs.null_handles+=!p.src||!p.dst;break;}
  case WMTBlitCommandCopyFromBufferToTextureWithBlitOption: {auto c=(const wmtcmd_blit_copy_from_buffer_to_texture_withblitoption*)cmd;p.src=c->src;p.dst=c->dst;p.offset=c->src_offset;p.length=c->bytes_per_row*c->size.height;obs.null_handles+=!p.src||!p.dst;break;}
  default:++obs.invalid;break;
  }
  obs.batches.at(encoder).commands.push_back(p);
 }
}
}
