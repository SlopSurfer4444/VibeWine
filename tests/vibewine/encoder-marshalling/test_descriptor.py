#!/usr/bin/env python3
"""Execute the actual render-pass bridge against recording Objective-C objects.

Foundation supplies only NSObject lifetime/runtime support. Metal headers supply
types; the fixture does not link Metal, create a device, or execute GPU work.
The same whole production function runs with the getter reference flag undefined,
explicitly zero, and explicitly one; only negative controls mutate its source.
All generated programs/binaries are private temporary files removed on exit.
"""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import tempfile

REPO = Path(__file__).resolve().parents[3]
SOURCE = REPO / "src/winemetal/unix/winemetal_unix.c"
HEADER = REPO / "src/winemetal/winemetal.h"
THUNKS = REPO / "src/winemetal/winemetal_thunks.h"

# The expected inputs are mapped independently of the extracted implementation.
# (Objective-C property, type, corresponding WMT field)
COLOR = [
    ("clearColor", "MTLClearColor", "clear_color"),
    ("level", "NSUInteger", "level"),
    ("slice", "NSUInteger", "slice"),
    ("depthPlane", "NSUInteger", "depth_plane"),
    ("texture", "id<MTLTexture>", "texture"),
    ("loadAction", "MTLLoadAction", "load_action"),
    ("storeAction", "MTLStoreAction", "store_action"),
    ("resolveTexture", "id<MTLTexture>", "resolve_texture"),
    ("resolveLevel", "NSUInteger", "resolve_level"),
    ("resolveSlice", "NSUInteger", "resolve_slice"),
    ("resolveDepthPlane", "NSUInteger", "resolve_depth_plane"),
]
DEPTH = [("clearDepth", "double", "clear_depth")] + COLOR[3:4] + COLOR[1:3] + COLOR[4:7]
STENCIL = [("clearStencil", "uint32_t", "clear_stencil")] + DEPTH[1:]
DESCRIPTOR = [
    ("defaultRasterSampleCount", "NSUInteger", "default_raster_sample_count"),
    ("renderTargetArrayLength", "NSUInteger", "render_target_array_length"),
    ("renderTargetHeight", "NSUInteger", "render_target_height"),
    ("renderTargetWidth", "NSUInteger", "render_target_width"),
    ("visibilityResultBuffer", "id<MTLBuffer>", "visibility_buffer"),
    ("tileWidth", "NSUInteger", "tile_width"),
    ("tileHeight", "NSUInteger", "tile_height"),
]


def extract_function(text):
    start = text.index("static NTSTATUS\n_MTLCommandBuffer_renderCommandEncoder(void *obj)")
    end = text.index("\nstatic NTSTATUS", start + 1)
    return text[start:end]


def recording_class(name, fields, extra_ivars="", extra_properties="", extra_methods="", setter_hook=""):
    ivars = "".join(f"  {kind} v_{prop}; unsigned n_{prop};\n" for prop, kind, _ in fields)
    properties = "".join(f"@property(nonatomic, assign) {kind} {prop};\n" for prop, kind, _ in fields)
    methods = "".join(
        f"- ({kind}){prop} {{ return v_{prop}; }}\n"
        f"- (void)set{prop[0].upper() + prop[1:]}:({kind})value {{ v_{prop} = value; ++n_{prop}; {setter_hook} }}\n"
        for prop, kind, _ in fields
    )
    return (f"@interface {name} : NSObject {{ @public\n{ivars}{extra_ivars}}}\n"
            f"{properties}{extra_properties}@end\n@implementation {name}\n{methods}{extra_methods}@end\n")


def validations(fields, target, source, enabled="1"):
    result = ""
    for prop, kind, field in fields:
        result += f'CHECK({target}->n_{prop} == ({enabled}), "{target}.{prop} setter count");\n'
        if kind == "MTLClearColor":
            for channel, component in zip(("red", "green", "blue", "alpha"), "rgba"):
                result += f'CHECK({target}->v_{prop}.{channel} == {source}.{field}.{component}, "color {component}");\n'
        else:
            cast = "(uintptr_t)" if kind.startswith("id") else ""
            # Unset attachments and tiles must retain their original zero state.
            result += (f'CHECK({cast}{target}->v_{prop} == (({enabled}) ? '
                       f'{cast}{source}.{field} : 0), "{target}.{prop} value");\n')
    return result


