/* Included after the main-owned display state. No timer, observer, collector or
 * synchronous dispatch. An active PE capture pulls at most twice per second. */
static uint64_t w7vs_next_identity=1;
static void w7vs_refresh_main(CAMetalLayer *layer) {
  assert([NSThread isMainThread]);
  W7DSLayerState *state=w7ds_state(layer);
  if (!state) return;
  struct W7VSSnapshot value={0};
  id delegate=layer.delegate;
  NSView *view=[delegate isKindOfClass:[NSView class]] ? (NSView *)delegate : nil;
  NSWindow *window=view.window;
  if (layer.hidden) value.flags|=W7VS_LAYER_HIDDEN;
  if (view && [view isHiddenOrHasHiddenAncestor]) value.flags|=W7VS_VIEW_HIDDEN;
  if (window) {
    value.flags|=W7VS_HAS_WINDOW;
    if (window.visible) value.flags|=W7VS_WINDOW_VISIBLE;
    if (window.miniaturized) value.flags|=W7VS_MINIATURIZED;
    if (window.onActiveSpace) value.flags|=W7VS_ON_ACTIVE_SPACE;
    value.occlusion_state=window.occlusionState;
    if (value.occlusion_state & NSWindowOcclusionStateVisible) value.flags|=W7VS_OCCLUSION_VISIBLE;
    if ([w7ds_closed_windows containsObject:window]) value.flags|=W7VS_WINDOW_CLOSED;
    NSInteger number=window.windowNumber;
    if (number>0) value.window_id=(uint64_t)number;
    NSNumber *display=window.screen.deviceDescription[@"NSScreenNumber"];
    if (display) value.display_id=display.unsignedIntValue;
  }
  value.observed_ns=w7ds_visibility_now();
  pthread_mutex_lock(&w7ds_lock);
  if (!state->diagnostic_visibility.layer_identity && w7vs_next_identity)
    state->diagnostic_visibility.layer_identity=w7vs_next_identity++;
  value.layer_identity=state->diagnostic_visibility.layer_identity;
  value.sequence=state->diagnostic_visibility.sequence+1;
  state->diagnostic_visibility=value;
  state->diagnostic_visibility_queued=NO;
  pthread_mutex_unlock(&w7ds_lock);
  [state release];
}
static uint64_t w7vs_dispatch(struct W7VSRequest *request) {
  if (!request || request->size!=sizeof(*request) || request->version!=W7VS_VERSION) return 0;
  request->status=W7VS_BAD_REQUEST;
  memset(&request->snapshot,0,sizeof(request->snapshot));
  if (!request->generation || !request->layer || request->reserved) return W7VS_REPLY;
  CAMetalLayer *layer=(CAMetalLayer *)(uintptr_t)request->layer;
  W7DSLayerState *state=w7ds_state(layer);
  if (!state) { request->status=W7VS_UNKNOWN; return W7VS_REPLY; }
  uint64_t now=w7ds_visibility_now();
  pthread_mutex_lock(&w7ds_lock);
  struct W7VSSnapshot value=state->diagnostic_visibility;
  BOOL clock_valid=now && value.observed_ns && now>=value.observed_ns;
  value.age_ns=clock_valid ? now-value.observed_ns : UINT64_MAX;
  BOOL schedule=!state->diagnostic_visibility_queued &&
      (!value.sequence || !clock_valid || value.age_ns>=W7VS_REFRESH_NS);
  if (schedule) state->diagnostic_visibility_queued=YES;
  pthread_mutex_unlock(&w7ds_lock);
  request->snapshot=value;
  request->status=!value.sequence || !value.observed_ns || !value.layer_identity ? W7VS_UNKNOWN :
      (!clock_valid || value.age_ns>=W7VS_EXPIRE_NS ? W7VS_STALE : W7VS_OK);
  /* The copied block pins the actual layer through this one refresh; no cached
   * delegate/window is messaged from a worker or retained across observations. */
  if (schedule) dispatch_async(dispatch_get_main_queue(), ^{ w7vs_refresh_main(layer); });
  [state release];
  return W7VS_REPLY;
}
