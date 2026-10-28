/* Optional compiler-only route. MRC, included by winemetal_unix.c.
 * The existing ordinary PSO ABI and command encoders remain unchanged. */
#include <stdlib.h>
#include <pthread.h>
#import <objc/runtime.h>

#if defined(DXMT_METAL4_COMPILER) && DXMT_METAL4_COMPILER && \
    __has_include(<Metal/MTL4Compiler.h>) && !defined(DXMT_METAL4_FORCE_UNAVAILABLE)
#define WMT_METAL4_AVAILABLE 1

static char wmt_metal4_recipe_key;
static pthread_mutex_t wmt_metal4_compile_lock = PTHREAD_MUTEX_INITIALIZER;
static bool wmt_metal4_capable(id<MTLDevice> device);

static bool
wmt_metal4_enabled(void) {
  static dispatch_once_t once;
  static bool enabled;
  dispatch_once(&once, ^{
    const char *value = getenv("DXMT_METAL4_COMPILER");
    enabled = value && !strcmp(value, "1");
  });
  return enabled;
}

/* Attach before publishing the function. The dictionary owns the library and
 * copied specialization values; no PE pointer survives the synchronous call.
 * Provenance follows function lifetime instead of a growing global registry. */
static void
wmt_metal4_remember(id<MTLFunction> function, id<MTLLibrary> library,
                    NSString *name, MTLFunctionConstantValues *values) {
  if (!function || !library || !name || !wmt_metal4_capable(library.device)) return;
  NSString *name_copy = [name copy];
  MTLFunctionConstantValues *copy = [values copy];
  NSDictionary *recipe = copy ? @{ @"library":library, @"name":name_copy, @"constants":copy }
                              : @{ @"library":library, @"name":name_copy };
  objc_setAssociatedObject(function, &wmt_metal4_recipe_key, recipe, OBJC_ASSOCIATION_RETAIN);
  [copy release];
  [name_copy release];
}

static bool
wmt_metal4_capable(id<MTLDevice> device) {
  if (!wmt_metal4_enabled()) return false;
  if (@available(macOS 26.0, *))
    return [device respondsToSelector:@selector(newCompilerWithDescriptor:error:)] &&
           [device supportsFamily:MTLGPUFamilyMetal4];
  return false;
}

static NSError *
wmt_metal4_error(NSString *reason) {
  return [NSError errorWithDomain:@"DXMTMetal4Compiler" code:1
                        userInfo:@{NSLocalizedDescriptionKey:reason}];
}

static MTL4FunctionDescriptor *
wmt_metal4_function(id<MTLFunction> function) API_AVAILABLE(macos(26.0));
static MTL4FunctionDescriptor *
wmt_metal4_function(id<MTLFunction> function) {
  if (!function) return nil;
  NSDictionary *recipe = [objc_getAssociatedObject(function, &wmt_metal4_recipe_key) retain];
  if (!recipe) return nil;
  MTL4LibraryFunctionDescriptor *base = [[MTL4LibraryFunctionDescriptor alloc] init];
  base.library = recipe[@"library"];
  base.name = recipe[@"name"];
  if (!recipe[@"constants"]) { [recipe release]; return [base autorelease]; }
  MTL4SpecializedFunctionDescriptor *specialized = [[MTL4SpecializedFunctionDescriptor alloc] init];
  specialized.functionDescriptor = base;
  specialized.constantValues = recipe[@"constants"];
  [recipe release];
  [base release];
  return [specialized autorelease];
}

/* Only the complete supported recipe enters the new compiler. Archive work,
 * indirect commands and patch shaders keep their existing implementation.
 * Immutable buffer declarations are optimization hints, absent in Metal4's
 * public descriptors; allowing mutable buffers preserves shader semantics. */