def fixture(body, wmt_types, thunk_type, reference_getters=False):
    prelude = r'''
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint64_t obj_handle_t;
typedef int NTSTATUS;
#define STATUS_SUCCESS 0
#define CHECK(condition, message) do { if (!(condition)) { \
  fprintf(stderr, "%s:%d: %s\n", scenario, __LINE__, message); exit(1); } } while (0)
static const char *scenario;
static unsigned color_getters, slot_getters, color_setters, depth_getters, stencil_getters;
static unsigned descriptor_created, descriptor_destroyed, encoder_calls;
static struct WMTRenderPassInfo *expected;
static id encoder_result;
'''
    classes = recording_class("FixtureColor", COLOR, setter_hook="++color_setters;")
    classes += recording_class("FixtureDepth", DEPTH)
    classes += recording_class("FixtureStencil", STENCIL)
    classes += r'''
@interface FixtureColors : NSObject { @public FixtureColor *slots[8]; }
- (FixtureColor *)objectAtIndexedSubscript:(NSUInteger)index;
@end
@implementation FixtureColors
- (id)init { self = [super init]; if (self) for (unsigned i=0; i<8; ++i) slots[i] = [FixtureColor new]; return self; }
- (FixtureColor *)objectAtIndexedSubscript:(NSUInteger)index { CHECK(index < 8, "slot bounds"); ++slot_getters; return slots[index]; }
- (void)dealloc { for (unsigned i=0; i<8; ++i) [slots[i] release]; [super dealloc]; }
@end
'''
    classes += recording_class("FixtureDescriptor", DESCRIPTOR,
        extra_ivars="  FixtureColors *colors; FixtureDepth *depth; FixtureStencil *stencil;\n",
        extra_properties="@property(nonatomic, readonly) FixtureColors *colorAttachments;\n"
                         "@property(nonatomic, readonly) FixtureDepth *depthAttachment;\n"
                         "@property(nonatomic, readonly) FixtureStencil *stencilAttachment;\n",
        extra_methods=r'''
- (id)init { self = [super init]; if (self) { ++descriptor_created; colors = [FixtureColors new]; depth = [FixtureDepth new]; stencil = [FixtureStencil new]; } return self; }
- (FixtureColors *)colorAttachments { ++color_getters; return colors; }
- (FixtureDepth *)depthAttachment { ++depth_getters; return depth; }
- (FixtureStencil *)stencilAttachment { ++stencil_getters; return stencil; }
- (void)dealloc { ++descriptor_destroyed; [colors release]; [depth release]; [stencil release]; [super dealloc]; }
''')
    validate = "static void validate(FixtureDescriptor *d) {\n"
    validate += "for (unsigned i=0; i<8; ++i) { FixtureColor *c = d->colors->slots[i];\n"
    validate += validations(COLOR, "c", "expected->colors[i]") + "}\n"
    validate += "FixtureDepth *depth = d->depth; FixtureStencil *stencil = d->stencil;\n"
    validate += validations(DEPTH, "depth", "expected->depth", "!!expected->depth.texture")
    validate += validations(STENCIL, "stencil", "expected->stencil", "!!expected->stencil.texture")
    validate += validations(DESCRIPTOR[:5], "d", "(*expected)")
    validate += validations(DESCRIPTOR[5:], "d", "(*expected)", "!!(expected->tile_width && expected->tile_height)")
    validate += "}\n"
    callback = r'''
@protocol FixtureCommandBufferProtocol <NSObject>
- (id)renderCommandEncoderWithDescriptor:(FixtureDescriptor *)descriptor;
@end
@interface FixtureCommandBuffer : NSObject <FixtureCommandBufferProtocol>
@end
@implementation FixtureCommandBuffer
- (id)renderCommandEncoderWithDescriptor:(FixtureDescriptor *)descriptor {
  ++encoder_calls;
  CHECK(descriptor_created == 1 && descriptor_destroyed == 0, "descriptor alive during callback");
  validate(descriptor);
  return encoder_result;
}
@end
#define MTLRenderPassDescriptor FixtureDescriptor
#define MTLRenderPassColorAttachmentDescriptorArray FixtureColors
#define MTLRenderPassColorAttachmentDescriptor FixtureColor
#define MTLRenderPassDepthAttachmentDescriptor FixtureDepth
#define MTLRenderPassStencilAttachmentDescriptor FixtureStencil
#define MTLCommandBuffer FixtureCommandBufferProtocol
'''
    harness = r'''
static void run(const char *name, struct WMTRenderPassInfo info, BOOL return_nil, BOOL nil_command_buffer) {
  scenario = name; expected = &info;
  color_getters = slot_getters = color_setters = depth_getters = stencil_getters = 0;
  descriptor_created = descriptor_destroyed = encoder_calls = 0;
  FixtureCommandBuffer *command_buffer = nil_command_buffer ? nil : [FixtureCommandBuffer new];
  NSObject *sentinel = [NSObject new]; encoder_result = return_nil ? nil : sentinel;
  struct unixcall_generic_obj_uint64_obj_ret params = {(obj_handle_t)command_buffer, (uint64_t)&info, UINT64_MAX};
  CHECK(_MTLCommandBuffer_renderCommandEncoder(&params) == STATUS_SUCCESS, "status");
  CHECK(params.ret == (obj_handle_t)(nil_command_buffer ? nil : encoder_result), "encoder return including nil");
  CHECK(encoder_calls == (nil_command_buffer ? 0u : 1u), "one encoder callback");
  CHECK(descriptor_created == 1 && descriptor_destroyed == 1, "descriptor released once");
  CHECK(color_getters == EXPECT_COLOR_GETTERS && slot_getters == EXPECT_SLOT_GETTERS, "color getter count");
  CHECK(color_setters == 88, "88 color setters");
  CHECK(depth_getters == (!!info.depth.texture ? EXPECT_ATTACHMENT_GETTERS : 0u), "depth getter count");
  CHECK(stencil_getters == (!!info.stencil.texture ? EXPECT_ATTACHMENT_GETTERS : 0u), "stencil getter count");
  printf("%s:passed:color_getters=%u:slot_getters=%u:color_dispatches=%u:depth_getters=%u:stencil_getters=%u\n",
         name, color_getters, slot_getters, color_getters + slot_getters + color_setters, depth_getters, stencil_getters);
  [command_buffer release]; [sentinel release];
}
int main(void) { @autoreleasepool {
  struct WMTRenderPassInfo empty = {0};
  run("empty_fields_nil_encoder", empty, YES, NO);
  struct WMTRenderPassInfo mrt = {0};
  for (unsigned i=0; i<8; ++i) {
    struct WMTColorAttachmentInfo *c = &mrt.colors[i];
    c->texture = 0x1000 + i; c->resolve_texture = 0x2000 + i;
    c->level = 11+i; c->slice = 21+i; c->depth_plane = 0x10000+i;
    c->clear_color = (struct WMTClearColor){i+0.125, i+0.25, i+0.5, i+0.75};
    c->load_action = (enum WMTLoadAction)(i%3);
    c->store_action = (enum WMTStoreAction)(i%6);
    c->resolve_level = 31+i; c->resolve_slice = 41+i; c->resolve_depth_plane = 0x20000+i;
  }
  mrt.depth = (struct WMTDepthAttachmentInfo){0x3000, WMTLoadActionClear, WMTStoreActionStore, 51, 61, 0x30000, 0.625f};
  mrt.stencil = (struct WMTStencilAttachmentInfo){0x4000, WMTLoadActionLoad, WMTStoreActionDontCare, 52, 62, 0x40000, 219};
  mrt.default_raster_sample_count = 4; mrt.render_target_array_length = 3;
  mrt.render_target_height = 1080; mrt.render_target_width = 1920;
  mrt.visibility_buffer = 0x5000; mrt.tile_width = 16; mrt.tile_height = 32;
  run("eight_mrt_msaa_resolve_depth_stencil_tile", mrt, NO, NO);
  struct WMTRenderPassInfo sparse = mrt;
  for (unsigned i=1; i<8; ++i) memset(&sparse.colors[i], 0, sizeof(sparse.colors[i]));
  sparse.depth.texture = 0; sparse.tile_height = 0;
  run("sparse_stencil_tile_width_only", sparse, NO, NO);
  sparse = mrt; sparse.stencil.texture = 0; sparse.tile_width = 0;
  run("depth_tile_height_only_nil_encoder", sparse, YES, NO);
  sparse = mrt; sparse.tile_width = sparse.tile_height = 0;
  sparse.colors[0].level = UINT16_MAX; sparse.colors[0].slice = UINT16_MAX;
  sparse.colors[0].depth_plane = UINT32_MAX; sparse.colors[0].resolve_depth_plane = UINT32_MAX;
  sparse.render_target_width = UINT32_MAX; sparse.render_target_height = UINT32_MAX;
  sparse.default_raster_sample_count = UINT8_MAX; sparse.render_target_array_length = UINT8_MAX;
  run("wide_fields_no_tiles", sparse, NO, NO);
  run("nil_command_buffer", empty, NO, YES);
} return 0; }
'''
    counters = (88, 88, 7) if reference_getters else (1, 8, 1)
    defines = "".join(f"#define {name} {count}u\n" for name, count in zip(
        ("EXPECT_COLOR_GETTERS", "EXPECT_SLOT_GETTERS", "EXPECT_ATTACHMENT_GETTERS"), counters))
    return prelude + wmt_types + thunk_type + classes + validate + callback + body + defines + harness


