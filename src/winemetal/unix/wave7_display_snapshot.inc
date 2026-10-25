/* Display metadata and main-owned AppKit snapshots. Included after native thunk
 * types/STATUS_SUCCESS. Never wait for the main thread from any getter here.
 * Existing setters/acquisition call the main-only prime/refresh hooks below. */
#include <pthread.h>
#include <assert.h>
#include <objc/runtime.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <time.h>
#include "../wave7_visibility.h"

bool GetDisplayColorGamut(ColorSyncProfileRef profile, struct WMTDisplayDescription *desc_out);
NSScreen *GetNSScreenForDisplayID(CGDirectDisplayID display_id);

struct W7DSLayerSnapshot {
  struct WMTLayerProps props;
  struct WMTEDRValue edr;
  CGDirectDisplayID display_id;
  BOOL display_valid;
  BOOL visibility_known, occluded;
  BOOL window_visible;
  uint64_t visibility_observed_ns;
};
struct W7DSDisplayState {
  struct W7DSDisplayState *next;
  CGDirectDisplayID display_id;
  uint64_t version;
  BOOL enabled, description_ready;
  enum WMTColorSpace colorspace;
  struct WMTHDRMetadata metadata;
  struct WMTDisplayDescription description;
};
/* Preserve the old exported data symbol's exact type and size for binary
 * symbol compatibility. No supported caller declares this storage; active
 * metadata belongs to the ID-keyed locked records below. Direct consumers of
 * the old mutable array have no supported semantic contract in this candidate. */
struct DisplaySetting {
  uint64_t version;
  enum WMTColorSpace colorspace;
  struct WMTHDRMetadata hdr_metadata;
};
__attribute__((visibility("default")))
struct DisplaySetting g_display_settings[2] = {{0, 0, {}}, {0, 0, {}}};

static pthread_mutex_t w7ds_lock = PTHREAD_MUTEX_INITIALIZER;
static struct W7DSDisplayState *w7ds_display_settings;
static uint64_t w7ds_next_version = 1;
static uint64_t w7ds_description_epoch;
static BOOL w7ds_setup_queued, w7ds_observer_ready;
static char w7ds_layer_key;
/* Access to these weak collections and notification registration is main-only. */
static NSHashTable *w7ds_layers, *w7ds_closed_windows;

@interface W7DSLayerState : NSObject {
@public
  struct W7DSLayerSnapshot snapshot;
  BOOL ready, refresh_queued;
  /* Pulled only by the optional presentation diagnostic; independent of skip. */
  struct W7VSSnapshot diagnostic_visibility;
  BOOL diagnostic_visibility_queued;
@package
  BOOL acquired_drawable;
  id visibility_window; /* runtime-managed weak storage; main-only access */
@public
  id device_owner;
}
@end
@implementation W7DSLayerState
- (id)init {
  if ((self = [super init])) {
    objc_storeWeak(&visibility_window,nil);
    snapshot.props.contents_scale = 1.0;
    snapshot.edr.maximum_edr_color_component_value = 1.0;
    snapshot.edr.maximum_potential_edr_color_component_value = 1.0;
  }
  return self;
}
- (void)dealloc { objc_storeWeak(&visibility_window,nil); [device_owner release]; [super dealloc]; }
@end

static void w7ds_default_description(struct WMTDisplayDescription *desc) {
  memset(desc, 0, sizeof(*desc));
  desc->red_primaries[0]=0.64f; desc->red_primaries[1]=0.33f;
  desc->green_primaries[0]=0.30f; desc->green_primaries[1]=0.60f;
  desc->blue_primaries[0]=0.15f; desc->blue_primaries[1]=0.06f;
  desc->white_points[0]=0.3127f; desc->white_points[1]=0.3290f;
  desc->maximum_edr_color_component_value=1.0f;
  desc->maximum_potential_edr_color_component_value=1.0f;
}
/* Caller holds w7ds_lock; records have process lifetime, never raw layer keys. */
static struct W7DSDisplayState *w7ds_display_locked(CGDirectDisplayID display_id, BOOL create) {
  struct W7DSDisplayState *state;
  for (state=w7ds_display_settings; state; state=state->next)
    if (state->display_id==display_id) return state;
  if (!create || !(state=calloc(1,sizeof(*state)))) return NULL;
  state->display_id=display_id;
  w7ds_default_description(&state->description);
  state->next=w7ds_display_settings; w7ds_display_settings=state;
  return state;
}
/* Caller owns layer through the synchronous API. No AppKit/property message is
 * sent here; the associated state never retains its layer or delegate/window. */