static bool
wmt_metal4_render(id<MTLDevice> device, MTLRenderPipelineDescriptor *old,
                  const struct WMTRenderPipelineInfo *info,
                  id<MTLRenderPipelineState> *result, NSError **error) {
  if (!wmt_metal4_capable(device) || info->logic_operation_enabled ||
      info->support_indirect_command_buffers || info->num_binary_archives_for_lookup ||
      info->binary_archive_for_serialization || info->fail_on_binary_archive_miss ||
      old.vertexFunction.patchType != MTLPatchTypeNone ||
      (old.rasterizationEnabled && old.vertexFunction && !old.fragmentFunction) ||
      (info->depth_pixel_format != WMTPixelFormatInvalid &&
       info->depth_pixel_format != WMTPixelFormatDepth32Float) ||
      info->stencil_pixel_format != WMTPixelFormatInvalid)
    return false;
  if (@available(macOS 26.0, *)) {
    *result = nil;
    *error = nil;
    /* Missing vertex used to assert in the Metal4 driver; never submit it.
     * Rasterizing without a fragment function is a supported legacy recipe,
     * but Metal4 documents it as invalid; the guard above keeps that legacy. */
    if (!old.vertexFunction) {
      *error = wmt_metal4_error(@"Required pipeline function is missing");
      return true;
    }
    MTL4FunctionDescriptor *vertex = wmt_metal4_function(old.vertexFunction);
    MTL4FunctionDescriptor *fragment = wmt_metal4_function(old.fragmentFunction);
    if (!vertex || (old.fragmentFunction && !fragment)) return false;
    if (old.vertexFunction.functionType != MTLFunctionTypeVertex ||
        (old.fragmentFunction && old.fragmentFunction.functionType != MTLFunctionTypeFragment) ||
        ![device supportsTextureSampleCount:old.rasterSampleCount]) {
      *error = wmt_metal4_error(@"Invalid pipeline function stage or sample count");
      return true;
    }
    MTL4RenderPipelineDescriptor *desc = [[MTL4RenderPipelineDescriptor alloc] init];
    desc.vertexFunctionDescriptor = vertex;
    desc.fragmentFunctionDescriptor = fragment;
    desc.vertexDescriptor = old.vertexDescriptor;
    /* Metal4 derives depth format from the render pass. The admitted legacy
     * encoder bridge has image/depth-test parity for no depth and Depth32Float;
     * other depth/stencil recipes stay legacy until separately validated. */
    desc.rasterSampleCount = old.rasterSampleCount;
    desc.rasterizationEnabled = old.rasterizationEnabled;
    desc.inputPrimitiveTopology = old.inputPrimitiveTopology;
    desc.alphaToCoverageState = old.alphaToCoverageEnabled ? MTL4AlphaToCoverageStateEnabled : MTL4AlphaToCoverageStateDisabled;
    desc.alphaToOneState = MTL4AlphaToOneStateDisabled;
    desc.colorAttachmentMappingState = MTL4LogicalToPhysicalColorAttachmentMappingStateIdentity;
    desc.supportIndirectCommandBuffers = MTL4IndirectCommandBufferSupportStateDisabled;
    for (unsigned i = 0; i < 8; ++i) {
      MTLRenderPipelineColorAttachmentDescriptor *src = old.colorAttachments[i];
      MTL4RenderPipelineColorAttachmentDescriptor *dst = desc.colorAttachments[i];
      dst.pixelFormat = src.pixelFormat;
      dst.blendingState = src.blendingEnabled ? MTL4BlendStateEnabled : MTL4BlendStateDisabled;
      dst.writeMask = src.writeMask;
      dst.alphaBlendOperation = src.alphaBlendOperation;
      dst.rgbBlendOperation = src.rgbBlendOperation;
      dst.sourceRGBBlendFactor = src.sourceRGBBlendFactor;
      dst.sourceAlphaBlendFactor = src.sourceAlphaBlendFactor;
      dst.destinationRGBBlendFactor = src.destinationRGBBlendFactor;
      dst.destinationAlphaBlendFactor = src.destinationAlphaBlendFactor;
    }
    /* No global compiler/device cache, no background producer, and at most one
     * compiler at a time. A returned PSO owns its native dependencies. */
    pthread_mutex_lock(&wmt_metal4_compile_lock);
    MTL4CompilerDescriptor *compiler_desc = [[MTL4CompilerDescriptor alloc] init];
    id<MTL4Compiler> compiler = [device newCompilerWithDescriptor:compiler_desc error:error];
    if (compiler && !*error)
      *result = [compiler newRenderPipelineStateWithDescriptor:desc compilerTaskOptions:nil error:error];
    [compiler release];
    [compiler_desc release];
    pthread_mutex_unlock(&wmt_metal4_compile_lock);
    [desc release];
    if (*error && *result) { [*result release]; *result = nil; }
    if (!*result && !*error) *error = wmt_metal4_error(@"Metal4 returned no render pipeline");
    return true;
  }
  return false;
}

static bool
wmt_metal4_compute(id<MTLDevice> device, MTLComputePipelineDescriptor *old,
                   const struct WMTComputePipelineInfo *info,
                   id<MTLComputePipelineState> *result, NSError **error) {
  if (!wmt_metal4_capable(device) || info->support_indirect_command_buffers ||
      info->num_binary_archives_for_lookup || info->binary_archive_for_serialization ||
      info->fail_on_binary_archive_miss) return false;
  if (@available(macOS 26.0, *)) {
    *result = nil;
    *error = nil;
    if (!old.computeFunction) {
      *error = wmt_metal4_error(@"Required compute function is missing");
      return true;
    }
    MTL4FunctionDescriptor *function = wmt_metal4_function(old.computeFunction);
    if (!function) return false;
    if (old.computeFunction.functionType != MTLFunctionTypeKernel) {
      *error = wmt_metal4_error(@"Invalid compute function stage");
      return true;
    }
    MTL4ComputePipelineDescriptor *desc = [[MTL4ComputePipelineDescriptor alloc] init];
    desc.computeFunctionDescriptor = function;
    desc.threadGroupSizeIsMultipleOfThreadExecutionWidth = old.threadGroupSizeIsMultipleOfThreadExecutionWidth;
    desc.maxTotalThreadsPerThreadgroup = old.maxTotalThreadsPerThreadgroup;
    desc.supportIndirectCommandBuffers = MTL4IndirectCommandBufferSupportStateDisabled;
    pthread_mutex_lock(&wmt_metal4_compile_lock);
    MTL4CompilerDescriptor *compiler_desc = [[MTL4CompilerDescriptor alloc] init];
    id<MTL4Compiler> compiler = [device newCompilerWithDescriptor:compiler_desc error:error];
    if (compiler && !*error)
      *result = [compiler newComputePipelineStateWithDescriptor:desc compilerTaskOptions:nil error:error];
    [compiler release];
    [compiler_desc release];
    pthread_mutex_unlock(&wmt_metal4_compile_lock);
    [desc release];
    if (*error && *result) { [*result release]; *result = nil; }
    if (!*result && !*error) *error = wmt_metal4_error(@"Metal4 returned no compute pipeline");
    return true;
  }
  return false;
}
#else
#define WMT_METAL4_AVAILABLE 0
#define wmt_metal4_remember(function, library, name, values) ((void)0)
#define wmt_metal4_render(device, old, info, result, error) false
#define wmt_metal4_compute(device, old, info, result, error) false
#endif