def main():
    raw = SOURCE.read_text()
    body = extract_function(raw)
    header = HEADER.read_text()
    wmt_types = header[header.index("enum WMTLoadAction {"):header.index("WINEMETAL_API obj_handle_t MTLCommandBuffer_renderCommandEncoder")]
    thunks = THUNKS.read_text()
    thunk_type = re.search(r"struct unixcall_generic_obj_uint64_obj_ret \{[^}]+\};", thunks).group(0)
    results = []
    with tempfile.TemporaryDirectory(prefix="dxmt-descriptor-") as scratch:
        missing_slice, deleted = re.subn(
            r"^[ \t]+W4_COLOR_ATTACHMENT\.resolveSlice = info->colors\[i\]\.resolve_slice;\n", "", body, flags=re.M)
        corrupted_return = body.replace("  [descriptor release];", "  params->ret = 1;\n  [descriptor release];")
        assert deleted == 1 and corrupted_return != body
        variants = [("default_undefined", body, None, None)]
        for mode in (0, 1):
            variants.extend([
                (f"explicit_{mode}", body, mode, None),
                (f"missing_resolve_slice_control_{mode}", missing_slice, mode, "c.resolveSlice setter count"),
                (f"nil_return_corruption_control_{mode}", corrupted_return, mode, "encoder return including nil"),
            ])
        for name, source_body, mode, expected_error in variants:
            program = Path(scratch) / (name + ".m")
            binary = program.with_suffix("")
            program.write_text(fixture(source_body, wmt_types, thunk_type, mode == 1))
            command = ["xcrun", "clang", "-Wall", "-Wextra", "-Werror", "-O2", "-fno-objc-arc",
                       "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                       str(program), "-framework", "Foundation", "-o", str(binary)]
            if mode is not None:
                command.insert(2, f"-DDXMT_PERF_REF_W4_GETTERS={mode}")
            compiled = subprocess.run(command, capture_output=True, text=True, timeout=60)
            assert compiled.returncode == 0, (name, compiled.stdout, compiled.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
            if expected_error is None:
                assert result.returncode == 0, (name, result.stdout, result.stderr)
            else:
                assert result.returncode != 0 and expected_error in result.stderr, (name, result.stdout, result.stderr)
            results.append({"variant": name, "expected_failure": expected_error,
                            "cases": result.stdout.splitlines(), "stderr": result.stderr})
    by_name = {result["variant"]: result for result in results}
    assert by_name["default_undefined"]["cases"] == by_name["explicit_0"]["cases"], "default differs from explicit optimized mode"
    print(json.dumps({"passed": True,
                      "scope": "CPU Objective-C recording fixture; no Metal device/GPU/Wine/game execution",
                      "function_sha256": hashlib.sha256(body.encode()).hexdigest(),
                      "results": results}, indent=2))


if __name__ == "__main__":
    main()
