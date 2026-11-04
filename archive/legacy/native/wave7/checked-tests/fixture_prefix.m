#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>
#import <Metal/Metal.h>
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include "winemetal_thunks.h"

typedef int NTSTATUS;
#define STATUS_SUCCESS 0
#define STATUS_UNSUCCESSFUL 0xC0000001

/* Cocoa request source uses exact production PerformRequest/OnMainThreadAsync.
 * No application, drawable, Metal device, renderer, or installed bridge runs. */
@interface WineApplicationController : NSObject {
@public dispatch_queue_t requestsManipQueue;
  NSMutableArray *requests;
  CFRunLoopSourceRef requestSource;
}
+(id)sharedController;
@end
static WineApplicationController *controller;
@implementation WineApplicationController
+(id)sharedController { return controller; }
@end

static int writes, refreshes, creates, creation_failures, releases, executor_calls, throw_at;
static bool fail_create, reject_executor;
static dispatch_block_t write_hook;
static int layer_deallocs;

@interface FixtureMetalLayer : NSObject {
@public id _device;
  BOOL _opaque, _framebufferOnly, _displaySyncEnabled, _wantsExtendedDynamicRangeContent;
  CGFloat _contentsScale;
  CGSize _drawableSize;
  MTLPixelFormat _pixelFormat;
  NSString *_contentsGravity;
  CGColorRef _backgroundColor;
  CGColorSpaceRef _colorspace;
}
@property(assign) id device;
@property(assign) BOOL opaque, framebufferOnly, displaySyncEnabled, wantsExtendedDynamicRangeContent;
@property(assign) CGFloat contentsScale;
@property(assign) CGSize drawableSize;
@property(assign) MTLPixelFormat pixelFormat;
@property(assign) NSString *contentsGravity;
@property(assign) CGColorRef backgroundColor;
@property(assign) CGColorSpaceRef colorspace;
@end

static void observe_write(void) {
  assert([NSThread isMainThread]);
  writes++;
  if (write_hook) { dispatch_block_t hook=write_hook; write_hook=nil; hook(); }
  if (throw_at && writes==throw_at) [NSException raise:@"FixtureSetterFailure" format:@"expected"];
}
#define ACCESSORS(TYPE, NAME, SETNAME) \
  -(TYPE)NAME { return _##NAME; } \
  -(void)SETNAME:(TYPE)value { observe_write(); _##NAME=value; }
@implementation FixtureMetalLayer
ACCESSORS(id, device, setDevice)
ACCESSORS(BOOL, opaque, setOpaque)
ACCESSORS(BOOL, framebufferOnly, setFramebufferOnly)
ACCESSORS(BOOL, displaySyncEnabled, setDisplaySyncEnabled)
ACCESSORS(BOOL, wantsExtendedDynamicRangeContent, setWantsExtendedDynamicRangeContent)
ACCESSORS(CGFloat, contentsScale, setContentsScale)
ACCESSORS(CGSize, drawableSize, setDrawableSize)
ACCESSORS(MTLPixelFormat, pixelFormat, setPixelFormat)
ACCESSORS(NSString *, contentsGravity, setContentsGravity)
ACCESSORS(CGColorRef, backgroundColor, setBackgroundColor)
-(CGColorSpaceRef)colorspace { return _colorspace; }
-(void)setColorspace:(CGColorSpaceRef)value {
  observe_write();
  if (value) CGColorSpaceRetain(value);
  if (_colorspace) CGColorSpaceRelease(_colorspace);
  _colorspace=value;
}
-(void)dealloc { if (_colorspace) CGColorSpaceRelease(_colorspace); layer_deallocs++; [super dealloc]; }
@end
#undef ACCESSORS

static CGColorSpaceRef fixture_create(CFStringRef name) {
  creates++;
  if (fail_create) { creation_failures++; return NULL; }
  return CGColorSpaceCreateWithName(name);
}
static void fixture_release(CGColorSpaceRef colorspace) {
  releases++;
  CGColorSpaceRelease(colorspace);
}
static bool ds3_process_gate(void) { return false; }
static MTLPixelFormat to_metal_pixel_format(enum WMTPixelFormat format) { return (MTLPixelFormat)format; }
static void w7ds_refresh_layer_main(FixtureMetalLayer *layer) { (void)layer; refreshes++; }

/* These replacements expose failed preparation and all layer writes. */
#define CAMetalLayer FixtureMetalLayer
#define CGColorSpaceCreateWithName fixture_create
#define CGColorSpaceRelease fixture_release
