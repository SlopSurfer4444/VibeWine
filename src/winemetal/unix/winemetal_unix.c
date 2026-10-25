#include <stdatomic.h>
#include <string.h>
#include <dlfcn.h>
#import <Cocoa/Cocoa.h>
#import <ColorSync/ColorSync.h>
#import <CoreFoundation/CFRunLoop.h>
#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>
#import <QuartzCore/QuartzCore.h>
#include "objc/objc-runtime.h"
#include <bootstrap.h>
#include <mach/mach_port.h>
#define WINEMETAL_API
#include "../winemetal_thunks.h"
#include "../airconv_thunks.h"

typedef int NTSTATUS;
#define STATUS_SUCCESS 0
#define STATUS_UNSUCCESSFUL 0xC0000001
#include "wave7_feedback.inc"
#include "wave7_display_snapshot.inc"
#include "wave7_visibility.inc"
#include "wave7_acquire_diag.inc"
#include "wave7_ds3_profile.inc"

/* The native Wine request source is serviced in its nested query-wait mode.
 * Raw main GCD dispatch is retained only for older native compatibility. */
static bool
execute_on_main_checked(dispatch_block_t block) {
  static int (*run_on_wine_main)(dispatch_block_t);
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    run_on_wine_main = dlsym(RTLD_DEFAULT, "macdrv_run_on_main_v1");
  });
  if (run_on_wine_main)
    return run_on_wine_main(block) != 0;
  if ([NSThread isMainThread])
    block();
  else
    dispatch_sync(dispatch_get_main_queue(), block);
  return true;
}

/* Preserve the donor's exported void helper ABI. Internal paths check failure. */
void
execute_on_main(dispatch_block_t block) {
  (void)execute_on_main_checked(block);
}

static NTSTATUS
_NSObject_retain(NSObject **obj) {
  [*obj retain];
  return STATUS_SUCCESS;
}

