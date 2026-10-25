// SPDX-License-Identifier: LGPL-2.1-or-later
// Native image oracle for the actual production present shader. No drawable/GUI.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "dxmt_command_constants.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

static void require(bool ok, const char *what) {
  if (!ok) { std::fprintf(stderr, "FAIL %s\n", what); std::exit(30); }
}
static void metal_error(NSError *error, const char *what) {
  if (error) { std::fprintf(stderr, "FAIL %s: %.512s\n", what, error.description.UTF8String); std::exit(30); }
}
struct Metadata { float edr_scale, max_content_luminance, max_display_luminance; };
static_assert(sizeof(Metadata) == 12);
struct Case { const char *name; bool ms, matched; NSUInteger width, height, x, y, vw, vh; };
static constexpr Case cases[] = {
  {"single_matched", false, true, 4, 3, 0, 0, 4, 3},
  {"msaa_matched", true, true, 4, 3, 0, 0, 4, 3},
  {"single_offset", false, false, 8, 3, 2, 0, 4, 3},
  {"msaa_offset", true, false, 8, 3, 2, 0, 4, 3},
  {"single_scaled_offset", false, false, 12, 6, 2, 0, 8, 6},
  {"msaa_scaled_offset", true, false, 12, 6, 2, 0, 8, 6},
};
static id<MTLRenderPipelineState> pipeline(id<MTLDevice> device, id<MTLFunction> vertex,
                                         id<MTLFunction> fragment, NSUInteger samples) {
  MTLRenderPipelineDescriptor *desc = [MTLRenderPipelineDescriptor new];
  desc.vertexFunction = vertex; desc.fragmentFunction = fragment;
  desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
  desc.rasterSampleCount = samples;
  NSError *error = nil;
  id<MTLRenderPipelineState> result = [device newRenderPipelineStateWithDescriptor:desc error:&error];
  metal_error(error, "pipeline creation"); require(result != nil, "nil pipeline"); return result;
}
static std::vector<uint8_t> image(id<MTLDevice> device, id<MTLCommandQueue> queue,
                                  id<MTLLibrary> production, id<MTLLibrary> fill, const Case &c) {
  MTLTextureDescriptor *sourceDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                     width:4 height:3 mipmapped:NO];
  sourceDesc.textureType = c.ms ? MTLTextureType2DMultisample : MTLTextureType2D;
  sourceDesc.sampleCount = c.ms ? 4 : 1;
  sourceDesc.storageMode = MTLStorageModePrivate;
  sourceDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
  id<MTLTexture> source = [device newTextureWithDescriptor:sourceDesc];
  MTLTextureDescriptor *targetDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                     width:c.width height:c.height mipmapped:NO];
  targetDesc.storageMode = MTLStorageModePrivate;
  targetDesc.usage = MTLTextureUsageRenderTarget;
  id<MTLTexture> target = [device newTextureWithDescriptor:targetDesc];
  require(source != nil && target != nil, "nil source/target texture");
  id<MTLFunction> vertex = [production newFunctionWithName:@"vs_present_quad"];
  require(vertex != nil, "production vs_present_quad missing");
  MTLFunctionConstantValues *constants = [MTLFunctionConstantValues new];
  bool values[6] = {c.matched, false, false, false, c.ms, false};
  constexpr NSUInteger indices[] = {
    dxmt::kPresentFCIndex_BackbufferSizeMatched, dxmt::kPresentFCIndex_HDRPQ,
    dxmt::kPresentFCIndex_WithHDRMetadata, dxmt::kPresentFCIndex_BackbufferIsSRGB,
    dxmt::kPresentFCIndex_BackbufferIsMS, dxmt::kPresentFCIndex_GammaEnabled};
  for (unsigned i = 0; i < 6; ++i)
    [constants setConstantValue:&values[i] type:MTLDataTypeBool atIndex:indices[i]];
  NSError *error = nil;
  id<MTLFunction> fragment = [production newFunctionWithName:@"fs_present_quad" constantValues:constants error:&error];
  metal_error(error, "production fs_present_quad specialization"); require(fragment != nil, "nil production fragment");
  auto presentPipeline = pipeline(device, vertex, fragment, 1);
  auto fillPipeline = pipeline(device, [fill newFunctionWithName:@"fixture_vertex"],
                               [fill newFunctionWithName:@"fixture_fragment"], sourceDesc.sampleCount);
  id<MTLCommandBuffer> command = [queue commandBuffer]; require(command != nil, "nil command buffer");
  MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
  pass.colorAttachments[0].texture = source;
  pass.colorAttachments[0].loadAction = MTLLoadActionClear;
  pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 1, 1);
  pass.colorAttachments[0].storeAction = MTLStoreActionStore;
  id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
  require(encoder != nil, "nil source fill encoder");
  [encoder setRenderPipelineState:fillPipeline]; [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
  [encoder endEncoding];
  pass = [MTLRenderPassDescriptor renderPassDescriptor];
  pass.colorAttachments[0].texture = target;
  pass.colorAttachments[0].loadAction = MTLLoadActionClear;
  pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
  pass.colorAttachments[0].storeAction = MTLStoreActionStore;
  encoder = [command renderCommandEncoderWithDescriptor:pass]; require(encoder != nil, "nil present encoder");
  [encoder setRenderPipelineState:presentPipeline];
  [encoder setViewport:MTLViewport{double(c.x), double(c.y), double(c.vw), double(c.vh), 0, 1}];
  [encoder setFragmentTexture:source atIndex:0];
  const Metadata metadata{1, 1000, 1000};
  [encoder setFragmentBytes:&metadata length:sizeof(metadata) atIndex:0];
  [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3]; [encoder endEncoding];
  constexpr NSUInteger stride = 256;
  id<MTLBuffer> readback = [device newBufferWithLength:stride * c.height options:MTLResourceStorageModeShared];
  require(readback != nil, "nil readback buffer");
  id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder]; require(blit != nil, "nil readback encoder");
  [blit copyFromTexture:target sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOrigin{0, 0, 0}
             sourceSize:MTLSize{c.width, c.height, 1} toBuffer:readback destinationOffset:0
     destinationBytesPerRow:stride destinationBytesPerImage:stride * c.height];
  [blit endEncoding]; [command commit]; [command waitUntilCompleted];
  metal_error(command.error, "GPU command error"); require(command.status == MTLCommandBufferStatusCompleted, "GPU not completed");
  std::vector<uint8_t> pixels(c.width * c.height * 4);
  const auto *bytes = static_cast<const uint8_t *>(readback.contents);
  for (NSUInteger y = 0; y < c.height; ++y) std::memcpy(pixels.data() + y * c.width * 4, bytes + y * stride, c.width * 4);
  return pixels;
}
int main(int argc, const char **argv) {
  if (argc < 2 || argc > 3 || (argc == 3 && std::strcmp(argv[2], "--wrong-golden"))) {
    std::fprintf(stderr, "usage: fixture /absolute/dxmt_command.metallib [--wrong-golden]\n"); return 2;
  }
  const bool wrong = argc == 3;
  std::thread([] { std::this_thread::sleep_for(std::chrono::seconds(25)); std::_Exit(124); }).detach();
  @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device || ![device supportsTextureSampleCount:4]) { std::fprintf(stderr, "UNSUPPORTED native device/MSAA4\n"); return 2; }
    NSError *error = nil;
    auto production = [device newLibraryWithURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[1]]] error:&error];
    metal_error(error, "load production metallib"); require(production != nil, "nil production library");
    NSString *fillSource = @"#include <metal_stdlib>\nusing namespace metal;\n"
      "vertex float4 fixture_vertex(uint id [[vertex_id]]) { float2 uv=float2((id<<1)&2,id&2); return float4(uv*float2(2,-2)+float2(-1,1),0,1); }\n"
      "fragment float4 fixture_fragment(float4 p [[position]]) { return p.x < 2 ? float4(1,0,0,1) : float4(0,1,0,1); }\n";
    auto fill = [device newLibraryWithSource:fillSource options:nil error:&error];
    metal_error(error, "fixture fill shader compile"); require(fill != nil, "nil fill shader library");
    auto queue = [device newCommandQueue]; require(queue != nil, "nil queue");
    for (const auto &c : cases) {
      const auto pixels = image(device, queue, production, fill, c);
      for (NSUInteger y = 0; y < c.height; ++y) for (NSUInteger x = 0; x < c.width; ++x) {
        std::array<uint8_t, 4> expected{0, 0, 0, 255};
        if (x >= c.x && x < c.x + c.vw && y >= c.y && y < c.y + c.vh)
          expected = (x - c.x) < c.vw / 2 ? std::array<uint8_t, 4>{255, 0, 0, 255} : std::array<uint8_t, 4>{0, 255, 0, 255};
        if (wrong && x == c.x && y == c.y) expected = {0, 0, 255, 255};
        const auto *actual = pixels.data() + (y * c.width + x) * 4;
        if (std::memcmp(actual, expected.data(), 4)) {
          std::printf("{\"case\":\"%s\",\"pass\":false,\"gpuCompleted\":true,\"x\":%lu,\"y\":%lu,\"actual\":[%u,%u,%u,%u],\"expected\":[%u,%u,%u,%u]}\n",
                      c.name, (unsigned long)x, (unsigned long)y, actual[0], actual[1], actual[2], actual[3],
                      expected[0], expected[1], expected[2], expected[3]);
          return 20;
        }
      }
      std::printf("{\"case\":\"%s\",\"pass\":true,\"gpuCompleted\":true,\"pixelsChecked\":%lu}\n", c.name, (unsigned long)(c.width*c.height));
    }
    std::puts("{\"summary\":\"pass\",\"cases\":6,\"scope\":\"offscreen-native-production-present-shaders\"}");
  }
  return 0;
}
