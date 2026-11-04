static bool wine_path, worker_path, reject_path;
static const char *fault;

static struct W7LARequest request_for(FixtureMetalLayer *layer) {
  return (struct W7LARequest){
    .size=sizeof(struct W7LARequest), .version=W7LA_VERSION, .op=W7LA_APPLY,
    .status=UINT32_MAX, .layer=(uintptr_t)layer, .config_revision=91,
    .configuration={.device=123, .contents_scale=2, .drawable_width=1920, .drawable_height=1080,
      .opaque=1, .display_sync_enabled=1, .framebuffer_only=0,
      .pixel_format=WMTPixelFormatBGRA8Unorm, .colorspace=WMTColorSpaceSRGB}
  };
}

static void assert_complete(FixtureMetalLayer *layer) {
  assert(layer.device==(id)(uintptr_t)123 && layer.opaque && !layer.framebufferOnly);
  assert(layer.contentsScale==2 && layer.displaySyncEnabled);
  assert(layer.drawableSize.width==1920 && layer.drawableSize.height==1080);
  assert(layer.pixelFormat==(MTLPixelFormat)WMTPixelFormatBGRA8Unorm);
  assert(layer.colorspace && !layer.wantsExtendedDynamicRangeContent);
}

static void exercise(void) {
  @autoreleasepool {
    FixtureMetalLayer *layer=[FixtureMetalLayer new];
    struct W7LARequest request=request_for(layer);
    bool throws=strcmp(fault,"none")!=0;
    throw_at=!strcmp(fault,"setter") ? 4 : 0;
    throw_refresh=!strcmp(fault,"refresh");
    assert(w7la_dispatch(&request)==W7LA_REPLY);
    assert(resolution_calls==1);
    assert(creates==1 && releases==1);
    if (reject_path) {
      assert(request.status==W7LA_REJECTED && writes==0 && refreshes==0);
    } else if (throws) {
      assert(request.status==W7LA_FAILED);
      assert(writes==(!strcmp(fault,"setter")?4:9));
      assert(refreshes==(!strcmp(fault,"refresh")?1:0));
      assert(request.config_revision==91);
      // All setters must run again; the first failure is not a committed apply.
      throw_at=0; throw_refresh=false;
      assert(w7la_dispatch(&request)==W7LA_REPLY);
      assert(request.status==W7LA_OK);
      assert(writes==(!strcmp(fault,"setter")?13:18));
      assert(refreshes==(!strcmp(fault,"refresh")?2:1));
      assert(creates==2 && releases==2 && resolution_calls==1);
      assert_complete(layer);
    } else {
      assert(request.status==W7LA_OK && writes==9 && refreshes==1);
      assert_complete(layer);
    }
    [layer release];
    assert(layer_deallocs==1);
    atomic_store(&done,1);
  }
}

static void *run_worker(void *unused) { (void)unused; exercise(); return NULL; }

int main(int argc, char **argv) { @autoreleasepool {
  assert(argc==5);
  wine_path=!strcmp(argv[1],"wine");
  worker_path=!strcmp(argv[2],"worker");
  reject_path=!strcmp(argv[3],"reject"); fault=argv[4];
  fixture_use_wine=wine_path;
  setup();
  if (reject_path) { assert(wine_path); macdrv_main_executor_stop_v1(); }
  if (worker_path) {
    pthread_t thread; assert(!pthread_create(&thread,NULL,run_worker,NULL));
    while (!atomic_load(&done))
      CFRunLoopRunInMode(wine_path?querymode:kCFRunLoopDefaultMode,0.005,false);
    assert(!pthread_join(thread,NULL));
  } else exercise();
  teardown();
  puts("PASS exact checked apply/executor selection");
  return 0;
} }