static NTSTATUS
_NSObject_release(NSObject **obj) {
  [*obj release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_NSArray_object(void *obj) {
  struct unixcall_generic_obj_uint64_obj_ret *params = obj;
  params->ret = (obj_handle_t)[(NSArray *)params->handle objectAtIndex:params->arg];
  return STATUS_SUCCESS;
}

static NTSTATUS
_NSArray_count(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [(NSArray *)params->handle count];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCopyAllDevices(void *obj) {
  struct unixcall_generic_obj_ret *params = obj;
  params->ret = (obj_handle_t)MTLCopyAllDevices();
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_recommendedMaxWorkingSetSize(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [(id<MTLDevice>)params->handle recommendedMaxWorkingSetSize];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_currentAllocatedSize(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [(id<MTLDevice>)params->handle currentAllocatedSize];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_name(void *obj) {
  struct unixcall_generic_obj_obj_ret *params = obj;
  params->ret = (obj_handle_t)[(id<MTLDevice>)params->handle name];
  return STATUS_SUCCESS;
}

static NTSTATUS
_NSString_getCString(void *obj) {
  struct unixcall_nsstring_getcstring *params = obj;
  params->ret = (uint32_t)[(NSString *)params->str getCString:(char *)params->buffer_ptr
                                                    maxLength:params->max_length
                                                     encoding:params->encoding];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newCommandQueue(void *obj) {
  struct unixcall_generic_obj_uint64_obj_ret *params = obj;
  params->ret = (obj_handle_t)[(id<MTLDevice>)params->handle newCommandQueueWithMaxCommandBufferCount:params->arg];
  return STATUS_SUCCESS;
}

static NTSTATUS
_NSAutoreleasePool_alloc_init(void *obj) {
  struct unixcall_generic_obj_ret *params = obj;
  params->ret = (obj_handle_t)[[NSAutoreleasePool alloc] init];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandQueue_commandBuffer(void *obj) {
  struct unixcall_generic_obj_obj_ret *params = obj;
  params->ret = (obj_handle_t)[(id<MTLCommandQueue>)params->handle commandBuffer];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandBuffer_commit(void *obj) {
  struct unixcall_generic_obj_noret *params = obj;
  [(id<MTLCommandBuffer>)params->handle commit];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandBuffer_waitUntilCompleted(void *obj) {
  struct unixcall_generic_obj_noret *params = obj;
  [(id<MTLCommandBuffer>)params->handle waitUntilCompleted];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandBuffer_status(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [(id<MTLCommandBuffer>)params->handle status];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newSharedEvent(void *obj) {
  struct unixcall_generic_obj_obj_ret *params = obj;
  params->ret = (obj_handle_t)[(id<MTLDevice>)params->handle newSharedEvent];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLSharedEvent_signaledValue(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [(id<MTLSharedEvent>)params->handle signaledValue];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandBuffer_encodeSignalEvent(void *obj) {
  struct unixcall_generic_obj_obj_uint64_noret *params = obj;
  [(id<MTLCommandBuffer>)params->handle encodeSignalEvent:(id<MTLSharedEvent>)params->arg0 value:params->arg1];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newBuffer(void *obj) {
  struct unixcall_mtldevice_newbuffer *params = obj;
  id<MTLDevice> device = (id<MTLDevice>)params->device;
  struct WMTBufferInfo *info = params->info.ptr;
  id<MTLBuffer> buffer;
  if (info->memory.ptr) {
    buffer = [device newBufferWithBytesNoCopy:info->memory.ptr
                                       length:info->length
                                      options:(enum MTLResourceOptions)info->options
                                  deallocator:NULL];
  } else {
    buffer = [device newBufferWithLength:info->length options:(enum MTLResourceOptions)info->options];
    info->memory.ptr = [buffer storageMode] == MTLStorageModePrivate ? NULL : [buffer contents];
  }
  params->ret = (obj_handle_t)buffer;
  info->gpu_address = [buffer gpuAddress];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newSamplerState(void *obj) {
  struct unixcall_mtldevice_newsamplerstate *params = obj;
  id<MTLDevice> device = (id<MTLDevice>)params->device;
  struct WMTSamplerInfo *info = params->info.ptr;

  MTLSamplerDescriptor *sampler_desc = [[MTLSamplerDescriptor alloc] init];
  sampler_desc.borderColor = (MTLSamplerBorderColor)info->border_color;
  sampler_desc.rAddressMode = (MTLSamplerAddressMode)info->r_address_mode;
  sampler_desc.sAddressMode = (MTLSamplerAddressMode)info->s_address_mode;
  sampler_desc.tAddressMode = (MTLSamplerAddressMode)info->t_address_mode;
  sampler_desc.magFilter = (MTLSamplerMinMagFilter)info->mag_filter;
  sampler_desc.minFilter = (MTLSamplerMinMagFilter)info->min_filter;
  sampler_desc.mipFilter = (MTLSamplerMipFilter)info->mip_filter;
  sampler_desc.compareFunction = (MTLCompareFunction)info->compare_function;
  sampler_desc.lodMaxClamp = info->lod_max_clamp;
  sampler_desc.lodMinClamp = info->lod_min_clamp;
  sampler_desc.maxAnisotropy = info->max_anisotroy;
  sampler_desc.lodAverage = info->lod_average;
  sampler_desc.normalizedCoordinates = info->normalized_coords;
  sampler_desc.supportArgumentBuffers = info->support_argument_buffers;

  id<MTLSamplerState> sampler = [device newSamplerStateWithDescriptor:sampler_desc];
  info->gpu_resource_id = info->support_argument_buffers ? [sampler gpuResourceID]._impl : 0;
  params->ret = (obj_handle_t)sampler;
  [sampler_desc release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newDepthStencilState(void *obj) {
  struct unixcall_mtldevice_newdepthstencilstate *params = obj;
  id<MTLDevice> device = (id<MTLDevice>)params->device;
  const struct WMTDepthStencilInfo *info = params->info.ptr;

  MTLDepthStencilDescriptor *desc = [[MTLDepthStencilDescriptor alloc] init];
  desc.depthCompareFunction = (MTLCompareFunction)info->depth_compare_function;
  desc.depthWriteEnabled = info->depth_write_enabled;

  if (info->front_stencil.enabled) {
    desc.frontFaceStencil.depthStencilPassOperation = (MTLStencilOperation)info->front_stencil.depth_stencil_pass_op;
    desc.frontFaceStencil.depthFailureOperation = (MTLStencilOperation)info->front_stencil.depth_fail_op;
    desc.frontFaceStencil.stencilFailureOperation = (MTLStencilOperation)info->front_stencil.stencil_fail_op;
    desc.frontFaceStencil.stencilCompareFunction = (MTLCompareFunction)info->front_stencil.stencil_compare_function;
    desc.frontFaceStencil.writeMask = info->front_stencil.write_mask;
    desc.frontFaceStencil.readMask = info->front_stencil.read_mask;
  }

  if (info->back_stencil.enabled) {
    desc.backFaceStencil.depthStencilPassOperation = (MTLStencilOperation)info->back_stencil.depth_stencil_pass_op;
    desc.backFaceStencil.depthFailureOperation = (MTLStencilOperation)info->back_stencil.depth_fail_op;
    desc.backFaceStencil.stencilFailureOperation = (MTLStencilOperation)info->back_stencil.stencil_fail_op;
    desc.backFaceStencil.stencilCompareFunction = (MTLCompareFunction)info->back_stencil.stencil_compare_function;
    desc.backFaceStencil.writeMask = info->back_stencil.write_mask;
    desc.backFaceStencil.readMask = info->back_stencil.read_mask;
  }

  params->ret = (obj_handle_t)[device newDepthStencilStateWithDescriptor:desc];
  [desc release];
  return STATUS_SUCCESS;
}

MTLPixelFormat to_metal_pixel_format(enum WMTPixelFormat format) {
  return (MTLPixelFormat)ORIGINAL_FORMAT(format);
}

void
fill_texture_descriptor(MTLTextureDescriptor *desc, struct WMTTextureInfo *info) {
  desc.textureType = (MTLTextureType)info->type;
  desc.pixelFormat = to_metal_pixel_format(info->pixel_format);
  desc.width = info->width;
  desc.height = info->height;
  desc.depth = info->depth;
  desc.arrayLength = info->array_length;
  desc.mipmapLevelCount = info->mipmap_level_count;
  desc.sampleCount = info->sample_count;
  desc.usage = (MTLTextureUsage)info->usage;
  desc.resourceOptions = (MTLResourceOptions)info->options;
};

void
extract_texture_descriptor(id<MTLTexture> desc, struct WMTTextureInfo *info) {
  info->type = desc.textureType;
  info->pixel_format = desc.pixelFormat;
  info->width = desc.width;
  info->height = desc.height;
  info->depth = desc.depth;
  info->array_length = desc.arrayLength;
  info->mipmap_level_count = desc.mipmapLevelCount;
  info->sample_count = desc.sampleCount;
  info->usage = desc.usage;
  info->options = (enum WMTResourceOptions)desc.resourceOptions;
  info->reserved = 0;
};

static NTSTATUS
_MTLDevice_newTexture(void *obj) {
  struct unixcall_mtldevice_newtexture *params = obj;
  id<MTLDevice> device = (id<MTLDevice>)params->device;
  struct WMTTextureInfo *info = params->info.ptr;
  MTLTextureDescriptor *desc = [[MTLTextureDescriptor alloc] init];
  fill_texture_descriptor(desc, info);

  id<MTLTexture> ret = [device newTextureWithDescriptor:desc];
  params->ret = (obj_handle_t)ret;
  info->gpu_resource_id = [ret gpuResourceID]._impl;
  info->mach_port = 0;

  [desc release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLBuffer_newTexture(void *obj) {
  struct unixcall_mtlbuffer_newtexture *params = obj;
  id<MTLBuffer> buffer = (id<MTLBuffer>)params->buffer;
  struct WMTTextureInfo *info = params->info.ptr;
  MTLTextureDescriptor *desc = [[MTLTextureDescriptor alloc] init];
  fill_texture_descriptor(desc, info);

  id<MTLTexture> ret = [buffer newTextureWithDescriptor:desc offset:params->offset bytesPerRow:params->bytes_per_row];
  params->ret = (obj_handle_t)ret;
  info->gpu_resource_id = [ret gpuResourceID]._impl;
  info->mach_port = 0;

  [desc release];
  return STATUS_SUCCESS;
}

static inline MTLTextureSwizzleChannels
to_metal_swizzle(struct WMTTextureSwizzleChannels swizzle, enum WMTPixelFormat format) {
  return MTLTextureSwizzleChannelsMake(
      (MTLTextureSwizzle)DECODE_FORMAT_COMPONENT_SWIZZLE(format, 0, swizzle.r),
      (MTLTextureSwizzle)DECODE_FORMAT_COMPONENT_SWIZZLE(format, 1, swizzle.g),
      (MTLTextureSwizzle)DECODE_FORMAT_COMPONENT_SWIZZLE(format, 2, swizzle.b),
      (MTLTextureSwizzle)DECODE_FORMAT_COMPONENT_SWIZZLE(format, 3, swizzle.a)
  );
}

static NTSTATUS
_MTLTexture_newTextureView(void *obj) {
  struct unixcall_mtltexture_newtextureview *params = obj;
  id<MTLTexture> texture = (id<MTLTexture>)params->texture;

  id<MTLTexture> ret = [texture
      newTextureViewWithPixelFormat:to_metal_pixel_format(params->format)
                        textureType:(MTLTextureType)params->texture_type
                             levels:NSMakeRange(params->level_start, params->level_count)
                             slices:NSMakeRange(params->slice_start, params->slice_count)
                            swizzle:to_metal_swizzle(params->swizzle, params->format)];
  params->ret = (obj_handle_t)ret;
  params->gpu_resource_id = [ret gpuResourceID]._impl;
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_minimumLinearTextureAlignmentForPixelFormat(void *obj) {
  struct unixcall_generic_obj_uint64_uint64_ret *params = obj;
  params->ret = [(id<MTLDevice>)params->handle minimumLinearTextureAlignmentForPixelFormat:to_metal_pixel_format(params->arg)];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newLibrary(void *obj) {
  struct unixcall_mtldevice_newlibrary *params = obj;
  id<MTLDevice> device = (id<MTLDevice>)params->device;
  NSError *err = NULL;
  params->ret_library = (obj_handle_t)[device newLibraryWithData:(dispatch_data_t)params->data error:&err];
  params->ret_error = (obj_handle_t)err;
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLLibrary_newFunction(void *obj) {
  struct unixcall_generic_obj_uint64_obj_ret *params = obj;
  id<MTLLibrary> library = (id<MTLLibrary>)params->handle;
  NSString *name = [[NSString alloc] initWithCString:(char *)params->arg encoding:NSUTF8StringEncoding];
  params->ret = (obj_handle_t)[library newFunctionWithName:name];
  [name release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_NSString_lengthOfBytesUsingEncoding(void *obj) {
  struct unixcall_generic_obj_uint64_uint64_ret *params = obj;
  params->ret = (uint64_t)[(NSString *)params->handle lengthOfBytesUsingEncoding:(NSStringEncoding)params->arg];
  return STATUS_SUCCESS;
}

static NTSTATUS
_NSObject_description(void *obj) {
  struct unixcall_generic_obj_obj_ret *params = obj;
  params->ret = (obj_handle_t)[(NSObject *)params->handle description];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newComputePipelineState(void *obj) {
  struct unixcall_mtldevice_newcomputepso *params = obj;
  id<MTLDevice> device = (id<MTLDevice>)params->device;
  const struct WMTComputePipelineInfo *info = params->info.ptr;
  MTLComputePipelineDescriptor *descriptor = [[MTLComputePipelineDescriptor alloc] init];
  NSError *err = NULL;
  descriptor.computeFunction = (id<MTLFunction>)info->compute_function;
  descriptor.threadGroupSizeIsMultipleOfThreadExecutionWidth = info->tgsize_is_multiple_of_sgwidth;
  descriptor.supportIndirectCommandBuffers = info->support_indirect_command_buffers;
  for (unsigned i = 0; i < 31; i++) {
    if (info->immutable_buffers & (1 << i))
      descriptor.buffers[i].mutability = MTLMutabilityImmutable;
  }
  if (info->num_binary_archives_for_lookup && info->binary_archives_for_lookup.ptr)
    descriptor.binaryArchives = [NSArray arrayWithObjects:(id<MTLBinaryArchive> *)info->binary_archives_for_lookup.ptr
                                                    count:info->num_binary_archives_for_lookup];
  MTLPipelineOption options =
      info->fail_on_binary_archive_miss ? MTLPipelineOptionFailOnBinaryArchiveMiss : MTLPipelineOptionNone;
  params->ret_pso =
      (obj_handle_t)[device newComputePipelineStateWithDescriptor:descriptor options:options reflection:nil error:&err];
  params->ret_error = (obj_handle_t)err;
  if (!err && info->binary_archive_for_serialization) {
    [(id<MTLBinaryArchive>)info->binary_archive_for_serialization addComputePipelineFunctionsWithDescriptor:descriptor
                                                                                                      error:&err];
  }
  [descriptor release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandBuffer_blitCommandEncoder(void *obj) {
  struct unixcall_generic_obj_obj_ret *params = obj;
  params->ret = (obj_handle_t)[(id<MTLCommandBuffer>)params->handle blitCommandEncoder];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandBuffer_computeCommandEncoder(void *obj) {
  struct unixcall_generic_obj_uint64_obj_ret *params = obj;
  params->ret = (obj_handle_t)[(id<MTLCommandBuffer>)params->handle
      computeCommandEncoderWithDispatchType:params->arg ? MTLDispatchTypeConcurrent : MTLDispatchTypeSerial];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandBuffer_renderCommandEncoder(void *obj) {
  struct unixcall_generic_obj_uint64_obj_ret *params = obj;
  struct WMTRenderPassInfo *info = (struct WMTRenderPassInfo *)params->arg;
  MTLRenderPassDescriptor *descriptor = [[MTLRenderPassDescriptor alloc] init];
#if defined(DXMT_PERF_REF_W4_GETTERS) && DXMT_PERF_REF_W4_GETTERS
  // Build-only reference: each assignment repeats the historical getter path.
#define W4_COLOR_ATTACHMENT descriptor.colorAttachments[i]
#define W4_DEPTH_ATTACHMENT descriptor.depthAttachment
#define W4_STENCIL_ATTACHMENT descriptor.stencilAttachment
#else
#define W4_COLOR_ATTACHMENT color
#define W4_DEPTH_ATTACHMENT depth
#define W4_STENCIL_ATTACHMENT stencil
  MTLRenderPassColorAttachmentDescriptorArray *colors = descriptor.colorAttachments;
#endif
#if defined(DXMT_EMPTY_COLOR_SLOTS) && DXMT_EMPTY_COLOR_SLOTS && \
    (!defined(DXMT_PERF_REF_W4_GETTERS) || !DXMT_PERF_REF_W4_GETTERS)
  const double clear_zero = 0.0, clear_one = 1.0;
#endif
  for (unsigned i = 0; i < 8; i++) {
#if defined(DXMT_EMPTY_COLOR_SLOTS) && DXMT_EMPTY_COLOR_SLOTS && \
    (!defined(DXMT_PERF_REF_W4_GETTERS) || !DXMT_PERF_REF_W4_GETTERS)
    const struct WMTColorAttachmentInfo *slot = &info->colors[i];
    // A fresh Metal color descriptor already has this exact public state.
    // Preserve every assignment for noncanonical nil attachments as well as
    // active ones, including clear/resolve/indices used by validation paths.
    if (!slot->texture && !slot->resolve_texture && slot->load_action == WMTLoadActionDontCare &&
        slot->store_action == WMTStoreActionStore && !slot->level && !slot->slice && !slot->depth_plane &&
        !slot->resolve_level && !slot->resolve_slice && !slot->resolve_depth_plane &&
        !memcmp(&slot->clear_color.r, &clear_zero, sizeof(clear_zero)) &&
        !memcmp(&slot->clear_color.g, &clear_zero, sizeof(clear_zero)) &&
        !memcmp(&slot->clear_color.b, &clear_zero, sizeof(clear_zero)) &&
        !memcmp(&slot->clear_color.a, &clear_one, sizeof(clear_one)))
      continue;
#endif
#if !defined(DXMT_PERF_REF_W4_GETTERS) || !DXMT_PERF_REF_W4_GETTERS
    MTLRenderPassColorAttachmentDescriptor *color = colors[i];
#endif
    W4_COLOR_ATTACHMENT.clearColor = MTLClearColorMake(
        info->colors[i].clear_color.r, info->colors[i].clear_color.g, info->colors[i].clear_color.b,
        info->colors[i].clear_color.a
    );
    W4_COLOR_ATTACHMENT.level = info->colors[i].level;
    W4_COLOR_ATTACHMENT.slice = info->colors[i].slice;
    W4_COLOR_ATTACHMENT.depthPlane = info->colors[i].depth_plane;
    W4_COLOR_ATTACHMENT.texture = (id<MTLTexture>)info->colors[i].texture;
    W4_COLOR_ATTACHMENT.loadAction = (MTLLoadAction)info->colors[i].load_action;
    W4_COLOR_ATTACHMENT.storeAction = (MTLStoreAction)info->colors[i].store_action;
    W4_COLOR_ATTACHMENT.resolveTexture = (id<MTLTexture>)info->colors[i].resolve_texture;
    W4_COLOR_ATTACHMENT.resolveLevel = info->colors[i].resolve_level;
    W4_COLOR_ATTACHMENT.resolveSlice = info->colors[i].resolve_slice;
    W4_COLOR_ATTACHMENT.resolveDepthPlane = info->colors[i].resolve_depth_plane;
  }

  if (info->depth.texture) {
#if !defined(DXMT_PERF_REF_W4_GETTERS) || !DXMT_PERF_REF_W4_GETTERS
    MTLRenderPassDepthAttachmentDescriptor *depth = descriptor.depthAttachment;
#endif
    W4_DEPTH_ATTACHMENT.clearDepth = info->depth.clear_depth;
    W4_DEPTH_ATTACHMENT.depthPlane = info->depth.depth_plane;
    W4_DEPTH_ATTACHMENT.level = info->depth.level;
    W4_DEPTH_ATTACHMENT.slice = info->depth.slice;
    W4_DEPTH_ATTACHMENT.texture = (id<MTLTexture>)info->depth.texture;
    W4_DEPTH_ATTACHMENT.loadAction = (MTLLoadAction)info->depth.load_action;
    W4_DEPTH_ATTACHMENT.storeAction = (MTLStoreAction)info->depth.store_action;
  }

  if (info->stencil.texture) {
#if !defined(DXMT_PERF_REF_W4_GETTERS) || !DXMT_PERF_REF_W4_GETTERS
    MTLRenderPassStencilAttachmentDescriptor *stencil = descriptor.stencilAttachment;
#endif
    W4_STENCIL_ATTACHMENT.clearStencil = info->stencil.clear_stencil;
    W4_STENCIL_ATTACHMENT.depthPlane = info->stencil.depth_plane;
    W4_STENCIL_ATTACHMENT.level = info->stencil.level;
    W4_STENCIL_ATTACHMENT.slice = info->stencil.slice;
    W4_STENCIL_ATTACHMENT.texture = (id<MTLTexture>)info->stencil.texture;
    W4_STENCIL_ATTACHMENT.loadAction = (MTLLoadAction)info->stencil.load_action;
    W4_STENCIL_ATTACHMENT.storeAction = (MTLStoreAction)info->stencil.store_action;
  }

  descriptor.defaultRasterSampleCount = info->default_raster_sample_count;
  descriptor.renderTargetArrayLength = info->render_target_array_length;
  descriptor.renderTargetHeight = info->render_target_height;
  descriptor.renderTargetWidth = info->render_target_width;
  descriptor.visibilityResultBuffer = (id<MTLBuffer>)info->visibility_buffer;

  if (info->tile_height && info->tile_width) {
    descriptor.tileWidth = info->tile_width;
    descriptor.tileHeight = info->tile_height;
  }

  params->ret = (obj_handle_t)[(id<MTLCommandBuffer>)params->handle renderCommandEncoderWithDescriptor:descriptor];

#undef W4_COLOR_ATTACHMENT
#undef W4_DEPTH_ATTACHMENT
#undef W4_STENCIL_ATTACHMENT
  [descriptor release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandEncoder_endEncoding(void *obj) {
  struct unixcall_generic_obj_noret *params = obj;
  [(id<MTLCommandEncoder>)params->handle endEncoding];
  return STATUS_SUCCESS;
}

#ifndef DXMT_NO_PRIVATE_API

typedef NS_ENUM(NSUInteger, MTLLogicOperation) {
  MTLLogicOperationClear,
  MTLLogicOperationSet,
  MTLLogicOperationCopy,
  MTLLogicOperationCopyInverted,
  MTLLogicOperationNoop,
  MTLLogicOperationInvert,
  MTLLogicOperationAnd,
  MTLLogicOperationNand,
  MTLLogicOperationOr,
  MTLLogicOperationNor,
  MTLLogicOperationXor,
  MTLLogicOperationEquivalence,
  MTLLogicOperationAndReverse,
  MTLLogicOperationAndInverted,
  MTLLogicOperationOrReverse,
  MTLLogicOperationOrInverted,
};

@interface
MTLRenderPipelineDescriptor ()

- (void)setLogicOperationEnabled:(BOOL)enable;
- (void)setLogicOperation:(MTLLogicOperation)op;

@end

@interface
MTLMeshRenderPipelineDescriptor ()

- (void)setLogicOperationEnabled:(BOOL)enable;
- (void)setLogicOperation:(MTLLogicOperation)op;

@end

#endif

static NTSTATUS
_MTLDevice_newRenderPipelineState(void *obj) {
  struct unixcall_mtldevice_newrenderpso *params = obj;
  const struct WMTRenderPipelineInfo *info = params->info.ptr;
  MTLRenderPipelineDescriptor *descriptor = [[MTLRenderPipelineDescriptor alloc] init];

  for (unsigned i = 0; i < 8; i++) {
    descriptor.colorAttachments[i].pixelFormat = to_metal_pixel_format(info->colors[i].pixel_format);
    descriptor.colorAttachments[i].blendingEnabled = info->colors[i].blending_enabled;
    descriptor.colorAttachments[i].writeMask = (MTLColorWriteMask)info->colors[i].write_mask;

    descriptor.colorAttachments[i].alphaBlendOperation = (MTLBlendOperation)info->colors[i].alpha_blend_operation;
    descriptor.colorAttachments[i].rgbBlendOperation = (MTLBlendOperation)info->colors[i].rgb_blend_operation;

    descriptor.colorAttachments[i].sourceRGBBlendFactor = (MTLBlendFactor)info->colors[i].src_rgb_blend_factor;
    descriptor.colorAttachments[i].sourceAlphaBlendFactor = (MTLBlendFactor)info->colors[i].src_alpha_blend_factor;
    descriptor.colorAttachments[i].destinationRGBBlendFactor = (MTLBlendFactor)info->colors[i].dst_rgb_blend_factor;
    descriptor.colorAttachments[i].destinationAlphaBlendFactor = (MTLBlendFactor)info->colors[i].dst_alpha_blend_factor;
  }

  for (unsigned i = 0; i < 31; i++) {
    if (info->immutable_fragment_buffers & (1 << i))
      descriptor.fragmentBuffers[i].mutability = MTLMutabilityImmutable;
    if (info->immutable_vertex_buffers & (1 << i))
      descriptor.vertexBuffers[i].mutability = MTLMutabilityImmutable;
  }

#ifndef DXMT_NO_PRIVATE_API
  [descriptor setLogicOperationEnabled:info->logic_operation_enabled];
  [descriptor setLogicOperation:(MTLLogicOperation)info->logic_operation];
#endif
  descriptor.depthAttachmentPixelFormat = to_metal_pixel_format(info->depth_pixel_format);
  descriptor.stencilAttachmentPixelFormat = to_metal_pixel_format(info->stencil_pixel_format);
  descriptor.alphaToCoverageEnabled = info->alpha_to_coverage_enabled;
  descriptor.rasterizationEnabled = info->rasterization_enabled;
  descriptor.rasterSampleCount = info->raster_sample_count;
  descriptor.inputPrimitiveTopology = (MTLPrimitiveTopologyClass)info->input_primitive_topology;
  descriptor.tessellationPartitionMode = (MTLTessellationPartitionMode)info->tessellation_partition_mode;
  descriptor.tessellationFactorStepFunction = (MTLTessellationFactorStepFunction)info->tessellation_factor_step;
  descriptor.tessellationOutputWindingOrder = (MTLWinding)info->tessellation_output_winding_order;
  descriptor.maxTessellationFactor = info->max_tessellation_factor;

  descriptor.vertexFunction = (id<MTLFunction>)info->vertex_function;
  descriptor.fragmentFunction = (id<MTLFunction>)info->fragment_function;
  descriptor.supportIndirectCommandBuffers = info->support_indirect_command_buffers;

  if (info->num_binary_archives_for_lookup && info->binary_archives_for_lookup.ptr)
    descriptor.binaryArchives = [NSArray arrayWithObjects:(id<MTLBinaryArchive> *)info->binary_archives_for_lookup.ptr
                                                    count:info->num_binary_archives_for_lookup];
  NSError *err = NULL;
  MTLPipelineOption options =
      info->fail_on_binary_archive_miss ? MTLPipelineOptionFailOnBinaryArchiveMiss : MTLPipelineOptionNone;
  params->ret_pso = (obj_handle_t)[(id<MTLDevice>)params->device newRenderPipelineStateWithDescriptor:descriptor
                                                                                              options:options
                                                                                           reflection:nil
                                                                                                error:&err];
  params->ret_error = (obj_handle_t)err;
  if (!err && info->binary_archive_for_serialization) {
    [(id<MTLBinaryArchive>)info->binary_archive_for_serialization addRenderPipelineFunctionsWithDescriptor:descriptor
                                                                                                     error:&err];
  }
  [descriptor release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newMeshRenderPipelineState(void *obj) {
  struct unixcall_mtldevice_newmeshrenderpso *params = obj;
  const struct WMTMeshRenderPipelineInfo *info = params->info.ptr;
  MTLMeshRenderPipelineDescriptor *descriptor = [[MTLMeshRenderPipelineDescriptor alloc] init];

  for (unsigned i = 0; i < 8; i++) {
    descriptor.colorAttachments[i].pixelFormat = to_metal_pixel_format(info->colors[i].pixel_format);
    descriptor.colorAttachments[i].blendingEnabled = info->colors[i].blending_enabled;
    descriptor.colorAttachments[i].writeMask = (MTLColorWriteMask)info->colors[i].write_mask;

    descriptor.colorAttachments[i].alphaBlendOperation = (MTLBlendOperation)info->colors[i].alpha_blend_operation;
    descriptor.colorAttachments[i].rgbBlendOperation = (MTLBlendOperation)info->colors[i].rgb_blend_operation;

    descriptor.colorAttachments[i].sourceRGBBlendFactor = (MTLBlendFactor)info->colors[i].src_rgb_blend_factor;
    descriptor.colorAttachments[i].sourceAlphaBlendFactor = (MTLBlendFactor)info->colors[i].src_alpha_blend_factor;
    descriptor.colorAttachments[i].destinationRGBBlendFactor = (MTLBlendFactor)info->colors[i].dst_rgb_blend_factor;
    descriptor.colorAttachments[i].destinationAlphaBlendFactor = (MTLBlendFactor)info->colors[i].dst_alpha_blend_factor;
  }

  for (unsigned i = 0; i < 31; i++) {
    if (info->immutable_fragment_buffers & (1 << i))
      descriptor.fragmentBuffers[i].mutability = MTLMutabilityImmutable;
    if (info->immutable_mesh_buffers & (1 << i))
      descriptor.meshBuffers[i].mutability = MTLMutabilityImmutable;
    if (info->immutable_object_buffers & (1 << i))
      descriptor.objectBuffers[i].mutability = MTLMutabilityImmutable;
  }

#ifndef DXMT_NO_PRIVATE_API
  [descriptor setLogicOperationEnabled:info->logic_operation_enabled];
  [descriptor setLogicOperation:(MTLLogicOperation)info->logic_operation];
#endif
  descriptor.depthAttachmentPixelFormat = to_metal_pixel_format(info->depth_pixel_format);
  descriptor.stencilAttachmentPixelFormat = to_metal_pixel_format(info->stencil_pixel_format);
  descriptor.alphaToCoverageEnabled = info->alpha_to_coverage_enabled;
  descriptor.rasterizationEnabled = info->rasterization_enabled;
  descriptor.rasterSampleCount = info->raster_sample_count;

  descriptor.objectFunction = (id<MTLFunction>)info->object_function;
  descriptor.meshFunction = (id<MTLFunction>)info->mesh_function;
  descriptor.fragmentFunction = (id<MTLFunction>)info->fragment_function;
  descriptor.payloadMemoryLength = info->payload_memory_length;

  descriptor.meshThreadgroupSizeIsMultipleOfThreadExecutionWidth = info->mesh_tgsize_is_multiple_of_sgwidth;
  descriptor.objectThreadgroupSizeIsMultipleOfThreadExecutionWidth = info->object_tgsize_is_multiple_of_sgwidth;
  descriptor.supportIndirectCommandBuffers = info->support_indirect_command_buffers;

  MTLPipelineOption options = MTLPipelineOptionNone;
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 150000
  if (@available(macOS 15, *)) {
    if (info->num_binary_archives_for_lookup && info->binary_archives_for_lookup.ptr)
      descriptor.binaryArchives = [NSArray arrayWithObjects:(id<MTLBinaryArchive> *)info->binary_archives_for_lookup.ptr
                                                      count:info->num_binary_archives_for_lookup];
    options = info->fail_on_binary_archive_miss ? MTLPipelineOptionFailOnBinaryArchiveMiss : MTLPipelineOptionNone;
  }
#endif
  NSError *err = NULL;
  params->ret_pso = (obj_handle_t)[(id<MTLDevice>)params->device newRenderPipelineStateWithMeshDescriptor:descriptor
                                                                                                  options:options
                                                                                               reflection:nil
                                                                                                    error:&err];
  params->ret_error = (obj_handle_t)err;
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 150000
  if (@available(macOS 15, *)) {
    if (!err && info->binary_archive_for_serialization) {
      [(id<MTLBinaryArchive>)info->binary_archive_for_serialization
          addMeshRenderPipelineFunctionsWithDescriptor:descriptor
                                                 error:&err];
    }
  }
#endif
  [descriptor release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLBlitCommandEncoder_encodeCommands(void *obj) {
  struct unixcall_generic_obj_cmd_noret *params = obj;
  const struct wmtcmd_base *next = params->cmd_head.ptr;
  id<MTLBlitCommandEncoder> encoder = (id<MTLBlitCommandEncoder>)params->encoder;
  while (next) {
    switch ((enum WMTBlitCommandType)next->type) {
    default:
      assert(!next->type && "unhandled blit command type");
      break;
    case WMTBlitCommandCopyFromBufferToBuffer: {
      struct wmtcmd_blit_copy_from_buffer_to_buffer *body = (struct wmtcmd_blit_copy_from_buffer_to_buffer *)next;
      [encoder copyFromBuffer:(id<MTLBuffer>)body->src
                 sourceOffset:body->src_offset
                     toBuffer:(id<MTLBuffer>)body->dst
            destinationOffset:body->dst_offset
                         size:body->copy_length];
      break;
    }
    case WMTBlitCommandCopyFromBufferToTexture: {
      struct wmtcmd_blit_copy_from_buffer_to_texture *body = (struct wmtcmd_blit_copy_from_buffer_to_texture *)next;
      [encoder copyFromBuffer:(id<MTLBuffer>)body->src
                 sourceOffset:body->src_offset
            sourceBytesPerRow:body->bytes_per_row
          sourceBytesPerImage:body->bytes_per_image
                   sourceSize:MTLSizeMake(body->size.width, body->size.height, body->size.depth)
                    toTexture:(id<MTLTexture>)body->dst
             destinationSlice:body->slice
             destinationLevel:body->level
            destinationOrigin:MTLOriginMake(body->origin.x, body->origin.y, body->origin.z)];
      break;
    }
    case WMTBlitCommandCopyFromBufferToTextureWithBlitOption: {
      struct wmtcmd_blit_copy_from_buffer_to_texture_withblitoption *body =
          (struct wmtcmd_blit_copy_from_buffer_to_texture_withblitoption *)next;
      [encoder copyFromBuffer:(id<MTLBuffer>)body->src
                 sourceOffset:body->src_offset
            sourceBytesPerRow:body->bytes_per_row
          sourceBytesPerImage:body->bytes_per_image
                   sourceSize:MTLSizeMake(body->size.width, body->size.height, body->size.depth)
                    toTexture:(id<MTLTexture>)body->dst
             destinationSlice:body->slice
             destinationLevel:body->level
            destinationOrigin:MTLOriginMake(body->origin.x, body->origin.y, body->origin.z)
                      options:(MTLBlitOption)body->options];
      break;
    }
    case WMTBlitCommandCopyFromTextureToBuffer: {
      struct wmtcmd_blit_copy_from_texture_to_buffer *body = (struct wmtcmd_blit_copy_from_texture_to_buffer *)next;
      [encoder copyFromTexture:(id<MTLTexture>)body->src
                       sourceSlice:body->slice
                       sourceLevel:body->level
                      sourceOrigin:MTLOriginMake(body->origin.x, body->origin.y, body->origin.z)
                        sourceSize:MTLSizeMake(body->size.width, body->size.height, body->size.depth)
                          toBuffer:(id<MTLBuffer>)body->dst
                 destinationOffset:body->offset
            destinationBytesPerRow:body->bytes_per_row
          destinationBytesPerImage:body->bytes_per_image];
      break;
    }
    case WMTBlitCommandCopyFromTextureToBufferWithBlitOption: {
      struct wmtcmd_blit_copy_from_texture_to_buffer_withblitoption *body =
          (struct wmtcmd_blit_copy_from_texture_to_buffer_withblitoption *)next;
      [encoder copyFromTexture:(id<MTLTexture>)body->src
                       sourceSlice:body->slice
                       sourceLevel:body->level
                      sourceOrigin:MTLOriginMake(body->origin.x, body->origin.y, body->origin.z)
                        sourceSize:MTLSizeMake(body->size.width, body->size.height, body->size.depth)
                          toBuffer:(id<MTLBuffer>)body->dst
                 destinationOffset:body->offset
            destinationBytesPerRow:body->bytes_per_row
          destinationBytesPerImage:body->bytes_per_image
                           options:(MTLBlitOption)body->options];
      break;
    }
    case WMTBlitCommandCopyFromTextureToTexture: {
      struct wmtcmd_blit_copy_from_texture_to_texture *body = (struct wmtcmd_blit_copy_from_texture_to_texture *)next;
      [encoder copyFromTexture:(id<MTLTexture>)body->src
                   sourceSlice:body->src_slice
                   sourceLevel:body->src_level
                  sourceOrigin:MTLOriginMake(body->src_origin.x, body->src_origin.y, body->src_origin.z)
                    sourceSize:MTLSizeMake(body->src_size.width, body->src_size.height, body->src_size.depth)
                     toTexture:(id<MTLTexture>)body->dst
              destinationSlice:body->dst_slice
              destinationLevel:body->dst_level
             destinationOrigin:MTLOriginMake(body->dst_origin.x, body->dst_origin.y, body->dst_origin.z)];
      break;
    }
    case WMTBlitCommandGenerateMipmaps: {
      struct wmtcmd_blit_generate_mipmaps *body = (struct wmtcmd_blit_generate_mipmaps *)next;
      [encoder generateMipmapsForTexture:(id<MTLTexture>)body->texture];
      break;
    }
    case WMTBlitCommandUpdateFence: {
      struct wmtcmd_blit_fence_op *body = (struct wmtcmd_blit_fence_op *)next;
      [encoder updateFence:(id<MTLFence>)body->fence];
      break;
    }
    case WMTBlitCommandWaitForFence: {
      struct wmtcmd_blit_fence_op *body = (struct wmtcmd_blit_fence_op *)next;
      [encoder waitForFence:(id<MTLFence>)body->fence];
      break;
    }
    case WMTBlitCommandFillBuffer: {
      struct wmtcmd_blit_fillbuffer *body = (struct wmtcmd_blit_fillbuffer *)next;
      [encoder fillBuffer:(id<MTLBuffer>)body->buffer range:NSMakeRange(body->offset, body->length) value:body->value];
      break;
    }
    case WMTBlitCommandResolveCounters: {
      struct wmtcmd_blit_resolvecounters *body = (struct wmtcmd_blit_resolvecounters *)next;
      [encoder resolveCounters:(id<MTLCounterSampleBuffer>)body->sample_buffer
                       inRange:NSMakeRange(body->start, body->len)
             destinationBuffer:(id<MTLBuffer>)body->dst_buffer
             destinationOffset:body->dst_offset];
      break;
    }
    case WMTBlitCommandResetCommandsInBuffer: {
      struct wmtcmd_blit_resetcommands *body = (struct wmtcmd_blit_resetcommands *)next;
      [encoder resetCommandsInBuffer:(id<MTLIndirectCommandBuffer>)body->indirect_command_buffer
                           withRange:NSMakeRange(body->location, body->length)];
      break;
    }
    case WMTBlitCommandCopyTexture: {
      struct wmtcmd_blit_copy_texture *body = (struct wmtcmd_blit_copy_texture *)next;
      [encoder copyFromTexture:(id<MTLTexture>)body->src toTexture:(id<MTLTexture>)body->dst];
      break;
    }
    }

    next = next->next.ptr;
  }
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLComputeCommandEncoder_encodeCommands(void *obj) {
  struct unixcall_generic_obj_cmd_noret *params = obj;
  const struct wmtcmd_base *next = params->cmd_head.ptr;
  id<MTLComputeCommandEncoder> encoder = (id<MTLComputeCommandEncoder>)params->encoder;
  MTLSize threadgroup_size = {0, 0, 0};
  while (next) {
    switch ((enum WMTComputeCommandType)next->type) {
    default:
      assert(!next->type && "unhandled compute command type");
      break;
    case WMTComputeCommandDispatch: {
      struct wmtcmd_compute_dispatch *body = (struct wmtcmd_compute_dispatch *)next;
      [encoder dispatchThreadgroups:MTLSizeMake(body->size.width, body->size.height, body->size.depth)
              threadsPerThreadgroup:threadgroup_size];
      break;
    }
    case WMTComputeCommandDispatchThreads: {
      struct wmtcmd_compute_dispatch *body = (struct wmtcmd_compute_dispatch *)next;
      [encoder dispatchThreads:MTLSizeMake(body->size.width, body->size.height, body->size.depth)
          threadsPerThreadgroup:threadgroup_size];
      break;
    }
    case WMTComputeCommandDispatchIndirect: {
      struct wmtcmd_compute_dispatch_indirect *body = (struct wmtcmd_compute_dispatch_indirect *)next;
      [encoder dispatchThreadgroupsWithIndirectBuffer:(id<MTLBuffer>)body->indirect_args_buffer
                                 indirectBufferOffset:body->indirect_args_offset
                                threadsPerThreadgroup:threadgroup_size];
      break;
    }
    case WMTComputeCommandSetPSO: {
      struct wmtcmd_compute_setpso *body = (struct wmtcmd_compute_setpso *)next;
      [encoder setComputePipelineState:(id<MTLComputePipelineState>)body->pso];
      threadgroup_size.width = body->threadgroup_size.width;
      threadgroup_size.height = body->threadgroup_size.height;
      threadgroup_size.depth = body->threadgroup_size.depth;
      break;
    }
    case WMTComputeCommandSetBuffer: {
      struct wmtcmd_compute_setbuffer *body = (struct wmtcmd_compute_setbuffer *)next;
      [encoder setBuffer:(id<MTLBuffer>)body->buffer offset:body->offset atIndex:body->index];
      break;
    }
    case WMTComputeCommandSetBufferOffset: {
      struct wmtcmd_compute_setbufferoffset *body = (struct wmtcmd_compute_setbufferoffset *)next;
      [encoder setBufferOffset:body->offset atIndex:body->index];
      break;
    }
    case WMTComputeCommandUseResource: {
      struct wmtcmd_compute_useresource *body = (struct wmtcmd_compute_useresource *)next;
      [encoder useResource:(id<MTLResource>)body->resource usage:(MTLResourceUsage)body->usage];
      break;
    }
    case WMTComputeCommandSetBytes: {
      struct wmtcmd_compute_setbytes *body = (struct wmtcmd_compute_setbytes *)next;
      [encoder setBytes:body->bytes.ptr length:body->length atIndex:body->index];
      break;
    }
    case WMTComputeCommandSetTexture: {
      struct wmtcmd_compute_settexture *body = (struct wmtcmd_compute_settexture *)next;
      [encoder setTexture:(id<MTLTexture>)body->texture atIndex:body->index];
      break;
    }
    case WMTComputeCommandUpdateFence: {
      struct wmtcmd_compute_fence_op *body = (struct wmtcmd_compute_fence_op *)next;
      [encoder updateFence:(id<MTLFence>)body->fence];
      break;
    }
    case WMTComputeCommandWaitForFence: {
      struct wmtcmd_compute_fence_op *body = (struct wmtcmd_compute_fence_op *)next;
      [encoder waitForFence:(id<MTLFence>)body->fence];
      break;
    }
    case WMTComputeCommandMemoryBarrier: {
      struct wmtcmd_compute_memory_barrier *body = (struct wmtcmd_compute_memory_barrier *)next;
      [encoder memoryBarrierWithScope:(MTLBarrierScope)body->scope];
      break;
    }    
    case WMTComputeCommandExecuteCommandsInBuffer: {
      struct wmtcmd_compute_executecommands *body = (struct wmtcmd_compute_executecommands *)next;
      [encoder executeCommandsInBuffer:(id<MTLIndirectCommandBuffer>)body->indirect_command_buffer
                             withRange:NSMakeRange(body->location, body->length)];
      break;
    }
    }

    next = next->next.ptr;
  }
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLRenderCommandEncoder_encodeCommands(void *obj) {
  struct unixcall_generic_obj_cmd_noret *params = obj;
  const struct wmtcmd_base *next = params->cmd_head.ptr;
  id<MTLRenderCommandEncoder> encoder = (id<MTLRenderCommandEncoder>)params->encoder;
#if defined(DXMT_NATIVE_RASTER_SHADOW) && DXMT_NATIVE_RASTER_SHADOW
  // Command storage is immutable until this synchronous decoder returns. Never
  // preserve state across calls, encoder handles, or indirect execution.
  const struct wmtcmd_render_setrasterizerstate *raster_shadow = NULL;
#endif
  while (next) {
    switch ((enum WMTRenderCommandType)next->type) {
    default:
#if defined(DXMT_NATIVE_RASTER_SHADOW) && DXMT_NATIVE_RASTER_SHADOW
      raster_shadow = NULL;
#endif
      assert(!next->type && "unhandled render command type");
      break;
    case WMTRenderCommandNop:
      break;
    case WMTRenderCommandUseResource: {
      struct wmtcmd_render_useresource *body = (struct wmtcmd_render_useresource *)next;
      [encoder useResource:(id<MTLResource>)body->resource
                     usage:(MTLResourceUsage)body->usage
                    stages:(MTLRenderStages)body->stages];
      break;
    }
    case WMTRenderCommandSetVertexBuffer: {
      struct wmtcmd_render_setbuffer *body = (struct wmtcmd_render_setbuffer *)next;
      [encoder setVertexBuffer:(id<MTLBuffer>)body->buffer offset:body->offset atIndex:body->index];
      break;
    }
    case WMTRenderCommandSetVertexBufferOffset: {
      struct wmtcmd_render_setbufferoffset *body = (struct wmtcmd_render_setbufferoffset *)next;
      [encoder setVertexBufferOffset:body->offset atIndex:body->index];
      break;
    }
    case WMTRenderCommandSetFragmentBuffer: {
      struct wmtcmd_render_setbuffer *body = (struct wmtcmd_render_setbuffer *)next;
      [encoder setFragmentBuffer:(id<MTLBuffer>)body->buffer offset:body->offset atIndex:body->index];
      break;
    }
    case WMTRenderCommandSetFragmentBufferOffset: {
      struct wmtcmd_render_setbufferoffset *body = (struct wmtcmd_render_setbufferoffset *)next;
      [encoder setFragmentBufferOffset:body->offset atIndex:body->index];
      break;
    }
    case WMTRenderCommandSetMeshBuffer: {
      struct wmtcmd_render_setbuffer *body = (struct wmtcmd_render_setbuffer *)next;
      [encoder setMeshBuffer:(id<MTLBuffer>)body->buffer offset:body->offset atIndex:body->index];
      break;
    }
    case WMTRenderCommandSetMeshBufferOffset: {
      struct wmtcmd_render_setbufferoffset *body = (struct wmtcmd_render_setbufferoffset *)next;
      [encoder setMeshBufferOffset:body->offset atIndex:body->index];
      break;
    }
    case WMTRenderCommandSetObjectBuffer: {
      struct wmtcmd_render_setbuffer *body = (struct wmtcmd_render_setbuffer *)next;
      [encoder setObjectBuffer:(id<MTLBuffer>)body->buffer offset:body->offset atIndex:body->index];
      break;
    }
    case WMTRenderCommandSetObjectBufferOffset: {
      struct wmtcmd_render_setbufferoffset *body = (struct wmtcmd_render_setbufferoffset *)next;
      [encoder setObjectBufferOffset:body->offset atIndex:body->index];
      break;
    }
    case WMTRenderCommandSetFragmentBytes: {
      struct wmtcmd_render_setbytes *body = (struct wmtcmd_render_setbytes *)next;
      [encoder setFragmentBytes:body->bytes.ptr length:body->length atIndex:body->index];
      break;
    }
    case WMTRenderCommandSetFragmentTexture: {
      struct wmtcmd_render_settexture *body = (struct wmtcmd_render_settexture *)next;
      [encoder setFragmentTexture:(id<MTLTexture>)body->texture atIndex:body->index];
      break;
    }
    case WMTRenderCommandSetRasterizerState: {
      struct wmtcmd_render_setrasterizerstate *body = (struct wmtcmd_render_setrasterizerstate *)next;
#if defined(DXMT_NATIVE_RASTER_SHADOW) && DXMT_NATIVE_RASTER_SHADOW
      if (!raster_shadow || raster_shadow->fill_mode != body->fill_mode)
        [encoder setTriangleFillMode:(MTLTriangleFillMode)body->fill_mode];
      if (!raster_shadow || raster_shadow->cull_mode != body->cull_mode)
        [encoder setCullMode:(MTLCullMode)body->cull_mode];
      if (!raster_shadow || raster_shadow->depth_clip_mode != body->depth_clip_mode)
        [encoder setDepthClipMode:(MTLDepthClipMode)body->depth_clip_mode];
      // Compare float representations individually: keep signed zero/NaN bits,
      // without interpreting ABI padding, links, or reserved command bytes.
      if (!raster_shadow || memcmp(&raster_shadow->depth_bias, &body->depth_bias, sizeof(body->depth_bias)) ||
          memcmp(&raster_shadow->scole_scale, &body->scole_scale, sizeof(body->scole_scale)) ||
          memcmp(&raster_shadow->depth_bias_clamp, &body->depth_bias_clamp, sizeof(body->depth_bias_clamp)))
        [encoder setDepthBias:body->depth_bias slopeScale:body->scole_scale clamp:body->depth_bias_clamp];
      if (!raster_shadow || raster_shadow->winding != body->winding)
        [encoder setFrontFacingWinding:(MTLWinding)body->winding];
      raster_shadow = body;
#else
      [encoder setTriangleFillMode:(MTLTriangleFillMode)body->fill_mode];
      [encoder setCullMode:(MTLCullMode)body->cull_mode];
      [encoder setDepthClipMode:(MTLDepthClipMode)body->depth_clip_mode];
      [encoder setDepthBias:body->depth_bias slopeScale:body->scole_scale clamp:body->depth_bias_clamp];
      [encoder setFrontFacingWinding:(MTLWinding)body->winding];
#endif
      break;
    }
    case WMTRenderCommandSetViewports: {
      struct wmtcmd_render_setviewports *body = (struct wmtcmd_render_setviewports *)next;
      [encoder setViewports:(const MTLViewport *)body->viewports.ptr count:body->viewport_count];
      break;
    }
    case WMTRenderCommandSetScissorRects: {
      struct wmtcmd_render_setscissorrects *body = (struct wmtcmd_render_setscissorrects *)next;
      [encoder setScissorRects:(const MTLScissorRect *)body->scissor_rects.ptr count:body->rect_count];
      break;
    }
    case WMTRenderCommandSetPSO: {
      struct wmtcmd_render_setpso *body = (struct wmtcmd_render_setpso *)next;
      [encoder setRenderPipelineState:(id<MTLRenderPipelineState>)body->pso];
      break;
    }
    case WMTRenderCommandSetDSSO: {
      struct wmtcmd_render_setdsso *body = (struct wmtcmd_render_setdsso *)next;
      [encoder setDepthStencilState:(id<MTLDepthStencilState>)body->dsso];
      [encoder setStencilReferenceValue:body->stencil_ref];
      break;
    }
    case WMTRenderCommandSetDepthStencilState: {
      struct wmtcmd_render_setdepthstencilstate *body = (struct wmtcmd_render_setdepthstencilstate *)next;
      [encoder setDepthStencilState:(id<MTLDepthStencilState>)body->depth_stencil_state];
      break;
    }
    case WMTRenderCommandSetBlendFactorAndStencilRef: {
      struct wmtcmd_render_setblendcolor *body = (struct wmtcmd_render_setblendcolor *)next;
      [encoder setBlendColorRed:body->red green:body->green blue:body->blue alpha:body->alpha];
      [encoder setStencilReferenceValue:body->stencil_ref];
      break;
    }
    case WMTRenderCommandSetBlendFactor: {
      struct wmtcmd_render_setblendcolor *body = (struct wmtcmd_render_setblendcolor *)next;
      [encoder setBlendColorRed:body->red green:body->green blue:body->blue alpha:body->alpha];
      break;
    }
    case WMTRenderCommandSetStencilRef: {
      struct wmtcmd_render_setstencilref *body = (struct wmtcmd_render_setstencilref *)next;
      [encoder setStencilReferenceValue:body->stencil_ref];
      break;
    }
    case WMTRenderCommandSetVisibilityMode: {
      struct wmtcmd_render_setvisibilitymode *body = (struct wmtcmd_render_setvisibilitymode *)next;
      [encoder setVisibilityResultMode:(MTLVisibilityResultMode)body->mode offset:body->offset];
      break;
    }
    case WMTRenderCommandDraw: {
      struct wmtcmd_render_draw *body = (struct wmtcmd_render_draw *)next;
      [encoder drawPrimitives:(MTLPrimitiveType)body->primitive_type
                  vertexStart:body->vertex_start
                  vertexCount:body->vertex_count
                instanceCount:body->instance_count
                 baseInstance:body->base_instance];
      break;
    }
    case WMTRenderCommandDrawIndexed: {
      struct wmtcmd_render_draw_indexed *body = (struct wmtcmd_render_draw_indexed *)next;
      [encoder drawIndexedPrimitives:(MTLPrimitiveType)body->primitive_type
                          indexCount:body->index_count
                           indexType:(MTLIndexType)body->index_type
                         indexBuffer:(id<MTLBuffer>)body->index_buffer
                   indexBufferOffset:body->index_buffer_offset
                       instanceCount:body->instance_count
                          baseVertex:body->base_vertex
                        baseInstance:body->base_instance];
      break;
    }
    case WMTRenderCommandDrawIndirect: {
      struct wmtcmd_render_draw_indirect *body = (struct wmtcmd_render_draw_indirect *)next;
      [encoder drawPrimitives:(MTLPrimitiveType)body->primitive_type
                indirectBuffer:(id<MTLBuffer>)body->indirect_args_buffer
          indirectBufferOffset:body->indirect_args_offset];
      break;
    }
    case WMTRenderCommandDrawIndexedIndirect: {
      struct wmtcmd_render_draw_indexed_indirect *body = (struct wmtcmd_render_draw_indexed_indirect *)next;
      [encoder drawIndexedPrimitives:(MTLPrimitiveType)body->primitive_type
                           indexType:(MTLIndexType)body->index_type
                         indexBuffer:(id<MTLBuffer>)body->index_buffer
                   indexBufferOffset:body->index_buffer_offset
                      indirectBuffer:(id<MTLBuffer>)body->indirect_args_buffer
                indirectBufferOffset:body->indirect_args_offset];
      break;
    }
    case WMTRenderCommandDrawMeshThreadgroups: {
      struct wmtcmd_render_draw_meshthreadgroups *body = (struct wmtcmd_render_draw_meshthreadgroups *)next;
      [encoder drawMeshThreadgroups:MTLSizeMake(
                                        body->threadgroup_per_grid.width, body->threadgroup_per_grid.height,
                                        body->threadgroup_per_grid.depth
                                    )
          threadsPerObjectThreadgroup:MTLSizeMake(
                                          body->object_threadgroup_size.width, body->object_threadgroup_size.height,
                                          body->object_threadgroup_size.depth
                                      )
            threadsPerMeshThreadgroup:MTLSizeMake(
                                          body->mesh_threadgroup_size.width, body->mesh_threadgroup_size.height,
                                          body->mesh_threadgroup_size.depth
                                      )];
      break;
    }
    case WMTRenderCommandDrawMeshThreadgroupsIndirect: {
      struct wmtcmd_render_draw_meshthreadgroups_indirect *body =
          (struct wmtcmd_render_draw_meshthreadgroups_indirect *)next;
      [encoder drawMeshThreadgroupsWithIndirectBuffer:(id<MTLBuffer>)body->indirect_args_buffer
                                 indirectBufferOffset:body->indirect_args_offset
                          threadsPerObjectThreadgroup:MTLSizeMake(
                                                          body->object_threadgroup_size.width,
                                                          body->object_threadgroup_size.height,
                                                          body->object_threadgroup_size.depth
                                                      )
                            threadsPerMeshThreadgroup:MTLSizeMake(
                                                          body->mesh_threadgroup_size.width,
                                                          body->mesh_threadgroup_size.height,
                                                          body->mesh_threadgroup_size.depth
                                                      )];
      break;
    }
    case WMTRenderCommandMemoryBarrier: {
      struct wmtcmd_render_memory_barrier *body = (struct wmtcmd_render_memory_barrier *)next;
      [encoder memoryBarrierWithScope:(MTLBarrierScope)body->scope
                          afterStages:(MTLRenderStages)body->stages_after
                         beforeStages:(MTLRenderStages)body->stages_before];
      break;
    }
    case WMTRenderCommandDXMTGeometryDraw: {
      struct wmtcmd_render_dxmt_geometry_draw *body = (struct wmtcmd_render_dxmt_geometry_draw *)next;
      [encoder setObjectBufferOffset:body->draw_arguments_offset atIndex:SM50_BINDING_INDEX_DRAW_ARGUMENTS];
      [encoder drawMeshThreadgroups:MTLSizeMake(body->warp_count, body->instance_count, 1)
          threadsPerObjectThreadgroup:MTLSizeMake(body->vertex_per_warp, 1, 1)
            threadsPerMeshThreadgroup:MTLSizeMake(1, 1, 1)];
      break;
    }
    case WMTRenderCommandDXMTGeometryDrawIndexed: {
      struct wmtcmd_render_dxmt_geometry_draw_indexed *body = (struct wmtcmd_render_dxmt_geometry_draw_indexed *)next;
      [encoder setObjectBuffer:(id<MTLBuffer>)body->index_buffer
                        offset:body->index_buffer_offset
                       atIndex:SM50_BINDING_INDEX_INDEX_BUFFER];
      [encoder setObjectBufferOffset:body->draw_arguments_offset atIndex:SM50_BINDING_INDEX_DRAW_ARGUMENTS];
      [encoder drawMeshThreadgroups:MTLSizeMake(body->warp_count, body->instance_count, 1)
          threadsPerObjectThreadgroup:MTLSizeMake(body->vertex_per_warp, 1, 1)
            threadsPerMeshThreadgroup:MTLSizeMake(1, 1, 1)];
      break;
    }
    case WMTRenderCommandDXMTGeometryDrawIndirect: {
      struct wmtcmd_render_dxmt_geometry_draw_indirect *body = (struct wmtcmd_render_dxmt_geometry_draw_indirect *)next;
      [encoder setObjectBuffer:(id<MTLBuffer>)body->indirect_args_buffer
                        offset:body->indirect_args_offset
                       atIndex:SM50_BINDING_INDEX_INDIRECT_ARGUMENTS];
      [encoder drawMeshThreadgroupsWithIndirectBuffer:(id<MTLBuffer>)body->dispatch_args_buffer
                                 indirectBufferOffset:body->dispatch_args_offset
                          threadsPerObjectThreadgroup:MTLSizeMake(body->vertex_per_warp, 1, 1)
                            threadsPerMeshThreadgroup:MTLSizeMake(1, 1, 1)];
      [encoder setObjectBuffer:(id<MTLBuffer>)body->imm_draw_arguments
                        offset:0
                       atIndex:SM50_BINDING_INDEX_DRAW_ARGUMENTS];
      break;
    }
    case WMTRenderCommandDXMTGeometryDrawIndexedIndirect: {
      struct wmtcmd_render_dxmt_geometry_draw_indexed_indirect *body =
          (struct wmtcmd_render_dxmt_geometry_draw_indexed_indirect *)next;
      [encoder setObjectBuffer:(id<MTLBuffer>)body->index_buffer
                        offset:body->index_buffer_offset
                       atIndex:SM50_BINDING_INDEX_INDEX_BUFFER];
      [encoder setObjectBuffer:(id<MTLBuffer>)body->indirect_args_buffer
                        offset:body->indirect_args_offset
                       atIndex:SM50_BINDING_INDEX_INDIRECT_ARGUMENTS];
      [encoder drawMeshThreadgroupsWithIndirectBuffer:(id<MTLBuffer>)body->dispatch_args_buffer
                                 indirectBufferOffset:body->dispatch_args_offset
                          threadsPerObjectThreadgroup:MTLSizeMake(body->vertex_per_warp, 1, 1)
                            threadsPerMeshThreadgroup:MTLSizeMake(1, 1, 1)];
      [encoder setObjectBuffer:(id<MTLBuffer>)body->imm_draw_arguments
                        offset:0
                       atIndex:SM50_BINDING_INDEX_DRAW_ARGUMENTS];
      break;
    }
    case WMTRenderCommandDXMTTessellationMeshDraw: {
      struct wmtcmd_render_dxmt_tessellation_mesh_draw *body = (struct wmtcmd_render_dxmt_tessellation_mesh_draw *)next;
      [encoder setObjectBufferOffset:body->draw_arguments_offset atIndex:SM50_BINDING_INDEX_DRAW_ARGUMENTS];
      [encoder drawMeshThreadgroups:MTLSizeMake(body->patch_per_mesh_instance, body->instance_count, 1)
          threadsPerObjectThreadgroup:MTLSizeMake(body->threads_per_patch, body->patch_per_group, 1)
            threadsPerMeshThreadgroup:MTLSizeMake(32, 1, 1)];
      break;
    }
    case WMTRenderCommandDXMTTessellationMeshDrawIndexed: {
      struct wmtcmd_render_dxmt_tessellation_mesh_draw_indexed *body =
          (struct wmtcmd_render_dxmt_tessellation_mesh_draw_indexed *)next;
      [encoder setObjectBuffer:(id<MTLBuffer>)body->index_buffer
                        offset:body->index_buffer_offset
                       atIndex:SM50_BINDING_INDEX_INDEX_BUFFER];
      [encoder setObjectBufferOffset:body->draw_arguments_offset atIndex:SM50_BINDING_INDEX_DRAW_ARGUMENTS];
      [encoder drawMeshThreadgroups:MTLSizeMake(body->patch_per_mesh_instance, body->instance_count, 1)
          threadsPerObjectThreadgroup:MTLSizeMake(body->threads_per_patch, body->patch_per_group, 1)
            threadsPerMeshThreadgroup:MTLSizeMake(32, 1, 1)];
      break;
    }

    case WMTRenderCommandDXMTTessellationMeshDrawIndirect: {
      struct wmtcmd_render_dxmt_tessellation_mesh_draw_indirect *body = (struct wmtcmd_render_dxmt_tessellation_mesh_draw_indirect *)next;
      [encoder setObjectBuffer:(id<MTLBuffer>)body->indirect_args_buffer
                        offset:body->indirect_args_offset
                       atIndex:SM50_BINDING_INDEX_INDIRECT_ARGUMENTS];
      [encoder drawMeshThreadgroupsWithIndirectBuffer:(id<MTLBuffer>)body->dispatch_args_buffer
                                 indirectBufferOffset:body->dispatch_args_offset
                          threadsPerObjectThreadgroup:MTLSizeMake(body->threads_per_patch, body->patch_per_group, 1)
                            threadsPerMeshThreadgroup:MTLSizeMake(32, 1, 1)];
      [encoder setObjectBuffer:(id<MTLBuffer>)body->imm_draw_arguments
                        offset:0
                       atIndex:SM50_BINDING_INDEX_DRAW_ARGUMENTS];
      break;
    }
    case WMTRenderCommandDXMTTessellationMeshDrawIndexedIndirect: {
      struct wmtcmd_render_dxmt_tessellation_mesh_draw_indexed_indirect *body =
          (struct wmtcmd_render_dxmt_tessellation_mesh_draw_indexed_indirect *)next;
      [encoder setObjectBuffer:(id<MTLBuffer>)body->index_buffer
                        offset:body->index_buffer_offset
                       atIndex:SM50_BINDING_INDEX_INDEX_BUFFER];
      [encoder setObjectBuffer:(id<MTLBuffer>)body->indirect_args_buffer
                        offset:body->indirect_args_offset
                       atIndex:SM50_BINDING_INDEX_INDIRECT_ARGUMENTS];
      [encoder drawMeshThreadgroupsWithIndirectBuffer:(id<MTLBuffer>)body->dispatch_args_buffer
                                 indirectBufferOffset:body->dispatch_args_offset
                          threadsPerObjectThreadgroup:MTLSizeMake(body->threads_per_patch, body->patch_per_group, 1)
                            threadsPerMeshThreadgroup:MTLSizeMake(32, 1, 1)];
      [encoder setObjectBuffer:(id<MTLBuffer>)body->imm_draw_arguments
                        offset:0
                       atIndex:SM50_BINDING_INDEX_DRAW_ARGUMENTS];
      break;
    }
    case WMTRenderCommandUpdateFence: {
      struct wmtcmd_render_fence_op *body = (struct wmtcmd_render_fence_op *)next;
      [encoder updateFence:(id<MTLFence>)body->fence afterStages:(MTLRenderStages)body->stages];
      break;
    }
    case WMTRenderCommandWaitForFence: {
      struct wmtcmd_render_fence_op *body = (struct wmtcmd_render_fence_op *)next;
      [encoder waitForFence:(id<MTLFence>)body->fence beforeStages:(MTLRenderStages)body->stages];
      break;
    }
    case WMTRenderCommandSetViewport: {
      struct wmtcmd_render_setviewport *body = (struct wmtcmd_render_setviewport *)next;
      union {
        struct WMTViewport src;
        MTLViewport dst;
      } u = {.src = body->viewport};
      [encoder setViewport:u.dst];
      break;
    }
    case WMTRenderCommandSetScissorRect: {
      struct wmtcmd_render_setscissorrect *body = (struct wmtcmd_render_setscissorrect *)next;
      union {
        struct WMTScissorRect src;
        MTLScissorRect dst;
      } u = {.src = body->scissor_rect};
      [encoder setScissorRect:u.dst];
      break;
    }
    case WMTRenderCommandDispatchThreadsPerTile: {
      struct wmtcmd_render_dispatch_threads_per_tile *body = (struct wmtcmd_render_dispatch_threads_per_tile *)next;
      [encoder dispatchThreadsPerTile:MTLSizeMake(body->width, body->height, 1)];
      break;
    }
    case WMTRenderCommandExecuteCommandsInBuffer: {
#if defined(DXMT_NATIVE_RASTER_SHADOW) && DXMT_NATIVE_RASTER_SHADOW
      // An ICB can carry raster state on supported devices; its contents are
      // not represented in this CPU list. Force the next full raster setup.
      raster_shadow = NULL;
#endif
      struct wmtcmd_render_executecommands *body = (struct wmtcmd_render_executecommands *)next;
      [encoder executeCommandsInBuffer:(id<MTLIndirectCommandBuffer>)body->indirect_command_buffer
                             withRange:NSMakeRange(body->location, body->length)];
      break;
    }
    }
    next = next->next.ptr;
  }
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLTexture_pixelFormat(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [(id<MTLTexture>)params->handle pixelFormat];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLTexture_width(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [(id<MTLTexture>)params->handle width];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLTexture_height(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [(id<MTLTexture>)params->handle height];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLTexture_depth(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [(id<MTLTexture>)params->handle depth];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLTexture_arrayLength(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [(id<MTLTexture>)params->handle arrayLength];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLTexture_mipmapLevelCount(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [(id<MTLTexture>)params->handle mipmapLevelCount];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLTexture_replaceRegion(void *obj) {
  struct unixcall_mtltexture_replaceregion *params = obj;
  [(id<MTLTexture>)params->texture replaceRegion:MTLRegionMake3D(
                                                     params->origin.x, params->origin.y, params->origin.z,
                                                     params->size.width, params->size.height, params->size.depth
                                                 )
                                     mipmapLevel:params->level
                                           slice:params->slice
                                       withBytes:params->data.ptr
                                     bytesPerRow:params->bytes_per_row
                                   bytesPerImage:params->bytes_per_image];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLBuffer_didModifyRange(void *obj) {
  struct unixcall_generic_obj_uint64_uint64_ret *params = obj;
  [(id<MTLBuffer>)params->handle didModifyRange:NSMakeRange(params->arg, params->ret)];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandBuffer_presentDrawable(void *obj) {
  struct unixcall_generic_obj_obj_noret *params = obj;
  [(id<MTLCommandBuffer>)params->handle presentDrawable:(id<MTLDrawable>)params->arg];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandBuffer_presentDrawableAfterMinimumDuration(void *obj) {
  struct unixcall_generic_obj_obj_double_noret *params = obj;
  [(id<MTLCommandBuffer>)params->handle presentDrawable:(id<MTLDrawable>)params->arg0
                                   afterMinimumDuration:params->arg1];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_supportsFamily(void *obj) {
  struct unixcall_generic_obj_uint64_uint64_ret *params = obj;
  params->ret = [(id<MTLDevice>)params->handle supportsFamily:(MTLGPUFamily)params->arg];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_supportsBCTextureCompression(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [(id<MTLDevice>)params->handle supportsBCTextureCompression];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_supportsTextureSampleCount(void *obj) {
  struct unixcall_generic_obj_uint64_uint64_ret *params = obj;
  params->ret = [(id<MTLDevice>)params->handle supportsTextureSampleCount:params->arg];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_hasUnifiedMemory(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [(id<MTLDevice>)params->handle hasUnifiedMemory];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCaptureManager_sharedCaptureManager(void *obj) {
  struct unixcall_generic_obj_ret *params = obj;
  params->ret = (obj_handle_t)[MTLCaptureManager sharedCaptureManager];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCaptureManager_startCapture(void *obj) {
  struct unixcall_mtlcapturemanager_startcapture *params = obj;
  params->ret = false;
  MTLCaptureManager *manager = (MTLCaptureManager *)params->capture_manager;
  const struct WMTCaptureInfo *info = params->info.ptr;
  // Never take ownership of a capture started by Xcode or another caller.
  if (!manager || !info || !info->capture_object || !info->output_url.ptr ||
      manager.isCapturing || ![manager supportsDestination:(MTLCaptureDestination)info->destination])
    return STATUS_SUCCESS;
  MTLCaptureDescriptor *desc = [[MTLCaptureDescriptor alloc] init];
  desc.destination = (MTLCaptureDestination)info->destination;
  desc.captureObject = (id)info->capture_object;
  NSString *path_str = [[NSString alloc] initWithCString:info->output_url.ptr encoding:NSUTF8StringEncoding];
  NSURL *url = [[NSURL alloc] initFileURLWithPath:path_str];
  desc.outputURL = url;
  NSError *error = nil;
  params->ret = [manager startCaptureWithDescriptor:desc error:&error];
  [url release];
  [path_str release];
  [desc release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLAllocation_allocatedSize(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  id<MTLAllocation> allocation = (id<MTLAllocation>)params->handle;
  params->ret = allocation ? allocation.allocatedSize : 0;
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCaptureManager_stopCapture(void *obj) {
  struct unixcall_generic_obj_noret *params = obj;
  [(MTLCaptureManager *)params->handle stopCapture];
  return STATUS_SUCCESS;
}

#include <signal.h>

void
temp_handler(int signum) {
  fprintf(stderr, "received signal %d in temp_handler(), and it may cause problem!\n", signum);
}

static const int SIGNALS[] = {
    SIGHUP,
    SIGINT,
    SIGTERM,
    SIGUSR2,
    SIGILL,
    SIGTRAP,
    SIGABRT,
    SIGFPE,
    SIGBUS,
    SIGSEGV,
    SIGQUIT
#ifdef SIGSYS
    ,
    SIGSYS
#endif
#ifdef SIGXCPU
    ,
    SIGXCPU
#endif
#ifdef SIGXFSZ
    ,
    SIGXFSZ
#endif
#ifdef SIGEMT
    ,
    SIGEMT
#endif
    ,
    SIGUSR1
#ifdef SIGINFO
    ,
    SIGINFO
#endif
};

static NTSTATUS
_MTLDevice_newTemporalScaler(void *obj) {
  struct unixcall_mtldevice_newfxtemporalscaler *params = obj;
  MTLFXTemporalScalerDescriptor *desc = [[MTLFXTemporalScalerDescriptor alloc] init];
  const struct WMTFXTemporalScalerInfo *info = params->info.ptr;
  desc.colorTextureFormat = to_metal_pixel_format(info->color_format);
  desc.outputTextureFormat = to_metal_pixel_format(info->output_format);
  desc.depthTextureFormat = to_metal_pixel_format(info->depth_format);
  desc.motionTextureFormat = to_metal_pixel_format(info->motion_format);
  desc.inputWidth = info->input_width;
  desc.inputHeight = info->input_height;
  desc.outputWidth = info->output_width;
  desc.outputHeight = info->output_height;
  desc.inputContentMaxScale = info->input_content_max_scale;
  desc.inputContentMinScale = info->input_content_min_scale;
  desc.inputContentPropertiesEnabled = info->input_content_properties_enabled;
  #if __MAC_OS_X_VERSION_MAX_ALLOWED >= 150000
  if (@available(macOS 15, *)) {
    desc.requiresSynchronousInitialization = info->requires_synchronous_initialization;
  }
  #endif
  desc.autoExposureEnabled = info->auto_exposure;

  struct sigaction old_action[sizeof(SIGNALS) / sizeof(int)], new_action;
  if (@available(macOS 16, *)) {} else {
    new_action.sa_handler = temp_handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;
    for (unsigned int i = 0; i < sizeof(SIGNALS) / sizeof(int); i++)
      sigaction(SIGNALS[i], &new_action, &old_action[i]);
  }

  params->ret = (obj_handle_t)[desc newTemporalScalerWithDevice:(id<MTLDevice>)params->device];

  if (@available(macOS 16, *)) {} else {
    for (unsigned int i = 0; i < sizeof(SIGNALS) / sizeof(int); i++)
      sigaction(SIGNALS[i], &old_action[i], NULL);
  }

  [desc release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newSpatialScaler(void *obj) {
  struct unixcall_mtldevice_newfxspatialscaler *params = obj;
  MTLFXSpatialScalerDescriptor *desc = [[MTLFXSpatialScalerDescriptor alloc] init];
  const struct WMTFXSpatialScalerInfo *info = params->info.ptr;
  desc.colorTextureFormat = to_metal_pixel_format(info->color_format);
  desc.outputTextureFormat = to_metal_pixel_format(info->output_format);
  desc.inputWidth = info->input_width;
  desc.inputHeight = info->input_height;
  desc.outputWidth = info->output_width;
  desc.outputHeight = info->output_height;
  params->ret = (obj_handle_t)[desc newSpatialScalerWithDevice:(id<MTLDevice>)params->device];
  [desc release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandBuffer_encodeTemporalScale(void *obj) {
  struct unixcall_mtlcommandbuffer_temporal_scale *params = obj;
  id<MTLCommandBuffer> cmdbuf = (id<MTLCommandBuffer>)params->cmdbuf;
  id<MTLFXTemporalScaler> scaler = (id<MTLFXTemporalScaler>)params->scaler;
  scaler.colorTexture = (id<MTLTexture>)params->color;
  scaler.outputTexture = (id<MTLTexture>)params->output;
  scaler.depthTexture = (id<MTLTexture>)params->depth;
  scaler.motionTexture = (id<MTLTexture>)params->motion;
  scaler.exposureTexture = (id<MTLTexture>)params->exposure;
  scaler.fence = (id<MTLFence>)params->fence;
  const struct WMTFXTemporalScalerProps *props = params->props.ptr;
  scaler.inputContentWidth = props->input_content_width;
  scaler.inputContentHeight = props->input_content_height;
  scaler.reset = props->reset;
  scaler.depthReversed = props->depth_reversed;
  scaler.motionVectorScaleX = props->motion_vector_scale_x;
  scaler.motionVectorScaleY = props->motion_vector_scale_y;
  scaler.jitterOffsetX = props->jitter_offset_x;
  scaler.jitterOffsetY = props->jitter_offset_y;
  scaler.preExposure = props->pre_exposure;
  [scaler encodeToCommandBuffer:cmdbuf];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandBuffer_encodeSpatialScale(void *obj) {
  struct unixcall_mtlcommandbuffer_spatial_scale *params = obj;
  id<MTLCommandBuffer> cmdbuf = (id<MTLCommandBuffer>)params->cmdbuf;
  id<MTLFXSpatialScaler> scaler = (id<MTLFXSpatialScaler>)params->scaler;
  scaler.colorTexture = (id<MTLTexture>)params->color;
  scaler.outputTexture = (id<MTLTexture>)params->output;
  scaler.fence = (id<MTLFence>)params->fence;
  [scaler encodeToCommandBuffer:cmdbuf];
  return STATUS_SUCCESS;
}

static NTSTATUS
_NSString_string(void *obj) {
  struct unixcall_nsstring_string *params = obj;
  NSString *str = [NSString stringWithCString:params->buffer_ptr.ptr encoding:(NSStringEncoding)params->encoding];
  params->ret = (obj_handle_t)str;
  return STATUS_SUCCESS;
}

static NTSTATUS
_NSString_alloc_init(void *obj) {
  struct unixcall_nsstring_string *params = obj;
  NSString *str = [[NSString alloc] initWithCString:params->buffer_ptr.ptr encoding:(NSStringEncoding)params->encoding];
  params->ret = (obj_handle_t)str;
  return STATUS_SUCCESS;
}

static bool
developer_hud_uses_metric_service(void) {
  NSOperatingSystemVersion version = [NSProcessInfo processInfo].operatingSystemVersion;
  return version.majorVersion >= 27;
}

static NTSTATUS
_DeveloperHUDProperties_instance(void *obj) {
  struct unixcall_generic_obj_ret *params = obj;
  Class cls = objc_lookUpClass(
      developer_hud_uses_metric_service()
          ? "MTLHUDService"
          : "_CADeveloperHUDProperties"
  );
  params->ret =
      (obj_handle_t)((id(*)(id, SEL))objc_msgSend)((id)cls, @selector(instance));
  return STATUS_SUCCESS;
}

static NTSTATUS
_DeveloperHUDProperties_addLabel(void *obj) {
  struct unixcall_generic_obj_obj_obj_uint64_ret *params = obj;
  if (developer_hud_uses_metric_service()) {
    params->ret =
        ((bool (*)(id, SEL, id, id, id, id, uint32_t, uint32_t, uint32_t, uint64_t))objc_msgSend)(
            (id)params->handle,
            @selector(insertMetric:after:name:unit:nameColor:valueColor:visualType:options:),
            (id)params->arg0,
            (id)params->arg1,
            @"",
            @"",
            UINT32_MAX,
            UINT32_MAX,
            1u,
            0u
        );
  } else {
    params->ret =
        ((bool (*)(id, SEL, id, id))objc_msgSend)(
            (id)params->handle,
            @selector(addLabel:after:),
            (id)params->arg0,
            (id)params->arg1
        );
  }
  return STATUS_SUCCESS;
}

static NTSTATUS
_DeveloperHUDProperties_updateLabel(void *obj) {
  struct unixcall_generic_obj_obj_obj_noret *params = obj;
  ((void (*)(id, SEL, id, id)
  )objc_msgSend)((id)params->handle, @selector(updateLabel:value:), (id)params->arg0, (id)params->arg1);
  return STATUS_SUCCESS;
}

static NTSTATUS
_DeveloperHUDProperties_remove(void *obj) {
  struct unixcall_generic_obj_obj_noret *params = obj;
  ((void (*)(id, SEL, id))objc_msgSend)((id)params->handle, @selector(remove:), (id)params->arg);
  return STATUS_SUCCESS;
}

static NTSTATUS
_MetalDrawable_texture(void *obj) {
  struct unixcall_generic_obj_obj_ret *params = obj;
  params->ret = (obj_handle_t)[(id<CAMetalDrawable>)params->handle texture];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MetalLayer_nextDrawable(void *obj) {
  struct unixcall_generic_obj_obj_ret *params = obj;
  if (w7ds_should_skip_drawable((CAMetalLayer *)params->handle)) {
    params->ret = 0;
    w7ad_skip_occluded();
    return STATUS_SUCCESS;
  }
  struct W7ADSample sample = w7ad_begin();
  params->ret = (obj_handle_t)[(CAMetalLayer *)params->handle nextDrawable];
  w7ad_end(sample, (uintptr_t)params->handle, (uintptr_t)params->ret);
  if (params->ret) w7ds_did_acquire_drawable((CAMetalLayer *)params->handle);
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_supportsFXSpatialScaler(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [MTLFXSpatialScalerDescriptor supportsDevice:(id<MTLDevice>)params->handle];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_supportsFXTemporalScaler(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [MTLFXTemporalScalerDescriptor supportsDevice:(id<MTLDevice>)params->handle];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MetalLayer_setProps(void *obj) {
  struct unixcall_generic_obj_constptr_noret *params = obj;
  CAMetalLayer *layer = (CAMetalLayer *)params->handle;
  const struct WMTLayerProps *props = params->arg.ptr;
  bool applied = execute_on_main_checked(^{
    layer.device = (id<MTLDevice>)props->device;
    layer.opaque = props->opaque;
    layer.framebufferOnly = props->framebuffer_only;
    layer.contentsScale = props->contents_scale;
    layer.displaySyncEnabled = props->display_sync_enabled;
    layer.drawableSize = CGSizeMake(props->drawable_width, props->drawable_height);
    layer.pixelFormat = to_metal_pixel_format(props->pixel_format);
    w7ds_apply_aspect_fit_main(layer);
    w7ds_refresh_layer_main(layer);
  });
  return applied ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}



typedef struct macdrv_opaque_metal_device *macdrv_metal_device;
typedef struct macdrv_opaque_metal_view *macdrv_metal_view;
typedef struct macdrv_opaque_metal_layer *macdrv_metal_layer;
typedef struct macdrv_opaque_view *macdrv_view;
typedef struct macdrv_opaque_window *macdrv_window;
typedef struct macdrv_opaque_window_data *macdrv_window_data;
typedef struct opaque_window_surface *window_surface;
typedef struct opaque_HWND *HWND;
struct macdrv_win_data {
  HWND hwnd; /* hwnd that this private data belongs to */
  macdrv_window cocoa_window;
  macdrv_view cocoa_view;
  macdrv_view client_cocoa_view;
};

struct macdrv_functions_t {
  void (*macdrv_init_display_devices)(BOOL);
  struct macdrv_win_data *(*get_win_data)(HWND hwnd);
  void (*release_win_data)(struct macdrv_win_data *data);
  macdrv_window (*macdrv_get_cocoa_window)(HWND hwnd, BOOL require_on_screen);
  macdrv_metal_device (*macdrv_create_metal_device)(void);
  void (*macdrv_release_metal_device)(macdrv_metal_device d);
  macdrv_metal_view (*macdrv_view_create_metal_view)(macdrv_view v, macdrv_metal_device d);
  macdrv_metal_layer (*macdrv_view_get_metal_layer)(macdrv_metal_view v);
  void (*macdrv_view_release_metal_view)(macdrv_metal_view v);
  void (*on_main_thread)(dispatch_block_t b);
};

static NTSTATUS
_CreateMetalViewFromHWND(void *obj) {
  struct unixcall_create_metal_view_from_hwnd *params = obj;
  /* Outputs are an all-or-nothing lease; failure must never expose stale
     handles left in a reused call structure. */
  params->ret_view = 0;
  params->ret_layer = 0;

  struct macdrv_win_data *(*pfn_get_win_data)(HWND hwnd) = NULL;
  void (*pfn_release_win_data)(struct macdrv_win_data *data) = NULL;
  macdrv_metal_view (*pfn_macdrv_view_create_metal_view)(macdrv_view v, macdrv_metal_device d) = NULL;
  macdrv_metal_layer (*pfn_macdrv_view_get_metal_layer)(macdrv_metal_view v) = NULL;

  void (*pfn_macdrv_view_release_metal_view)(macdrv_metal_view v) = NULL;
  struct macdrv_functions_t *macdrv_functions;
  if ((macdrv_functions = dlsym(RTLD_DEFAULT, "macdrv_functions"))) {
    pfn_get_win_data = macdrv_functions->get_win_data;
    pfn_release_win_data = macdrv_functions->release_win_data;
    pfn_macdrv_view_create_metal_view = macdrv_functions->macdrv_view_create_metal_view;
    pfn_macdrv_view_get_metal_layer = macdrv_functions->macdrv_view_get_metal_layer;
    pfn_macdrv_view_release_metal_view = macdrv_functions->macdrv_view_release_metal_view;
  } else {
    pfn_get_win_data = dlsym(RTLD_DEFAULT, "get_win_data");
    pfn_release_win_data = dlsym(RTLD_DEFAULT, "release_win_data");
    pfn_macdrv_view_create_metal_view = dlsym(RTLD_DEFAULT, "macdrv_view_create_metal_view");
    pfn_macdrv_view_get_metal_layer = dlsym(RTLD_DEFAULT, "macdrv_view_get_metal_layer");
    pfn_macdrv_view_release_metal_view = dlsym(RTLD_DEFAULT, "macdrv_view_release_metal_view");
  }

  if (pfn_get_win_data && pfn_release_win_data && pfn_macdrv_view_create_metal_view &&
      pfn_macdrv_view_get_metal_layer && pfn_macdrv_view_release_metal_view) {
    struct macdrv_win_data *win_data = pfn_get_win_data((HWND)params->hwnd);
    if (!win_data)
      return STATUS_SUCCESS;

    macdrv_metal_view view = win_data->client_cocoa_view ?
        pfn_macdrv_view_create_metal_view(win_data->client_cocoa_view, (macdrv_metal_device)params->device) : NULL;
    if (view) {
      /* Native getter marshals through Wine OnMainThread itself. Never call
       * it from inside a main block: that dispatcher has no inline guard. */
      __block macdrv_metal_layer layer = pfn_macdrv_view_get_metal_layer(view);
      if (!execute_on_main_checked(^{
        if (layer && !w7ds_prime_layer_main((CAMetalLayer *)layer))
          layer = NULL;
      })) layer = NULL;
      if (layer) {
        params->ret_view = (obj_handle_t)view;
        params->ret_layer = (obj_handle_t)layer;
      } else {
        pfn_macdrv_view_release_metal_view(view);
      }
    }
    pfn_release_win_data(win_data);
  }

  return STATUS_SUCCESS;
}

static NTSTATUS
_ReleaseMetalView(void *obj) {
  struct unixcall_generic_obj_noret *params = obj;

  void (*pfn_macdrv_view_release_metal_view)(macdrv_metal_view v) = NULL;

  struct macdrv_functions_t *macdrv_functions;
  if ((macdrv_functions = dlsym(RTLD_DEFAULT, "macdrv_functions"))) {
    pfn_macdrv_view_release_metal_view = macdrv_functions->macdrv_view_release_metal_view;
  } else {
    pfn_macdrv_view_release_metal_view = dlsym(RTLD_DEFAULT, "macdrv_view_release_metal_view");
  }

  if (pfn_macdrv_view_release_metal_view)
    pfn_macdrv_view_release_metal_view((macdrv_metal_view)params->handle);

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk_SM50Initialize(void *args) {
  struct sm50_initialize_params *params = args;

  params->ret =
      SM50Initialize(params->bytecode, params->bytecode_size, params->shader, params->reflection, params->error);

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk_SM50Destroy(void *args) {
  struct sm50_destroy_params *params = args;

  SM50Destroy(params->shader);

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk_SM50Compile(void *args) {
  struct sm50_compile_params *params = args;

  params->ret = SM50Compile(params->shader, params->args, params->func_name, params->bitcode, params->error);

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk_SM50GetCompiledBitcode(void *args) {
  struct sm50_get_compiled_bitcode_params *params = args;

  SM50GetCompiledBitcode(params->bitcode, params->data_out);

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk_SM50DestroyBitcode(void *args) {
  struct sm50_destroy_bitcode_params *params = args;

  SM50DestroyBitcode(params->bitcode);

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk_SM50GetErrorMessage(void *args) {
  struct sm50_get_error_message_params *params = args;

  params->ret_size = SM50GetErrorMessage(params->error, params->buffer, params->buffer_size);

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk_SM50FreeError(void *args) {
  struct sm50_free_error_params *params = args;

  SM50FreeError(params->error);

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk_SM50CompileTessellationPipelineHull(void *args) {
  struct sm50_compile_tessellation_pipeline_hull_params *params = args;

  params->ret = SM50CompileTessellationPipelineHull(
      params->vertex, params->hull, params->hull_args, params->func_name, params->bitcode, params->error
  );

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk_SM50CompileTessellationPipelineDomain(void *args) {
  struct sm50_compile_tessellation_pipeline_domain_params *params = args;

  params->ret = SM50CompileTessellationPipelineDomain(
      params->hull, params->domain, params->domain_args, params->func_name, params->bitcode, params->error
  );

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk_SM50CompileGeometryPipelineVertex(void *args) {
  struct sm50_compile_geometry_pipeline_vertex_params *params = args;

  params->ret = SM50CompileGeometryPipelineVertex(
      params->vertex, params->geometry, params->vertex_args, params->func_name, params->bitcode, params->error
  );

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk_SM50CompileGeometryPipelineGeometry(void *args) {
  struct sm50_compile_geometry_pipeline_geometry_params *params = args;

  params->ret = SM50CompileGeometryPipelineGeometry(
      params->vertex, params->geometry, params->geometry_args, params->func_name, params->bitcode, params->error
  );

  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandEncoder_setLabel(void *args) {
  struct unixcall_generic_obj_obj_noret *params = args;
  [(id<MTLCommandEncoder>)params->handle setLabel:(NSString *)params->arg];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_setShouldMaximizeConcurrentCompilation(void *args) {
  struct unixcall_generic_obj_uint64_noret *params = args;
  [(id<MTLDevice>)params->handle setShouldMaximizeConcurrentCompilation:(BOOL)params->arg];
  return STATUS_SUCCESS;
}

static NTSTATUS
thunk_SM50GetArgumentsInfo(void *args) {
  struct sm50_get_arguments_info_params *params = args;
  SM50GetArgumentsInfo(params->shader, params->constant_buffers, params->arguments);
  return STATUS_SUCCESS;
}

static inline void *
UInt32ToPtr(uint32_t v) {
  return (void *)(uint64_t)v;
}

#ifndef DXMT_NATIVE

static NTSTATUS
thunk32_SM50Initialize(void *args) {
  struct sm50_initialize_params32 *params = args;

  params->ret = SM50Initialize(
      UInt32ToPtr(params->bytecode), params->bytecode_size, UInt32ToPtr(params->shader),
      UInt32ToPtr(params->reflection), UInt32ToPtr(params->error)
  );

  return STATUS_SUCCESS;
}

struct SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA32 {
  uint32_t next;
  enum SM50_SHADER_COMPILATION_ARGUMENT_TYPE type;
  uint32_t num_output_slots;
  uint32_t num_elements;
  uint32_t strides[4];
  uint32_t elements;
};

struct SM50_SHADER_COMMON_DATA32 {
  uint32_t next;
  enum SM50_SHADER_COMPILATION_ARGUMENT_TYPE type;
  enum SM50_SHADER_METAL_VERSION metal_version;
  enum SM50_SHADER_FLAG flag;
};

struct SM50_SHADER_COMPILATION_ARGUMENT_DATA32 {
  uint32_t next;
  enum SM50_SHADER_COMPILATION_ARGUMENT_TYPE type;
};

struct SM50_SHADER_IA_INPUT_LAYOUT_DATA32 {
  uint32_t next;
  enum SM50_SHADER_COMPILATION_ARGUMENT_TYPE type;
  enum SM50_INDEX_BUFFER_FORMAT index_buffer_format;
  uint32_t slot_mask;
  uint32_t num_elements;
  uint32_t elements;
};

struct SM50_SHADER_PSO_PIXEL_SHADER_DATA32 {
  uint32_t next;
  enum SM50_SHADER_COMPILATION_ARGUMENT_TYPE type;
  uint32_t sample_mask;
  bool dual_source_blending;
  bool disable_depth_output;
  uint32_t unorm_output_reg_mask;
  /** MTLPixelFormat */
  uint32_t pixel_formats[8];
};

struct SM50_SHADER_GS_PASS_THROUGH_DATA32 {
  uint32_t next;
  enum SM50_SHADER_COMPILATION_ARGUMENT_TYPE type;
  union {
    struct MTL_GEOMETRY_SHADER_PASS_THROUGH Data;
    uint32_t DataEncoded;
  };
  bool RasterizationDisabled;
};

struct SM50_SHADER_PSO_GEOMETRY_SHADER_DATA32 {
  uint32_t next;
  enum SM50_SHADER_COMPILATION_ARGUMENT_TYPE type;
  bool strip_topology;
};

struct SM50_SHADER_PSO_TESSELLATOR_DATA32 {
  uint32_t next;
  enum SM50_SHADER_COMPILATION_ARGUMENT_TYPE type;
  uint32_t max_potential_tess_factor;
};

struct SM50_SHADER_ROOT_SIGNATURE_DATA32 {
  uint32_t next;
  enum SM50_SHADER_COMPILATION_ARGUMENT_TYPE type;
  uint32_t bytecode;
  uint32_t bytecode_length;
};

void
sm50_compilation_argument32_convert(
    struct SM50_SHADER_COMPILATION_ARGUMENT_DATA *first_arg, struct SM50_SHADER_COMPILATION_ARGUMENT_DATA32 *args32
) {
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA *last_arg = first_arg;

  first_arg->type = SM50_SHADER_ARGUMENT_TYPE_MAX;
  first_arg->next = NULL;

  while (args32) {
    switch (args32->type) {
    case SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT: {
      struct SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA32 *src = (void *)args32;
      struct SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA *data =
          malloc(sizeof(struct SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA));
      last_arg->next = data;
      last_arg = (void *)data;
      last_arg->next = NULL;
      data->type = src->type;
      data->num_output_slots = src->num_output_slots;
      data->num_elements = src->num_elements;
      data->strides[0] = src->strides[0];
      data->strides[1] = src->strides[1];
      data->strides[2] = src->strides[2];
      data->strides[3] = src->strides[3];
      data->elements = UInt32ToPtr(src->elements);
      break;
    }
    case SM50_SHADER_COMMON: {
      struct SM50_SHADER_COMMON_DATA32 *src = (void *)args32;
      struct SM50_SHADER_COMMON_DATA *data = malloc(sizeof(struct SM50_SHADER_COMMON_DATA));
      last_arg->next = data;
      last_arg = (void *)data;
      last_arg->next = NULL;
      data->type = src->type;
      data->metal_version = src->metal_version;
      data->flags = src->flag;
      break;
    }
    case SM50_SHADER_PSO_PIXEL_SHADER: {
      struct SM50_SHADER_PSO_PIXEL_SHADER_DATA32 *src = (void *)args32;
      struct SM50_SHADER_PSO_PIXEL_SHADER_DATA *data = malloc(sizeof(struct SM50_SHADER_PSO_PIXEL_SHADER_DATA));
      last_arg->next = data;
      last_arg = (void *)data;
      last_arg->next = NULL;
      data->type = src->type;
      data->unorm_output_reg_mask = src->unorm_output_reg_mask;
      data->disable_depth_output = src->disable_depth_output;
      data->sample_mask = src->sample_mask;
      data->dual_source_blending = src->dual_source_blending;
      memcpy(data->pixel_formats, src->pixel_formats, sizeof(data->pixel_formats));
      break;
    }
    case SM50_SHADER_IA_INPUT_LAYOUT: {
      struct SM50_SHADER_IA_INPUT_LAYOUT_DATA32 *src = (void *)args32;
      struct SM50_SHADER_IA_INPUT_LAYOUT_DATA *data = malloc(sizeof(struct SM50_SHADER_IA_INPUT_LAYOUT_DATA));
      last_arg->next = data;
      last_arg = (void *)data;
      last_arg->next = NULL;
      data->type = src->type;
      data->slot_mask = src->slot_mask;
      data->index_buffer_format = src->index_buffer_format;
      data->num_elements = src->num_elements;
      data->elements = UInt32ToPtr(src->elements);
      break;
    }
    case SM50_SHADER_GS_PASS_THROUGH: {
      struct SM50_SHADER_GS_PASS_THROUGH_DATA32 *src = (void *)args32;
      struct SM50_SHADER_GS_PASS_THROUGH_DATA *data = malloc(sizeof(struct SM50_SHADER_GS_PASS_THROUGH_DATA));
      last_arg->next = data;
      last_arg = (void *)data;
      last_arg->next = NULL;
      data->type = src->type;
      data->Data = src->Data;
      data->RasterizationDisabled = src->RasterizationDisabled;
      break;
    }
    case SM50_SHADER_PSO_GEOMETRY_SHADER: {
      struct SM50_SHADER_PSO_GEOMETRY_SHADER_DATA32 *src = (void *)args32;
      struct SM50_SHADER_PSO_GEOMETRY_SHADER_DATA *data = malloc(sizeof(struct SM50_SHADER_PSO_GEOMETRY_SHADER_DATA));
      last_arg->next = data;
      last_arg = (void *)data;
      last_arg->next = NULL;
      data->type = src->type;
      data->strip_topology = src->strip_topology;
      break;
    }
    case SM50_SHADER_PSO_TESSELLATOR: {
      struct SM50_SHADER_PSO_TESSELLATOR_DATA32 *src = (void *)args32;
      struct SM50_SHADER_PSO_TESSELLATOR_DATA *data = malloc(sizeof(struct SM50_SHADER_PSO_TESSELLATOR_DATA));
      last_arg->next = data;
      last_arg = (void *)data;
      last_arg->next = NULL;
      data->type = src->type;
      data->max_potential_tess_factor = src->max_potential_tess_factor;
      break;
    }
    case SM50_SHADER_ROOT_SIGNATURE:
    case SM50_SHADER_ROOT_SIGNATURE2: {
      struct SM50_SHADER_ROOT_SIGNATURE_DATA32 *src = (void *)args32;
      struct SM50_SHADER_ROOT_SIGNATURE_DATA *data = malloc(sizeof(struct SM50_SHADER_ROOT_SIGNATURE_DATA));
      last_arg->next = data;
      last_arg = (void *)data;
      last_arg->next = NULL;
      data->type = src->type;
      data->bytecode = UInt32ToPtr(src->bytecode);
      data->bytecode_length = src->bytecode_length;
      break;
    }
    case SM50_SHADER_ARGUMENT_TYPE_MAX:
      break;
    }
    args32 = UInt32ToPtr(args32->next);
  }
}

void
sm50_compilation_argument32_free(struct SM50_SHADER_COMPILATION_ARGUMENT_DATA *first_arg) {
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA *arg = first_arg->next;

  while (arg) {
    struct SM50_SHADER_COMPILATION_ARGUMENT_DATA *next = arg->next;
    free(arg);
    arg = next;
  }
}

static NTSTATUS
thunk32_SM50Compile(void *args) {
  struct sm50_compile_params32 *params = args;
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA first_arg;
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA32 *args32 = UInt32ToPtr(params->args);
  sm50_compilation_argument32_convert(&first_arg, args32);

  params->ret = SM50Compile(
      params->shader, &first_arg, UInt32ToPtr(params->func_name), UInt32ToPtr(params->bitcode),
      UInt32ToPtr(params->error)
  );

  sm50_compilation_argument32_free(&first_arg);

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk32_SM50GetCompiledBitcode(void *args) {
  struct sm50_get_compiled_bitcode_params32 *params = args;

  SM50GetCompiledBitcode(params->bitcode, UInt32ToPtr(params->data_out));

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk32_SM50GetErrorMessage(void *args) {
  struct sm50_get_error_message_params32 *params = args;

  params->ret_size = SM50GetErrorMessage(params->error, UInt32ToPtr(params->buffer), params->buffer_size);

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk32_SM50CompileTessellationPipelineHull(void *args) {
  struct sm50_compile_tessellation_pipeline_hull_params32 *params = args;
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA first_arg;
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA32 *args32 = UInt32ToPtr(params->hull_args);
  sm50_compilation_argument32_convert(&first_arg, args32);

  params->ret = SM50CompileTessellationPipelineHull(
      params->vertex, params->hull, &first_arg, UInt32ToPtr(params->func_name), UInt32ToPtr(params->bitcode),
      UInt32ToPtr(params->error)
  );

  sm50_compilation_argument32_free(&first_arg);

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk32_SM50CompileTessellationPipelineDomain(void *args) {
  struct sm50_compile_tessellation_pipeline_domain_params32 *params = args;
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA first_arg;
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA32 *args32 = UInt32ToPtr(params->domain_args);
  sm50_compilation_argument32_convert(&first_arg, args32);

  params->ret = SM50CompileTessellationPipelineDomain(
      params->hull, params->domain, &first_arg, UInt32ToPtr(params->func_name), UInt32ToPtr(params->bitcode),
      UInt32ToPtr(params->error)
  );

  sm50_compilation_argument32_free(&first_arg);

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk32_SM50CompileGeometryPipelineVertex(void *args) {
  struct sm50_compile_geometry_pipeline_vertex_params32 *params = args;
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA first_arg;
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA32 *args32 = UInt32ToPtr(params->vertex_args);
  sm50_compilation_argument32_convert(&first_arg, args32);

  params->ret = SM50CompileGeometryPipelineVertex(
      params->vertex, params->geometry, &first_arg, UInt32ToPtr(params->func_name), UInt32ToPtr(params->bitcode),
      UInt32ToPtr(params->error)
  );

  sm50_compilation_argument32_free(&first_arg);

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk32_SM50CompileGeometryPipelineGeometry(void *args) {
  struct sm50_compile_geometry_pipeline_geometry_params32 *params = args;
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA first_arg;
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA32 *args32 = UInt32ToPtr(params->geometry_args);
  sm50_compilation_argument32_convert(&first_arg, args32);

  params->ret = SM50CompileGeometryPipelineGeometry(
      params->vertex, params->geometry, &first_arg, UInt32ToPtr(params->func_name), UInt32ToPtr(params->bitcode),
      UInt32ToPtr(params->error)
  );

  sm50_compilation_argument32_free(&first_arg);

  return STATUS_SUCCESS;
}

static NTSTATUS
thunk32_SM50GetArgumentsInfo(void *args) {
  struct sm50_get_arguments_info_params32 *params = args;

  SM50GetArgumentsInfo(params->shader, UInt32ToPtr(params->constant_buffers), UInt32ToPtr(params->arguments));

  return STATUS_SUCCESS;
}
#endif /* DXMT_NATIVE */

static NTSTATUS
_MTLCommandBuffer_error(void *obj) {
  struct unixcall_generic_obj_obj_ret *params = obj;
  params->ret = (obj_handle_t)[(id<MTLCommandBuffer>)params->handle error];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandBuffer_logs(void *obj) {
  struct unixcall_generic_obj_obj_ret *params = obj;
  params->ret = (obj_handle_t)[(id<MTLCommandBuffer>)params->handle logs];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLLogContainer_enumerate(void *obj) {
  struct unixcall_enumerate *params = obj;
  uint64_t count = 0;
  uint64_t read = 0;
  id *buffer = params->buffer.ptr;
  for (id _ in (id<MTLLogContainer>)params->enumerable) {
    if (count >= params->start) {
      if (count < params->start + params->buffer_size) {
        buffer[count - params->start] = _;
        read++;
      } else {
        break;
      }
    }
    count++;
  }
  params->ret_read = read;
  return STATUS_SUCCESS;
}

CFStringRef
GetColorSpaceName(enum WMTColorSpace colorspace) {
  switch (colorspace) {
  case WMTColorSpaceSRGB:
    return kCGColorSpaceSRGB;
  case WMTColorSpaceSRGBLinear:
  case WMTColorSpaceHDR_scRGB:
    return kCGColorSpaceExtendedLinearSRGB;
  case WMTColorSpaceBT2020:
    return kCGColorSpaceITUR_2020_sRGBGamma;
  case WMTColorSpaceHDR_PQ:
    return kCGColorSpaceITUR_2100_PQ;
  default:
    return nil;
  }
}

static NTSTATUS
_CGColorSpace_checkColorSpaceSupported(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = false;
  CFStringRef name = GetColorSpaceName((enum WMTColorSpace)params->handle);
  if (!name)
    return STATUS_SUCCESS;
  CGColorSpaceRef ref = CGColorSpaceCreateWithName(name);
  if (!ref)
    return STATUS_SUCCESS;
  CGColorSpaceRelease(ref);
  params->ret = true;
  return STATUS_SUCCESS;
}

static NTSTATUS
_MetalLayer_setColorSpace(void *obj) {
  struct unixcall_generic_obj_uint64_uint64_ret *params = obj;
  CAMetalLayer *layer = (CAMetalLayer *)params->handle;
  enum WMTColorSpace colorspace = params->arg;
  CFStringRef name = GetColorSpaceName(colorspace);
  params->ret = false;
  if (!name)
    return STATUS_SUCCESS;
  CGColorSpaceRef ref = CGColorSpaceCreateWithName(name);
  if (!ref)
    return STATUS_SUCCESS;
  bool applied = execute_on_main_checked(^{
    layer.colorspace = ref;
    layer.wantsExtendedDynamicRangeContent = WMT_COLORSPACE_IS_HDR(colorspace);
    w7ds_refresh_layer_main(layer);
  });
  CGColorSpaceRelease(ref);
  if (!applied) return STATUS_UNSUCCESSFUL;
  params->ret = true;
  return STATUS_SUCCESS;
}

#include "wave7_layer_apply.inc"

static NTSTATUS
_WMTGetPrimaryDisplayId(void *obj) {
  struct unixcall_generic_obj_ret *params = obj;
  params->ret = CGMainDisplayID();
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTGetSecondaryDisplayId(void *obj) {
  struct unixcall_generic_obj_ret *params = obj;
  params->ret = kCGNullDirectDisplay;

  uint32_t count = 0;
  CGGetOnlineDisplayList(0, NULL, &count);

  if (count == 0)
    return STATUS_SUCCESS;

  CGDirectDisplayID main_display = CGMainDisplayID();
  CGDirectDisplayID displays[count];
  CGGetOnlineDisplayList(count, displays, &count);

  for (uint32_t i = 0; i < count; i++) {
    CGDirectDisplayID id = displays[i];
    if (id == main_display)
      continue;
    if (CGDisplayMirrorsDisplay(id) != kCGNullDirectDisplay)
      continue;
    params->ret = id;
    break;
  }

  return STATUS_SUCCESS;
}

typedef struct icc_XYZ_t {
  uint32_t sig;      // 0x205a5958
  uint32_t reserved; // 0
  int32_t x;
  int32_t y;
  int32_t z;
} icc_XYZ_t;

bool
GetChromaticity_xy(ColorSyncProfileRef profile, CFStringRef tag, float *out_x, float *out_y) {
  CFDataRef tag_data = ColorSyncProfileCopyTag(profile, tag);
  if (!tag_data)
    return false;
  if (CFDataGetLength(tag_data) != sizeof(icc_XYZ_t))
    return false;
  icc_XYZ_t *data = (icc_XYZ_t *)CFDataGetBytePtr(tag_data);
  if (data->sig != 0x205a5958)
    return false;
  double X = (int32_t)__builtin_bswap32(data->x) / 65536.0;
  double Y = (int32_t)__builtin_bswap32(data->y) / 65536.0;
  double Z = (int32_t)__builtin_bswap32(data->z) / 65536.0;
  *out_x = X / (X + Y + Z);
  *out_y = Y / (X + Y + Z);
  return true;
}

bool
GetDisplayColorGamut(ColorSyncProfileRef profile, struct WMTDisplayDescription *desc_out) {
  return GetChromaticity_xy(
             profile, kColorSyncSigMediaWhitePointTag, &desc_out->white_points[0], &desc_out->white_points[1]
         ) &&
         GetChromaticity_xy(
             profile, kColorSyncSigRedColorantTag, &desc_out->red_primaries[0], &desc_out->red_primaries[1]
         ) &&
         GetChromaticity_xy(
             profile, kColorSyncSigGreenColorantTag, &desc_out->green_primaries[0], &desc_out->green_primaries[1]
         ) &&
         GetChromaticity_xy(
             profile, kColorSyncSigBlueColorantTag, &desc_out->blue_primaries[0], &desc_out->blue_primaries[1]
         );
}

NSScreen *
GetNSScreenForDisplayID(CGDirectDisplayID display_id) {
  for (NSScreen *screen in [NSScreen screens]) {
    CGDirectDisplayID id = [[[screen deviceDescription] objectForKey:@"NSScreenNumber"] unsignedIntValue];
    if (id == display_id) {
      return screen;
    }
  }
  return nil;
}







static NTSTATUS
_MTLLibrary_newFunctionWithConstants(void *obj) {
  struct unixcall_mtllibrary_newfunction_with_constants *params = obj;
  id<MTLLibrary> library = (id<MTLLibrary>)params->library;
  NSString *name = [[NSString alloc] initWithCString:(char *)params->name.ptr encoding:NSUTF8StringEncoding];
  struct WMTFunctionConstant *constants = (struct WMTFunctionConstant *)params->constants.ptr;
  NSError *err = NULL;
  MTLFunctionConstantValues *values = [[MTLFunctionConstantValues alloc] init];
  for (uint64_t i = 0; i < params->num_constants; i++)
    [values setConstantValue:constants[i].data.ptr type:(MTLDataType)constants[i].type atIndex:constants[i].index];

  params->ret = (obj_handle_t)[library newFunctionWithName:name constantValues:values error:&err];
  params->ret_error = (obj_handle_t)err;
  [name release];
  [values release];
  return STATUS_SUCCESS;
}







static NTSTATUS
_MTLCommandBuffer_encodeWaitForEvent(void *obj) {
  struct unixcall_generic_obj_obj_uint64_noret *params = obj;
  [(id<MTLCommandBuffer>)params->handle encodeWaitForEvent:(id<MTLSharedEvent>)params->arg0 value:params->arg1];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLSharedEvent_signalValue(void *obj) {
  struct unixcall_generic_obj_uint64_noret *params = obj;
  [(id<MTLSharedEvent>)params->handle setSignaledValue:params->arg];
  return STATUS_SUCCESS;
}

#ifndef DXMT_NATIVE
#include "wave7_shared_listener.inc"

#else
static NTSTATUS
_MTLSharedEvent_setWin32EventAtValue(void *obj) {
  // nop
  return STATUS_SUCCESS;
}

static NTSTATUS
_SharedEventListener_start(void *obj) {
  return STATUS_SUCCESS;
}

static NTSTATUS
_SharedEventListener_create(void *obj) {
  return STATUS_SUCCESS;
}

static NTSTATUS
_SharedEventListener_destroy(void *obj) {
  return STATUS_SUCCESS;
}

#endif

static NTSTATUS
_MTLDevice_newFence(void *obj) {
  struct unixcall_generic_obj_obj_ret *params = obj;
  params->ret = (obj_handle_t)[(id<MTLDevice>)params->handle newFence];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newEvent(void *obj) {
  struct unixcall_generic_obj_obj_ret *params = obj;
  params->ret = (obj_handle_t)[(id<MTLDevice>)params->handle newEvent];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLBuffer_updateContents(void *obj) {
  struct unixcall_mtlbuffer_updatecontents *params = obj;
  memcpy((void *)((char *)[(id<MTLBuffer>)params->buffer contents] + params->offset), params->data.ptr, params->length);
  if ([(id<MTLBuffer>)params->buffer storageMode] == MTLStorageModeManaged)
    [(id<MTLBuffer>)params->buffer didModifyRange:NSMakeRange(params->offset, params->length)];
  return STATUS_SUCCESS;
}

static NTSTATUS
_WMTGetOSVersion(void *obj) {
  struct unixcall_get_os_version *params = obj;
  NSOperatingSystemVersion version = [NSProcessInfo processInfo].operatingSystemVersion;
  params->ret_major = version.majorVersion;
  params->ret_minor = version.minorVersion;
  params->ret_patch = version.patchVersion;
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newBinaryArchive(void *obj) {
  struct unixcall_mtldevice_newbinaryarchive *params = obj;
  NSString *path_str = NULL;
  NSURL *url = NULL;
  MTLBinaryArchiveDescriptor *desc = [[MTLBinaryArchiveDescriptor alloc] init];
  if (params->url.ptr != NULL) {
    path_str = [[NSString alloc] initWithCString:params->url.ptr encoding:NSUTF8StringEncoding];
    url = [[NSURL alloc] initFileURLWithPath:path_str];
    desc.url = url;
  }
  NSError *err = NULL;
  params->ret_archive = (obj_handle_t)[(id<MTLDevice>)params->device newBinaryArchiveWithDescriptor:desc error:&err];
  params->ret_error = (obj_handle_t)err;
  [desc release];
  if (url)
    [url release];
  if (path_str)
    [path_str release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLBinaryArchive_serialize(void *obj) {
  struct unixcall_mtlbinaryarchive_serialize *params = obj;
  NSString *path_str = [[NSString alloc] initWithCString:params->url.ptr encoding:NSUTF8StringEncoding];
  NSURL *url = [[NSURL alloc] initFileURLWithPath:path_str];
  NSError *err = NULL;
  [(id<MTLBinaryArchive>)params->archive serializeToURL:url error:&err];
  params->ret_error = (obj_handle_t)err;
  [url release];
  [path_str release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_DispatchData_alloc_init(void *obj) {
  struct unixcall_generic_obj_uint64_obj_ret *params = obj;
  params->ret = (obj_handle_t)dispatch_data_create((void *)params->handle, params->arg, NULL, NULL);
  return STATUS_SUCCESS;
}

@interface MTLSharedTextureHandle ()

- (MTLSharedTextureHandle *)initWithMachPort:(mach_port_t)port;
- (mach_port_t)createMachPort;

@end

static NTSTATUS
_MTLDevice_newSharedTexture(void *obj) {
  struct unixcall_mtldevice_newtexture *params = obj;
  id<MTLDevice> device = (id<MTLDevice>)params->device;
  struct WMTTextureInfo *info = params->info.ptr;

  if (info->mach_port) {
    MTLSharedTextureHandle *handle = [[MTLSharedTextureHandle alloc] initWithMachPort:info->mach_port];
    id<MTLTexture> ret = [device newSharedTextureWithHandle:handle];
    extract_texture_descriptor(ret, info);
    params->ret = (obj_handle_t)ret;
    info->gpu_resource_id = [ret gpuResourceID]._impl;
    [handle release];
  } else {
    MTLTextureDescriptor *desc = [[MTLTextureDescriptor alloc] init];
    fill_texture_descriptor(desc, info);
    id<MTLTexture> ret = [device newSharedTextureWithDescriptor:desc];
    MTLSharedTextureHandle *handle = [ret newSharedTextureHandle];
    params->ret = (obj_handle_t)ret;
    info->gpu_resource_id = [ret gpuResourceID]._impl;
    info->mach_port = [handle createMachPort]; // implicitly add ref to underlying IOSurface
    [handle release];
    [desc release];
  }

  return STATUS_SUCCESS;
}

/* Private API to register a mach port with the bootstrap server */
extern kern_return_t bootstrap_register2(mach_port_t bp, name_t service_name, mach_port_t sp, int flags);

static NTSTATUS
_WMTBootstrapRegister(void *obj) {
  struct unixcall_bootstrap *params = obj;
  mach_port_t rp = params->mach_port;
  mach_port_t bp;

  if (task_get_bootstrap_port(mach_task_self(), &bp) != KERN_SUCCESS)
    return STATUS_UNSUCCESSFUL;
  NTSTATUS ret = bootstrap_register2(bp, params->name, rp, 0) != KERN_SUCCESS ? STATUS_UNSUCCESSFUL : STATUS_SUCCESS;
  mach_port_deallocate(mach_task_self(), bp);
  return ret;
}

static NTSTATUS
_WMTBootstrapLookUp(void *obj) {
  struct unixcall_bootstrap *params = obj;
  mach_port_t rp = 0;
  mach_port_t bp;

  if (task_get_bootstrap_port(mach_task_self(), &bp) != KERN_SUCCESS)
    return STATUS_UNSUCCESSFUL;
  NTSTATUS ret = bootstrap_look_up(bp, params->name, &rp) != KERN_SUCCESS ? STATUS_UNSUCCESSFUL : STATUS_SUCCESS;
  mach_port_deallocate(mach_task_self(), bp);
  params->mach_port = rp;
  return ret;
}

@protocol MTLDeviceSPI <MTLDevice>

- (id<MTLSharedEvent>)newSharedEventWithMachPort:(mach_port_t)machPort;

@end

@interface MTLSharedEventHandle ()

- (mach_port_t)eventPort;

@end

static NTSTATUS
_MTLSharedEvent_createMachPort(void *obj) {
  struct unixcall_mtlsharedevent_createmachport *params = obj;
  id<MTLSharedEvent> event = (id<MTLSharedEvent>)params->event;
  MTLSharedEventHandle *handle = [event newSharedEventHandle];
  mach_port_t port = [handle eventPort];
  
  // The eventPort method returns a send right that's owned by the handle.
  // We need to add our own send right since we're keeping the port but releasing the handle.
  // This increments the send right count so the port remains valid.
  mach_port_mod_refs(mach_task_self(), port, MACH_PORT_RIGHT_SEND, 1);
  
  params->ret_mach_port = port;
  [handle release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newSharedEventWithMachPort(void *obj) {
  struct unixcall_mtldevice_newsharedeventwithmachport *params = obj;
  id<MTLDevice> device = (id<MTLDevice>)params->device;
  id<MTLDeviceSPI> deviceSPI = (id<MTLDeviceSPI>)device;
  params->ret_event = (obj_handle_t)[deviceSPI newSharedEventWithMachPort:params->mach_port];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_registryID(void *obj) {
  struct unixcall_generic_obj_uint64_ret *params = obj;
  params->ret = [(id<MTLDevice>)params->handle registryID];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLSharedEvent_waitUntilSignaledValue(void *obj) {
  struct unixcall_mtlsharedevent_waituntilsignaledvalue *params = obj;
  bool timeout = [(id<MTLSharedEvent>)params->event waitUntilSignaledValue:params->value timeoutMS:params->timeout_ms];
  params->ret_timeout = timeout;
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCounterSampleBuffer_newTimestampBuffer(void *obj) {
  struct unixcall_mtlcountersamplebuffer_newtimestampbuffer *params = obj;
  id<MTLDevice> device = (id<MTLDevice>)params->device;

  MTLCounterSampleBufferDescriptor *desc = [[MTLCounterSampleBufferDescriptor alloc] init];
  NSArray<id<MTLCounterSet>> *counter_sets = [device counterSets];
  id<MTLCounterSet> timestamp_counter_set = nil;
  for (id<MTLCounterSet> counterSet in counter_sets) {
    if ([counterSet.name isEqualToString:MTLCommonCounterSetTimestamp]) {
      timestamp_counter_set = counterSet;
      break;
    }
  }
  desc.counterSet = timestamp_counter_set;
  desc.sampleCount = params->sample_count;
  desc.storageMode = params->shared ? MTLStorageModeShared : MTLStorageModePrivate;

  NSError *error = nil;
  params->ret = (obj_handle_t)[device newCounterSampleBufferWithDescriptor:desc error:&error];

  [desc release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCounterSampleBuffer_resolveCounterRange(void *obj) {
  struct unixcall_mtlcountersamplebuffer_resolvecounterrange *params = obj;
  id<MTLCounterSampleBuffer> sample_buffer = (id<MTLCounterSampleBuffer>)params->sample_buffer;

  NSData *data = [sample_buffer resolveCounterRange:NSMakeRange(params->start, params->len)];
  if (data && params->data_out.ptr) {
    [data getBytes:params->data_out.ptr length:params->data_length];
  }
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandBuffer_blitCommandEncoderWithSampleBuffers(void *obj) {
  struct unixcall_mtlcommandbuffer_blitcommandencoderwithsamplebuffers *params = obj;
  id<MTLCommandBuffer> cmdbuf = (id<MTLCommandBuffer>)params->cmdbuf;
  struct WMTSampleBufferAttachmentInfo *attachments = params->attachments.ptr;

  MTLBlitPassDescriptor *blit_desc = [[MTLBlitPassDescriptor alloc] init];
  for (uint64_t i = 0; i < params->num_attachments; i++) {
    MTLBlitPassSampleBufferAttachmentDescriptor *desc = blit_desc.sampleBufferAttachments[i];
    desc.sampleBuffer = (id<MTLCounterSampleBuffer>)attachments[i].sample_buffer;
    desc.startOfEncoderSampleIndex = attachments[i].start_of_encoder_sample_index;
    desc.endOfEncoderSampleIndex = attachments[i].end_of_encoder_sample_index;
  }

  params->ret = (obj_handle_t)[cmdbuf blitCommandEncoderWithDescriptor:blit_desc];

  [blit_desc release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandBuffer_property(void *obj) {
  struct unixcall_generic_obj_uint64_uint64_ret *params = obj;
  id<MTLCommandBuffer> cmdbuf = (id<MTLCommandBuffer>)params->handle;
  double ns = 1000000000;
  switch (params->arg) {
  case W7VS_PROPERTY:
    params->ret = w7vs_dispatch((struct W7VSRequest *)(uintptr_t)params->handle);
    break;
  case W7FB_PROPERTY:
    params->ret = w7fb_dispatch((struct W7FBRequest *)(uintptr_t)params->handle);
    break;
  case W7LA_PROPERTY:
    params->ret = w7la_dispatch((struct W7LARequest *)(uintptr_t)params->handle);
    break;
  case WMTCommandBufferPropertyKernelStartTime:
    params->ret = (uint64_t)([cmdbuf kernelStartTime] * ns);
    break;
  case WMTCommandBufferPropertyKernelEndTime:
    params->ret = (uint64_t)([cmdbuf kernelEndTime] * ns);
    break;
  case WMTCommandBufferPropertyGPUStartTime:
    params->ret = (uint64_t)([cmdbuf GPUStartTime] * ns);
    break;
  case WMTCommandBufferPropertyGPUEndTime:
    params->ret = (uint64_t)([cmdbuf GPUEndTime] * ns);
    break;
  default:
    params->ret = 0;
    break;
  }
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newTileRenderPipelineState(void *obj) {
  struct unixcall_mtldevice_newrenderpso *params = obj;
  const struct WMTTileRenderPipelineInfo *info = params->info.ptr;
  MTLTileRenderPipelineDescriptor *descriptor = [[MTLTileRenderPipelineDescriptor alloc] init];

  for (unsigned i = 0; i < 8; i++) {
    descriptor.colorAttachments[i].pixelFormat = to_metal_pixel_format(info->color_formats[i]);
  }

  for (unsigned i = 0; i < 31; i++) {
    if (info->immutable_tile_buffers & (1 << i))
      descriptor.tileBuffers[i].mutability = MTLMutabilityImmutable;
  }

  descriptor.rasterSampleCount = info->raster_sample_count;
  descriptor.threadgroupSizeMatchesTileSize = info->tgsize_matches_tile_size;


  descriptor.tileFunction = (id<MTLFunction>)info->tile_function;

  MTLPipelineOption options = MTLPipelineOptionNone;
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 150000
  if (@available(macOS 15, *)) {
    if (info->num_binary_archives_for_lookup && info->binary_archives_for_lookup.ptr)
      descriptor.binaryArchives = [NSArray arrayWithObjects:(id<MTLBinaryArchive> *)info->binary_archives_for_lookup.ptr
                                                      count:info->num_binary_archives_for_lookup];
    options = info->fail_on_binary_archive_miss ? MTLPipelineOptionFailOnBinaryArchiveMiss : MTLPipelineOptionNone;
  }
#endif
  NSError *err = NULL;
  params->ret_pso = (obj_handle_t)[(id<MTLDevice>)params->device newRenderPipelineStateWithTileDescriptor:descriptor
                                                                                                  options:options
                                                                                               reflection:nil
                                                                                                    error:&err];
  params->ret_error = (obj_handle_t)err;
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 150000
  if (@available(macOS 15, *)) {
    if (!err && info->binary_archive_for_serialization) {
      [(id<MTLBinaryArchive>)info->binary_archive_for_serialization
          addTileRenderPipelineFunctionsWithDescriptor:descriptor
                                                 error:&err];
    }
  }
#endif
  [descriptor release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newResidencySet(void *obj) {
  struct unixcall_mtldevice_newresidencyset *params = obj;
  NSError *err = NULL;
  MTLResidencySetDescriptor *descriptor = [[MTLResidencySetDescriptor alloc] init];
  descriptor.initialCapacity = params->init_capacity;
  params->ret_set = (obj_handle_t)[(id<MTLDevice>)params->device newResidencySetWithDescriptor:descriptor error:&err];
  params->ret_error = (obj_handle_t)err;
  [descriptor release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLResidencySet_addAllocations(void *obj) {
  struct unixcall_mtlresidencyset_addallocations *params = obj;
  id<MTLResidencySet> set = (id<MTLResidencySet>)params->set;
  [set addAllocations:(id<MTLAllocation> const *)params->allocations.ptr count:params->count];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLResidencySet_removeAllocations(void *obj) {
  struct unixcall_mtlresidencyset_addallocations *params = obj;
  id<MTLResidencySet> set = (id<MTLResidencySet>)params->set;
  [set removeAllocations:(id<MTLAllocation> const *)params->allocations.ptr count:params->count];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLResidencySet_removeAllAllocations(void *obj) {
  struct unixcall_generic_obj_noret *params = obj;
  id<MTLResidencySet> set = (id<MTLResidencySet>)params->handle;
  [set removeAllAllocations];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLResidencySet_commit(void *obj) {
  struct unixcall_generic_obj_noret *params = obj;
  id<MTLResidencySet> set = (id<MTLResidencySet>)params->handle;
  [set commit];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLCommandQueue_addResidencySet(void *obj) {
  struct unixcall_generic_obj_obj_noret *params = obj;
  id<MTLCommandQueue> queue = (id<MTLCommandQueue>)params->handle;
  id<MTLResidencySet> set = (id<MTLResidencySet>)params->arg;
  [queue addResidencySet:set];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newHeap(void *obj) {
  struct unixcall_mtldevice_newheap *params = obj;
  id<MTLDevice> device = (id<MTLDevice>)params->device;
  struct WMTHeapInfo const *info = params->info.ptr;
  MTLHeapDescriptor *desc = [[MTLHeapDescriptor alloc] init];
  desc.resourceOptions = (MTLResourceOptions)info->options;
  desc.sparsePageSize = (MTLSparsePageSize)info->sparse_page_size;
  desc.type = (MTLHeapType)info->type;
  desc.size = info->size;

  params->ret = (obj_handle_t)[device newHeapWithDescriptor:desc];

  [desc release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_heapBufferSizeAndAlign(void *obj) {
  struct unixcall_mtldevice_heapbuffersizeandalign *params = obj;
  id<MTLDevice> device = (id<MTLDevice>)params->device;
  MTLSizeAndAlign ret =
      [device heapBufferSizeAndAlignWithLength:params->length options:(MTLResourceOptions)params->options];
  params->ret_size = ret.size;
  params->ret_align = ret.align;
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_heapTextureSizeAndAlign(void *obj) {
  struct unixcall_mtldevice_heaptexturesizeandalign *params = obj;
  id<MTLDevice> device = (id<MTLDevice>)params->device;
  struct WMTTextureInfo *info = params->info.ptr;
  MTLTextureDescriptor *desc = [[MTLTextureDescriptor alloc] init];
  fill_texture_descriptor(desc, info);
  MTLSizeAndAlign ret = [device heapTextureSizeAndAlignWithDescriptor:desc];
  params->ret_size = ret.size;
  params->ret_align = ret.align;
  [desc release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLHeap_newBuffer(void *obj) {
  struct unixcall_mtlheap_newbuffer *params = obj;
  id<MTLHeap> heap = (id<MTLHeap>)params->heap;
  struct WMTBufferInfo *info = params->info.ptr;
  id<MTLBuffer> buffer;
  if (~params->offset == 0)
    buffer = [heap newBufferWithLength:info->length options:(enum MTLResourceOptions)info->options];
  else
    buffer =
        [heap newBufferWithLength:info->length options:(enum MTLResourceOptions)info->options offset:params->offset];
  info->memory.ptr = [heap storageMode] == MTLStorageModePrivate ? NULL : [buffer contents];

  params->ret = (obj_handle_t)buffer;
  info->gpu_address = [buffer gpuAddress];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLHeap_newTexture(void *obj) {
  struct unixcall_mtlheap_newtexture *params = obj;
  id<MTLHeap> heap = (id<MTLHeap>)params->heap;
  struct WMTTextureInfo *info = params->info.ptr;
  MTLTextureDescriptor *desc = [[MTLTextureDescriptor alloc] init];
  fill_texture_descriptor(desc, info);

  id<MTLTexture> ret;
  if (~params->offset == 0)
    ret = [heap newTextureWithDescriptor:desc];
  else
    ret = [heap newTextureWithDescriptor:desc offset:params->offset];
  params->ret = (obj_handle_t)ret;
  info->gpu_resource_id = [ret gpuResourceID]._impl;
  info->mach_port = 0;

  [desc release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newIndirectCommandBuffer(void *obj) {
  struct unixcall_mtldevice_newicb *params = obj;
  id<MTLDevice> device = (id<MTLDevice>)params->device;

  struct WMTIndirectCommandBufferInfo *info = params->info.ptr;

  MTLIndirectCommandBufferDescriptor *desc = [[MTLIndirectCommandBufferDescriptor alloc] init];

  desc.commandTypes = (MTLIndirectCommandType)info->type;

  desc.maxFragmentBufferBindCount = info->max_fragment_buffer_binding;
  desc.maxKernelBufferBindCount = info->max_kernel_buffer_binding;
  desc.maxKernelThreadgroupMemoryBindCount = info->max_kernel_threadgroup_memory_binding;
  desc.maxMeshBufferBindCount = info->max_mesh_buffer_binding;
  desc.maxObjectBufferBindCount = info->max_object_buffer_binding;
  desc.maxObjectThreadgroupMemoryBindCount = info->max_object_threadgroup_memory_binding;
  desc.maxVertexBufferBindCount = info->max_vertex_buffer_binding;

  desc.inheritBuffers = info->inherit_buffers;
  desc.inheritPipelineState = info->inherit_pso;
  desc.supportDynamicAttributeStride = info->support_dynamic_attribute_stride;
  desc.supportRayTracing = info->support_ray_tracing;

#if 0 // needs macOS 26 SDK, and we don't really use it at the moment
  desc.inheritCullMode = info->inherit_cull_mode;
  desc.inheritDepthBias = info->inherit_depth_bias;
  desc.inheritDepthClipMode = info->inherit_depth_clip_mode;
  desc.inheritDepthStencilState = info->inherit_depth_stencil_state;
  desc.inheritFrontFacingWinding = info->inherit_front_facing;
  desc.inheritTriangleFillMode = info->inherit_fill_mode;
  desc.supportColorAttachmentMapping = info->support_color_attachment_mapping;
#endif

  id<MTLIndirectCommandBuffer> icb =
      [device newIndirectCommandBufferWithDescriptor:desc
                                     maxCommandCount:params->max_count
                                             options:(MTLResourceOptions)params->options];
  params->ret = (obj_handle_t)icb;
  info->gpu_resource_id = [icb gpuResourceID]._impl;
  [desc release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLDevice_newLibraryWithSource(void *obj) {
  struct unixcall_mtldevice_newlibrarywithsource *params = obj;
  id<MTLDevice> device = (id<MTLDevice>)params->device;
  NSString *source =
      [[NSString alloc] initWithBytes:params->source.ptr length:params->length encoding:NSASCIIStringEncoding];
  NSError *err = NULL;
  params->ret_library = (obj_handle_t)[device newLibraryWithSource:source options:nil error:&err];
  params->ret_error = (obj_handle_t)err;
  [source release];
  return STATUS_SUCCESS;
}

static NTSTATUS
_MTLTexture_getBytes(void *obj) {
  struct unixcall_mtltexture_replaceregion *params = obj;
  [(id<MTLTexture>)params->texture getBytes:params->data.ptr
                                bytesPerRow:params->bytes_per_row
                              bytesPerImage:params->bytes_per_image
                                 fromRegion:MTLRegionMake3D(
                                                params->origin.x, params->origin.y, params->origin.z,
                                                params->size.width, params->size.height, params->size.depth
                                            )
                                mipmapLevel:params->level
                                      slice:params->slice];
  return STATUS_SUCCESS;
}

/*
 * Definition from cache.c
 */

NTSTATUS _CacheReader_alloc_init(void *obj);
NTSTATUS _CacheReader_get(void *obj);
NTSTATUS _CacheWriter_alloc_init(void *obj);
NTSTATUS _CacheWriter_set(void *obj);
NTSTATUS _Cache_alloc_init_checked(void *obj);
NTSTATUS _CacheReader_get_checked(void *obj);
NTSTATUS _CacheWriter_set_checked(void *obj);
NTSTATUS _WMTSetMetalShaderCachePath(void *obj);

const void *__wine_unix_call_funcs[] = {
    &_NSObject_retain,
    &_NSObject_release,
    &_NSArray_object,
    &_NSArray_count,
    &_MTLCopyAllDevices,
    &_MTLDevice_recommendedMaxWorkingSetSize,
    &_MTLDevice_currentAllocatedSize,
    &_MTLDevice_name,
    &_NSString_getCString,
    &_MTLDevice_newCommandQueue,
    &_NSAutoreleasePool_alloc_init,
    &_MTLCommandQueue_commandBuffer,
    &_MTLCommandBuffer_commit,
    &_MTLCommandBuffer_waitUntilCompleted,
    &_MTLCommandBuffer_status,
    &_MTLDevice_newSharedEvent,
    &_MTLSharedEvent_signaledValue,
    &_MTLCommandBuffer_encodeSignalEvent,
    &_MTLDevice_newBuffer,
    &_MTLDevice_newSamplerState,
    &_MTLDevice_newDepthStencilState,
    &_MTLDevice_newTexture,
    &_MTLBuffer_newTexture,
    &_MTLTexture_newTextureView,
    &_MTLDevice_minimumLinearTextureAlignmentForPixelFormat,
    &_MTLDevice_newLibrary,
    &_MTLLibrary_newFunction,
    &_NSString_lengthOfBytesUsingEncoding,
    &_NSObject_description,
    &_MTLDevice_newComputePipelineState,
    &_MTLCommandBuffer_blitCommandEncoder,
    &_MTLCommandBuffer_computeCommandEncoder,
    &_MTLCommandBuffer_renderCommandEncoder,
    &_MTLCommandEncoder_endEncoding,
    &_MTLDevice_newRenderPipelineState,
    &_MTLDevice_newMeshRenderPipelineState,
    &_MTLBlitCommandEncoder_encodeCommands,
    &_MTLComputeCommandEncoder_encodeCommands,
    &_MTLRenderCommandEncoder_encodeCommands,
    &_MTLTexture_pixelFormat,
    &_MTLTexture_width,
    &_MTLTexture_height,
    &_MTLTexture_depth,
    &_MTLTexture_arrayLength,
    &_MTLTexture_mipmapLevelCount,
    &_MTLTexture_replaceRegion,
    &_MTLBuffer_didModifyRange,
    &_MTLCommandBuffer_presentDrawable,
    &_MTLCommandBuffer_presentDrawableAfterMinimumDuration,
    &_MTLDevice_supportsFamily,
    &_MTLDevice_supportsBCTextureCompression,
    &_MTLDevice_supportsTextureSampleCount,
    &_MTLDevice_hasUnifiedMemory,
    &_MTLCaptureManager_sharedCaptureManager,
    &_MTLCaptureManager_startCapture,
    &_MTLCaptureManager_stopCapture,
    &_MTLDevice_newTemporalScaler,
    &_MTLDevice_newSpatialScaler,
    &_MTLCommandBuffer_encodeTemporalScale,
    &_MTLCommandBuffer_encodeSpatialScale,
    &_NSString_string,
    &_NSString_alloc_init,
    &_DeveloperHUDProperties_instance,
    &_DeveloperHUDProperties_addLabel,
    &_DeveloperHUDProperties_updateLabel,
    &_DeveloperHUDProperties_remove,
    &_MetalDrawable_texture,
    &_MetalLayer_nextDrawable,
    &_MTLDevice_supportsFXSpatialScaler,
    &_MTLDevice_supportsFXTemporalScaler,
    &_MetalLayer_setProps,
    &_MetalLayer_getProps,
    &_CreateMetalViewFromHWND,
    &_ReleaseMetalView,
    &thunk_SM50Initialize,
    &thunk_SM50Destroy,
    &thunk_SM50Compile,
    &thunk_SM50GetCompiledBitcode,
    &thunk_SM50DestroyBitcode,
    &thunk_SM50GetErrorMessage,
    &thunk_SM50FreeError,
    &thunk_SM50CompileGeometryPipelineVertex,
    &thunk_SM50CompileGeometryPipelineGeometry,
    NULL,
    &thunk_SM50CompileTessellationPipelineHull,
    &thunk_SM50CompileTessellationPipelineDomain,
    &_MTLCommandEncoder_setLabel,
    &_MTLDevice_setShouldMaximizeConcurrentCompilation,
    &thunk_SM50GetArgumentsInfo,
    &_MTLCommandBuffer_error,
    &_MTLCommandBuffer_logs,
    &_MTLLogContainer_enumerate,
    &_CGColorSpace_checkColorSpaceSupported,
    &_MetalLayer_setColorSpace,
    &_WMTGetPrimaryDisplayId,
    &_WMTGetSecondaryDisplayId,
    &_WMTGetDisplayDescription,
    &_MetalLayer_getEDRValue,
    &_MTLLibrary_newFunctionWithConstants,
    &_WMTQueryDisplaySetting,
    &_WMTUpdateDisplaySetting,
    &_WMTQueryDisplaySettingForLayer,
    &_MTLCommandBuffer_encodeWaitForEvent,
    &_MTLSharedEvent_signalValue,
    &_MTLSharedEvent_setWin32EventAtValue,
    &_MTLDevice_newFence,
    &_MTLDevice_newEvent,
    &_MTLBuffer_updateContents,
    &_SharedEventListener_create,
    &_SharedEventListener_start,
    &_SharedEventListener_destroy,
    &_WMTGetOSVersion,
    &_MTLDevice_newBinaryArchive,
    &_MTLBinaryArchive_serialize,
    &_DispatchData_alloc_init,
    &_CacheReader_alloc_init,
    &_CacheReader_get,
    &_CacheWriter_alloc_init,
    &_CacheWriter_set,
    &_WMTSetMetalShaderCachePath,
    &_MTLDevice_newSharedTexture,
    &_WMTBootstrapRegister,
    &_WMTBootstrapLookUp,
    &_MTLSharedEvent_createMachPort,
    &_MTLDevice_newSharedEventWithMachPort,
    &_MTLDevice_registryID,
    &_MTLSharedEvent_waitUntilSignaledValue,
    &_MTLCounterSampleBuffer_newTimestampBuffer,
    &_MTLCounterSampleBuffer_resolveCounterRange,
    &_MTLCommandBuffer_blitCommandEncoderWithSampleBuffers,
    &_MTLCommandBuffer_property,
    &_MTLDevice_newTileRenderPipelineState,
    &_MTLDevice_newResidencySet,
    &_MTLResidencySet_addAllocations,
    &_MTLResidencySet_removeAllocations,
    &_MTLResidencySet_removeAllAllocations,
    &_MTLResidencySet_commit,
    &_MTLCommandQueue_addResidencySet,
    &_MTLDevice_newHeap,
    &_MTLDevice_heapBufferSizeAndAlign,
    &_MTLDevice_heapTextureSizeAndAlign,
    &_MTLHeap_newBuffer,
    &_MTLHeap_newTexture,
    &_MTLDevice_newIndirectCommandBuffer,
    &_MTLDevice_newLibraryWithSource,
    &_MTLTexture_getBytes,
    &_Cache_alloc_init_checked,
    &_CacheReader_get_checked,
    &_CacheWriter_set_checked,
    &_MTLAllocation_allocatedSize,
};

#ifndef DXMT_NATIVE
const void *__wine_unix_call_wow64_funcs[] = {
    &_NSObject_retain,
    &_NSObject_release,
    &_NSArray_object,
    &_NSArray_count,
    &_MTLCopyAllDevices,
    &_MTLDevice_recommendedMaxWorkingSetSize,
    &_MTLDevice_currentAllocatedSize,
    &_MTLDevice_name,
    &_NSString_getCString,
    &_MTLDevice_newCommandQueue,
    &_NSAutoreleasePool_alloc_init,
    &_MTLCommandQueue_commandBuffer,
    &_MTLCommandBuffer_commit,
    &_MTLCommandBuffer_waitUntilCompleted,
    &_MTLCommandBuffer_status,
    &_MTLDevice_newSharedEvent,
    &_MTLSharedEvent_signaledValue,
    &_MTLCommandBuffer_encodeSignalEvent,
    &_MTLDevice_newBuffer,
    &_MTLDevice_newSamplerState,
    &_MTLDevice_newDepthStencilState,
    &_MTLDevice_newTexture,
    &_MTLBuffer_newTexture,
    &_MTLTexture_newTextureView,
    &_MTLDevice_minimumLinearTextureAlignmentForPixelFormat,
    &_MTLDevice_newLibrary,
    &_MTLLibrary_newFunction,
    &_NSString_lengthOfBytesUsingEncoding,
    &_NSObject_description,
    &_MTLDevice_newComputePipelineState,
    &_MTLCommandBuffer_blitCommandEncoder,
    &_MTLCommandBuffer_computeCommandEncoder,
    &_MTLCommandBuffer_renderCommandEncoder,
    &_MTLCommandEncoder_endEncoding,
    &_MTLDevice_newRenderPipelineState,
    &_MTLDevice_newMeshRenderPipelineState,
    &_MTLBlitCommandEncoder_encodeCommands,
    &_MTLComputeCommandEncoder_encodeCommands,
    &_MTLRenderCommandEncoder_encodeCommands,
    &_MTLTexture_pixelFormat,
    &_MTLTexture_width,
    &_MTLTexture_height,
    &_MTLTexture_depth,
    &_MTLTexture_arrayLength,
    &_MTLTexture_mipmapLevelCount,
    &_MTLTexture_replaceRegion,
    &_MTLBuffer_didModifyRange,
    &_MTLCommandBuffer_presentDrawable,
    &_MTLCommandBuffer_presentDrawableAfterMinimumDuration,
    &_MTLDevice_supportsFamily,
    &_MTLDevice_supportsBCTextureCompression,
    &_MTLDevice_supportsTextureSampleCount,
    &_MTLDevice_hasUnifiedMemory,
    &_MTLCaptureManager_sharedCaptureManager,
    &_MTLCaptureManager_startCapture,
    &_MTLCaptureManager_stopCapture,
    &_MTLDevice_newTemporalScaler,
    &_MTLDevice_newSpatialScaler,
    &_MTLCommandBuffer_encodeTemporalScale,
    &_MTLCommandBuffer_encodeSpatialScale,
    &_NSString_string,
    &_NSString_alloc_init,
    &_DeveloperHUDProperties_instance,
    &_DeveloperHUDProperties_addLabel,
    &_DeveloperHUDProperties_updateLabel,
    &_DeveloperHUDProperties_remove,
    &_MetalDrawable_texture,
    &_MetalLayer_nextDrawable,
    &_MTLDevice_supportsFXSpatialScaler,
    &_MTLDevice_supportsFXTemporalScaler,
    &_MetalLayer_setProps,
    &_MetalLayer_getProps,
    &_CreateMetalViewFromHWND,
    &_ReleaseMetalView,
    &thunk32_SM50Initialize,
    &thunk_SM50Destroy,
    &thunk32_SM50Compile,
    &thunk32_SM50GetCompiledBitcode,
    &thunk_SM50DestroyBitcode,
    &thunk32_SM50GetErrorMessage,
    &thunk_SM50FreeError,
    &thunk32_SM50CompileGeometryPipelineVertex,
    &thunk32_SM50CompileGeometryPipelineGeometry,
    NULL,
    &thunk32_SM50CompileTessellationPipelineHull,
    &thunk32_SM50CompileTessellationPipelineDomain,
    &_MTLCommandEncoder_setLabel,
    &_MTLDevice_setShouldMaximizeConcurrentCompilation,
    &thunk32_SM50GetArgumentsInfo,
    &_MTLCommandBuffer_error,
    &_MTLCommandBuffer_logs,
    &_MTLLogContainer_enumerate,
    &_CGColorSpace_checkColorSpaceSupported,
    &_MetalLayer_setColorSpace,
    &_WMTGetPrimaryDisplayId,
    &_WMTGetSecondaryDisplayId,
    &_WMTGetDisplayDescription,
    &_MetalLayer_getEDRValue,
    &_MTLLibrary_newFunctionWithConstants,
    &_WMTQueryDisplaySetting,
    &_WMTUpdateDisplaySetting,
    &_WMTQueryDisplaySettingForLayer,
    &_MTLCommandBuffer_encodeWaitForEvent,
    &_MTLSharedEvent_signalValue,
    &_MTLSharedEvent_setWin32EventAtValue,
    &_MTLDevice_newFence,
    &_MTLDevice_newEvent,
    &_MTLBuffer_updateContents,
    &_SharedEventListener_create,
    &_SharedEventListener_start,
    &_SharedEventListener_destroy,
    &_WMTGetOSVersion,
    &_MTLDevice_newBinaryArchive,
    &_MTLBinaryArchive_serialize,
    &_DispatchData_alloc_init,
    &_CacheReader_alloc_init,
    &_CacheReader_get,
    &_CacheWriter_alloc_init,
    &_CacheWriter_set,
    &_WMTSetMetalShaderCachePath,
    &_MTLDevice_newSharedTexture,
    &_WMTBootstrapRegister,
    &_WMTBootstrapLookUp,
    &_MTLSharedEvent_createMachPort,
    &_MTLDevice_newSharedEventWithMachPort,
    &_MTLDevice_registryID,
    &_MTLSharedEvent_waitUntilSignaledValue,
    &_MTLCounterSampleBuffer_newTimestampBuffer,
    &_MTLCounterSampleBuffer_resolveCounterRange,
    &_MTLCommandBuffer_blitCommandEncoderWithSampleBuffers,
    &_MTLCommandBuffer_property,
    &_MTLDevice_newTileRenderPipelineState,
    &_MTLDevice_newResidencySet,
    &_MTLResidencySet_addAllocations,
    &_MTLResidencySet_removeAllocations,
    &_MTLResidencySet_removeAllAllocations,
    &_MTLResidencySet_commit,
    &_MTLCommandQueue_addResidencySet,
    &_MTLDevice_newHeap,
    &_MTLDevice_heapBufferSizeAndAlign,
    &_MTLDevice_heapTextureSizeAndAlign,
    &_MTLHeap_newBuffer,
    &_MTLHeap_newTexture,
    &_MTLDevice_newIndirectCommandBuffer,
    &_MTLDevice_newLibraryWithSource,
    &_MTLTexture_getBytes,
    &_Cache_alloc_init_checked,
    &_CacheReader_get_checked,
    &_CacheWriter_set_checked,
    &_MTLAllocation_allocatedSize,
};
#endif