static W7DSLayerState *w7ds_state(CAMetalLayer *layer) {
  if (!layer) return nil;
  pthread_mutex_lock(&w7ds_lock);
  W7DSLayerState *state=objc_getAssociatedObject(layer,&w7ds_layer_key);
  if (!state) {
    state=[[W7DSLayerState alloc] init];
    if (state) {
      objc_setAssociatedObject(layer,&w7ds_layer_key,state,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
      [state release];
    }
  }
  [state retain];
  pthread_mutex_unlock(&w7ds_lock);
  return state;
}
static BOOL w7ds_prime_layer_main(CAMetalLayer *layer);
static void w7ds_refresh_layer_main(CAMetalLayer *layer);
static void w7ds_refresh_description_main(CGDirectDisplayID display_id);
static void w7ds_prime_displays_main(void);

/* Experimental presentation-only admission. Unknown state and the first
 * successful drawable always pass; no AppKit query or main wait is done here.
 * Associated state belongs to the actual layer, not a reusable pointer key. */
static BOOL w7ds_skip_occluded_enabled;
#define W7DS_VISIBILITY_REFRESH_NS UINT64_C(250000000)
#define W7DS_VISIBILITY_EXPIRE_NS UINT64_C(1000000000)
static uint64_t w7ds_visibility_now(void) {
  struct timespec time;
  if (clock_gettime(CLOCK_MONOTONIC_RAW,&time) || time.tv_sec<0 || time.tv_nsec<0 || time.tv_nsec>=1000000000 ||
      (uint64_t)time.tv_sec>(UINT64_MAX-(uint64_t)time.tv_nsec)/UINT64_C(1000000000)) return 0;
  return (uint64_t)time.tv_sec*UINT64_C(1000000000)+(uint64_t)time.tv_nsec;
}
static void __attribute__((constructor)) w7ds_occlusion_init(void) {
  const char *mode=getenv("DXMT_SKIP_OCCLUDED_DRAWABLES");
  w7ds_skip_occluded_enabled=mode && !strcmp(mode,"1");
}
static BOOL w7ds_should_skip_drawable(CAMetalLayer *layer) {
  if (!w7ds_skip_occluded_enabled || !layer) return NO;
  uint64_t now=w7ds_visibility_now();
  pthread_mutex_lock(&w7ds_lock);
  W7DSLayerState *state=objc_getAssociatedObject(layer,&w7ds_layer_key);
  uint64_t observed=state ? state->snapshot.visibility_observed_ns : 0;
  BOOL clock_valid=now && observed && now>=observed;
  uint64_t age=clock_valid ? now-observed : UINT64_MAX;
  BOOL schedule=state && state->ready && clock_valid && age>=W7DS_VISIBILITY_REFRESH_NS && !state->refresh_queued;
  if (schedule) state->refresh_queued=YES;
  BOOL skip=state && state->ready && state->acquired_drawable &&
            state->snapshot.visibility_known && state->snapshot.occluded && age<W7DS_VISIBILITY_EXPIRE_NS;
  pthread_mutex_unlock(&w7ds_lock);
  /* Healthy main servicing refreshes stable occlusion without forcing a native
   * probe. If main cannot refresh before hard expiry, allow recovery work. */
  if (schedule) dispatch_async(dispatch_get_main_queue(), ^{ w7ds_refresh_layer_main(layer); });
  return skip;
}
static void w7ds_did_acquire_drawable(CAMetalLayer *layer) {
  if (!w7ds_skip_occluded_enabled || !layer) return;
  pthread_mutex_lock(&w7ds_lock);
  W7DSLayerState *state=objc_getAssociatedObject(layer,&w7ds_layer_key);
  if (state) state->acquired_drawable=YES;
  pthread_mutex_unlock(&w7ds_lock);
}

@interface W7DSObserver : NSObject
- (void)changed:(NSNotification *)notification;
@end
@implementation W7DSObserver
- (void)changed:(NSNotification *)notification {
  if (![NSThread isMainThread]) {
    dispatch_async(dispatch_get_main_queue(), ^{ [self changed:notification]; });
    return;
  }
  if ([notification.name isEqualToString:NSWindowWillCloseNotification] && notification.object)
    [w7ds_closed_windows addObject:notification.object];
  if ([notification.name isEqualToString:NSApplicationDidChangeScreenParametersNotification]) {
    pthread_mutex_lock(&w7ds_lock);
    ++w7ds_description_epoch;
    for (struct W7DSDisplayState *state=w7ds_display_settings; state; state=state->next) {
      state->description_ready=NO;
      w7ds_default_description(&state->description);
    }
    pthread_mutex_unlock(&w7ds_lock);
  }
  /* allObjects pins each weakly registered layer only through this refresh. */
  for (CAMetalLayer *layer in [w7ds_layers allObjects]) w7ds_refresh_layer_main(layer);
  if ([notification.name isEqualToString:NSApplicationDidChangeScreenParametersNotification])
    w7ds_prime_displays_main();
}
@end
static W7DSObserver *w7ds_observer;
static BOOL w7ds_setup_main(void) {
  assert([NSThread isMainThread]);
  if (w7ds_observer) return YES;
  NSHashTable *layers=[[NSHashTable alloc] initWithOptions:NSPointerFunctionsWeakMemory capacity:0];
  NSHashTable *closed_windows=[[NSHashTable alloc] initWithOptions:NSPointerFunctionsWeakMemory capacity:0];
  W7DSObserver *observer=[[W7DSObserver alloc] init];
  if (!layers || !closed_windows || !observer) {
    [layers release]; [closed_windows release]; [observer release];
    return NO;
  }
  w7ds_layers=layers; w7ds_closed_windows=closed_windows; w7ds_observer=observer;
  NSNotificationCenter *center=[NSNotificationCenter defaultCenter];
  for (NSString *name in @[NSApplicationDidChangeScreenParametersNotification,NSApplicationDidBecomeActiveNotification,
                          NSWindowDidChangeScreenNotification,NSWindowDidChangeBackingPropertiesNotification,
                          NSWindowDidResizeNotification,NSWindowWillCloseNotification])
    [center addObserver:w7ds_observer selector:@selector(changed:) name:name object:nil];
  if (w7ds_skip_occluded_enabled)
    for (NSString *name in @[NSWindowDidChangeOcclusionStateNotification,NSWindowDidMiniaturizeNotification,
                            NSWindowDidDeminiaturizeNotification,NSApplicationDidHideNotification,
                            NSApplicationDidUnhideNotification])
      [center addObserver:w7ds_observer selector:@selector(changed:) name:name object:nil];
  pthread_mutex_lock(&w7ds_lock);
  w7ds_observer_ready=YES;
  pthread_mutex_unlock(&w7ds_lock);
  return YES;
}
/* ColorSync profile reads preserve the original thread-safe native query
 * path; no AppKit or Cocoa main-thread dispatch occurs in this helper. */
static void w7ds_collect_gamut(CGDirectDisplayID display_id, struct WMTDisplayDescription *output) {
  struct WMTDisplayDescription desc;
  w7ds_default_description(&desc);
  ColorSyncProfileRef profile=ColorSyncProfileCreateWithDisplayID(display_id);
  BOOL gamut=profile && GetDisplayColorGamut(profile,&desc);
  if (profile) CFRelease(profile);
  if (!gamut) {
    profile=ColorSyncProfileCreateWithName(kColorSyncGenericRGBProfile);
    if (profile) { GetDisplayColorGamut(profile,&desc); CFRelease(profile); }
  }
  *output=desc;
}
static void w7ds_refresh_description_main(CGDirectDisplayID display_id) {
  assert([NSThread isMainThread]);
  struct WMTDisplayDescription desc;
  w7ds_collect_gamut(display_id,&desc);
  NSScreen *screen=GetNSScreenForDisplayID(display_id);
  if (screen) {
    desc.maximum_edr_color_component_value=screen.maximumExtendedDynamicRangeColorComponentValue;
    desc.maximum_reference_edr_color_component_value=screen.maximumReferenceExtendedDynamicRangeColorComponentValue;
    desc.maximum_potential_edr_color_component_value=screen.maximumPotentialExtendedDynamicRangeColorComponentValue;
  }
  pthread_mutex_lock(&w7ds_lock);
  struct W7DSDisplayState *state=w7ds_display_locked(display_id,YES);
  if (state) { state->description=desc; state->description_ready=YES; }
  pthread_mutex_unlock(&w7ds_lock);
}
static void w7ds_prime_displays_main(void) {
  assert([NSThread isMainThread]);
  if (!w7ds_setup_main()) return;
  for (NSScreen *screen in [NSScreen screens]) {
    NSNumber *number=screen.deviceDescription[@"NSScreenNumber"];
    if (number) w7ds_refresh_description_main(number.unsignedIntValue);
  }
}
static BOOL w7ds_prime_layer_main(CAMetalLayer *layer) {
  assert([NSThread isMainThread]);
  if (!layer) return NO;
  W7DSLayerState *state=w7ds_state(layer);
  if (!state) return NO;
  if (!w7ds_setup_main()) { [state release]; return NO; }
  [w7ds_layers addObject:layer];
  struct W7DSLayerSnapshot value={0};
  id device=[layer.device retain];
  value.props.device=(obj_handle_t)device;
  value.props.contents_scale=layer.contentsScale;
  value.props.opaque=layer.opaque;
  value.props.framebuffer_only=layer.framebufferOnly;
  value.props.display_sync_enabled=layer.displaySyncEnabled;
  CGSize size=layer.drawableSize;
  value.props.drawable_width=size.width; value.props.drawable_height=size.height;
  value.props.pixel_format=layer.pixelFormat;
  value.edr.maximum_edr_color_component_value=1.0;
  value.edr.maximum_potential_edr_color_component_value=1.0;
  id delegate=layer.delegate;
  NSWindow *window=[delegate isKindOfClass:[NSView class]] ? [(NSView *)delegate window] : nil;
  id previous_window=nil;
  if (w7ds_skip_occluded_enabled && window) {
    previous_window=[objc_loadWeak(&state->visibility_window) retain];
    objc_storeWeak(&state->visibility_window,window);
    /* A hidden client-view ancestor may need the WineMetalLayer event hook to
     * activate the new surface. Let it run rather than gating on old content. */
    value.visibility_known=![(NSView *)delegate isHiddenOrHasHiddenAncestor];
    value.window_visible=window.visible;
    value.occluded=[w7ds_closed_windows containsObject:window] || !window.visible || window.miniaturized ||
        !(window.occlusionState & NSWindowOcclusionStateVisible);
    value.visibility_observed_ns=w7ds_visibility_now();
  } else if (w7ds_skip_occluded_enabled) {
    previous_window=[objc_loadWeak(&state->visibility_window) retain];
    objc_storeWeak(&state->visibility_window,nil);
    value.visibility_observed_ns=w7ds_visibility_now();
  }
  NSScreen *screen=window && ![w7ds_closed_windows containsObject:window] ? window.screen : nil;
  NSNumber *number=screen.deviceDescription[@"NSScreenNumber"];
  if (number) {
    value.display_valid=YES; value.display_id=number.unsignedIntValue;
    value.edr.maximum_edr_color_component_value=layer.wantsExtendedDynamicRangeContent ?
      screen.maximumExtendedDynamicRangeColorComponentValue : 1.0;
    value.edr.maximum_potential_edr_color_component_value=screen.maximumPotentialExtendedDynamicRangeColorComponentValue;
  }
  pthread_mutex_lock(&w7ds_lock);
  id old_device=state->device_owner;
  if (w7ds_skip_occluded_enabled && (previous_window!=window ||
      (!state->snapshot.window_visible && value.window_visible))) state->acquired_drawable=NO;
  state->device_owner=device;
  state->snapshot=value; state->ready=YES; state->refresh_queued=NO;
  struct W7DSDisplayState *display=value.display_valid ? w7ds_display_locked(value.display_id,YES) : NULL;
  BOOL needs_description=display && !display->description_ready;
  pthread_mutex_unlock(&w7ds_lock);
  [old_device release];
  [previous_window release];
  [state release];
  if (needs_description) w7ds_refresh_description_main(value.display_id);
  return YES;
}
static void w7ds_refresh_layer_main(CAMetalLayer *layer) { (void)w7ds_prime_layer_main(layer); }

static struct W7DSLayerSnapshot w7ds_read_layer(CAMetalLayer *layer,
    struct WMTHDRMetadata *metadata, enum WMTColorSpace *colorspace, uint64_t *version) {
  struct W7DSLayerSnapshot value={0};
  value.props.contents_scale=1.0;
  value.edr.maximum_edr_color_component_value=1.0;
  value.edr.maximum_potential_edr_color_component_value=1.0;
  if (metadata) memset(metadata,0,sizeof(*metadata));
  if (colorspace) *colorspace=0;
  if (version) *version=0;
  W7DSLayerState *state=w7ds_state(layer);
  if (!state) return value;
  pthread_mutex_lock(&w7ds_lock);
  value=state->snapshot;
  BOOL schedule=!state->ready && !state->refresh_queued;
  if (schedule) state->refresh_queued=YES;
  struct W7DSDisplayState *display=value.display_valid ? w7ds_display_locked(value.display_id,NO) : NULL;
  if (display && display->enabled) {
    if (metadata) *metadata=display->metadata;
    if (colorspace) *colorspace=display->colorspace;
    if (version) *version=display->version;
  }
  pthread_mutex_unlock(&w7ds_lock);
  /* One initialization request, not one dispatch per Present. The copied block
   * pins the actual layer until refresh, never a raw cached delegate/window. */
  if (schedule) dispatch_async(dispatch_get_main_queue(), ^{ w7ds_refresh_layer_main(layer); });
  [state release];
  return value;
}
static NTSTATUS _MetalLayer_getProps(void *obj) {
  struct unixcall_generic_obj_ptr_noret *p=obj;
  struct W7DSLayerSnapshot value=w7ds_read_layer((CAMetalLayer *)p->handle,NULL,NULL,NULL);
  *(struct WMTLayerProps *)p->arg.ptr=value.props;
  return STATUS_SUCCESS;
}
static NTSTATUS _MetalLayer_getEDRValue(void *obj) {
  struct unixcall_generic_obj_ptr_noret *p=obj;
  struct W7DSLayerSnapshot value=w7ds_read_layer((CAMetalLayer *)p->handle,NULL,NULL,NULL);
  *(struct WMTEDRValue *)p->arg.ptr=value.edr;
  return STATUS_SUCCESS;
}
static NTSTATUS _WMTQueryDisplaySettingForLayer(void *obj) {
  struct unixcall_query_display_setting_for_layer *p=obj;
  struct W7DSLayerSnapshot value=w7ds_read_layer((CAMetalLayer *)p->layer,p->hdr_metadata.ptr,&p->colorspace,&p->version);
  p->edr_value=value.edr;
  return STATUS_SUCCESS;
}
static NTSTATUS _WMTQueryDisplaySetting(void *obj) {
  struct unixcall_query_display_setting *p=obj;
  p->ret=false; p->colorspace=0;
  memset(p->hdr_metadata.ptr,0,sizeof(struct WMTHDRMetadata));
  pthread_mutex_lock(&w7ds_lock);
  struct W7DSDisplayState *state=w7ds_display_locked(p->display_id,NO);
  if (state && state->enabled) {
    *(struct WMTHDRMetadata *)p->hdr_metadata.ptr=state->metadata;
    p->colorspace=state->colorspace; p->ret=true;
  }
  pthread_mutex_unlock(&w7ds_lock);
  return STATUS_SUCCESS;
}
static NTSTATUS _WMTUpdateDisplaySetting(void *obj) {
  struct unixcall_update_display_setting *p=obj;
  pthread_mutex_lock(&w7ds_lock);
  struct W7DSDisplayState *state=w7ds_display_locked(p->display_id,YES);
  if (state) {
    state->enabled=p->hdr_metadata.ptr && w7ds_next_version;
    if (state->enabled) {
      state->metadata=*(const struct WMTHDRMetadata *)p->hdr_metadata.ptr;
      state->colorspace=p->colorspace; state->version=w7ds_next_version++;
    }
  }
  pthread_mutex_unlock(&w7ds_lock);
  return STATUS_SUCCESS;
}
static NTSTATUS _WMTGetDisplayDescription(void *obj) {
  struct unixcall_generic_obj_ptr_noret *p=obj;
  CGDirectDisplayID display_id=(CGDirectDisplayID)p->handle;
  struct WMTDisplayDescription value;
  w7ds_default_description(&value);
  pthread_mutex_lock(&w7ds_lock);
  struct W7DSDisplayState *state=w7ds_display_locked(display_id,YES);
  BOOL ready=w7ds_observer_ready && state && state->description_ready;
  BOOL bootstrap=!w7ds_observer_ready && !w7ds_setup_queued;
  if (bootstrap) w7ds_setup_queued=YES;
  uint64_t epoch=w7ds_description_epoch;
  if (ready) value=state->description;
  pthread_mutex_unlock(&w7ds_lock);
  if (bootstrap) dispatch_async(dispatch_get_main_queue(), ^{ w7ds_prime_displays_main(); });
  if (!ready) {
    w7ds_collect_gamut(display_id,&value);
    pthread_mutex_lock(&w7ds_lock);
    /* A concurrent topology notification must not be overwritten by an old
     * profile read. Caller receives the completed read; next query refreshes. */
    if (state && epoch==w7ds_description_epoch && !state->description_ready) {
      state->description=value; state->description_ready=YES;
    }
    pthread_mutex_unlock(&w7ds_lock);
  }
  /* The matching Wine driver publishes this cache before COCOA_APP_RUNNING.
   * Its query only locks/copies native state, so even a cold factory/output
   * constructor receives EDR without a main-thread wait. Missing old-driver
   * capability is unsupported pairing, never proof of exact HDR capability. */
  int (*copy_edr)(uint32_t,double[3])=(int (*)(uint32_t,double[3]))
      dlsym(RTLD_DEFAULT,"macdrv_copy_display_edr_v1");
  double edr[3];
  BOOL exact=copy_edr && copy_edr(display_id,edr)==1;
  if (exact) {
    value.maximum_edr_color_component_value=edr[0];
    value.maximum_reference_edr_color_component_value=edr[1];
    value.maximum_potential_edr_color_component_value=edr[2];
  } else {
    value.maximum_edr_color_component_value=1.0;
    value.maximum_reference_edr_color_component_value=0.0;
    value.maximum_potential_edr_color_component_value=1.0;
  }
  *(struct WMTDisplayDescription *)p->arg.ptr=value;
  return exact ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}
