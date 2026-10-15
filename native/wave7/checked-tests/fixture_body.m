
static const CFStringRef querymode=CFSTR("WineAppWaitQueryResponseMode");
static atomic_int done;
static struct W7LARequest worker_result;
static struct W7LARequest *waiting_request;

static void setup(void) {
  unsetenv("DXMT_DS3_ASPECT_FIT");
  controller=[WineApplicationController new];
  controller->requests=[NSMutableArray new];
  controller->requestsManipQueue=dispatch_queue_create("checked.apply.test",DISPATCH_QUEUE_SERIAL);
  CFRunLoopSourceContext context={0}; context.perform=PerformRequest;
  controller->requestSource=CFRunLoopSourceCreate(NULL,0,&context);
  CFRunLoopAddSource(CFRunLoopGetMain(),controller->requestSource,kCFRunLoopCommonModes);
  CFRunLoopAddSource(CFRunLoopGetMain(),controller->requestSource,querymode);
  macdrv_main_executor_start_v1();
}

static void teardown(void) {
  macdrv_main_executor_stop_v1();
  PerformRequest(NULL);
  CFRunLoopSourceInvalidate(controller->requestSource);
  CFRelease(controller->requestSource);
  [controller->requests release];
  dispatch_release(controller->requestsManipQueue);
  [controller release];
}

static struct W7LARequest request_for(FixtureMetalLayer *layer) {
  return (struct W7LARequest){
    .size=sizeof(struct W7LARequest), .version=W7LA_VERSION, .op=W7LA_APPLY,
    .status=UINT32_MAX, .layer=(uintptr_t)layer, .config_revision=91,
    .configuration={.device=123, .contents_scale=2, .drawable_width=1920, .drawable_height=1080,
      .opaque=1, .display_sync_enabled=1, .framebuffer_only=0,
      .pixel_format=WMTPixelFormatBGRA8Unorm, .colorspace=WMTColorSpaceSRGB}
  };
}

static void dispatch_request(struct W7LARequest *request) {
  struct unixcall_generic_obj_uint64_uint64_ret parameters={
    .handle=(uintptr_t)request, .arg=W7LA_PROPERTY, .ret=UINT64_MAX};
  assert(_MTLCommandBuffer_property(&parameters)==STATUS_SUCCESS);
  assert(parameters.ret==W7LA_REPLY);
}

static void expect_bad(struct W7LARequest request) {
  int oldwrites=writes, oldcalls=executor_calls, oldcreates=creates;
  dispatch_request(&request);
  assert(request.status==W7LA_BAD_REQUEST);
  assert(writes==oldwrites && executor_calls==oldcalls && creates==oldcreates);
}

static void assert_complete(FixtureMetalLayer *layer) {
  assert(layer.device==(id)(uintptr_t)123 && layer.opaque && !layer.framebufferOnly);
  assert(layer.contentsScale==2 && layer.displaySyncEnabled);
  assert(layer.drawableSize.width==1920 && layer.drawableSize.height==1080);
  assert(layer.pixelFormat==(MTLPixelFormat)WMTPixelFormatBGRA8Unorm);
  assert(layer.colorspace && !layer.wantsExtendedDynamicRangeContent);
}

static void wait_queued(void) {
  for (int i=0; i<10000; i++) {
    __block NSUInteger count;
    dispatch_sync(controller->requestsManipQueue,^{count=controller->requests.count;});
    if (count) return;
    usleep(100);
  }
  assert(!"worker never queued");
}

static void *worker(void *unused) { (void)unused; @autoreleasepool {
  FixtureMetalLayer *layer=[FixtureMetalLayer new];
  struct W7LARequest request=request_for(layer);
  waiting_request=&request;
  dispatch_request(&request);
  if (request.status==W7LA_OK) assert_complete(layer);
  worker_result=request;
  /* A cancelled queue wrapper must never dereference this request or layer. */
  memset(&request,0xa5,sizeof(request));
  [layer release];
  atomic_store(&done,1);
  return NULL;
} }

int main(int argc, char **argv) { @autoreleasepool {
  assert(argc==2); setup();
  FixtureMetalLayer *layer=[FixtureMetalLayer new];
  struct W7LARequest request=request_for(layer);
  const char *mode=argv[1];
  if (!strcmp(mode,"validation")) {
    assert(w7la_dispatch(NULL)==0);
    uint32_t tiny=sizeof(tiny);
    assert(w7la_dispatch((struct W7LARequest *)&tiny)==0 && tiny==sizeof(tiny));
    struct W7LARequest changed=request;
    changed.size--; expect_bad(changed);
    changed=request; changed.size++; expect_bad(changed);
    changed=request; changed.version=0; expect_bad(changed);
    changed=request; changed.version++; expect_bad(changed);
    changed=request; changed.op=99; expect_bad(changed);
    changed=request; changed.layer=0; expect_bad(changed);
    changed=request; changed.config_revision=0; expect_bad(changed);
    changed=request; changed.configuration.device=0; expect_bad(changed);
    changed=request; changed.configuration.reserved=1; expect_bad(changed);
    changed=request; changed.configuration.opaque=2; expect_bad(changed);
    changed=request; changed.configuration.display_sync_enabled=2; expect_bad(changed);
    changed=request; changed.configuration.framebuffer_only=2; expect_bad(changed);
    changed=request; changed.configuration.pixel_format=UINT32_MAX; expect_bad(changed);
    changed=request; changed.configuration.colorspace=UINT32_MAX; expect_bad(changed);
    const double bad_numbers[]={0, -1, INFINITY, -INFINITY, NAN};
    for (unsigned i=0; i<sizeof(bad_numbers)/sizeof(*bad_numbers); i++) {
      changed=request; changed.configuration.contents_scale=bad_numbers[i]; expect_bad(changed);
      changed=request; changed.configuration.drawable_width=bad_numbers[i]; expect_bad(changed);
      changed=request; changed.configuration.drawable_height=bad_numbers[i]; expect_bad(changed);
    }
    changed=request; changed.op=W7LA_QUERY; changed.layer=0; dispatch_request(&changed);
    assert(changed.status==W7LA_OK && !writes && !executor_calls && !creates);
  } else if (!strcmp(mode,"old-bridge")) {
    const struct W7LARequest before=request;
    struct unixcall_generic_obj_uint64_uint64_ret parameters={
      .handle=(uintptr_t)&request, .arg=W7LA_PROPERTY, .ret=UINT64_MAX};
    assert(legacy_MTLCommandBuffer_property(&parameters)==STATUS_SUCCESS);
    assert(parameters.ret==0 && !memcmp(&before,&request,sizeof(request)));
    assert(!writes && !executor_calls && !creates);
  } else if (!strcmp(mode,"prepare-failure")) {
    fail_create=true; dispatch_request(&request);
    assert(request.status==W7LA_FAILED && creates==1 && !releases && !writes && !executor_calls);
    fail_create=false; dispatch_request(&request);
    assert(request.status==W7LA_OK); assert_complete(layer);
  } else if (!strcmp(mode,"executor-reject")) {
    reject_executor=true; dispatch_request(&request);
    assert(request.status==W7LA_REJECTED && creates==1 && releases==1 && !writes && !refreshes);
    reject_executor=false; dispatch_request(&request);
    assert(request.status==W7LA_OK && writes==9 && refreshes==1); assert_complete(layer);
  } else if (!strcmp(mode,"success")) {
    dispatch_request(&request);
    assert(request.status==W7LA_OK && executor_calls==1 && writes==9 && refreshes==1);
    assert(creates==1 && releases==1); assert_complete(layer);
  } else if (!strcmp(mode,"colorspace-format-matrix")) {
    const enum WMTColorSpace colorspaces[]={WMTColorSpaceSRGB,WMTColorSpaceSRGBLinear,
      WMTColorSpaceBT2020,WMTColorSpaceHDR_scRGB,WMTColorSpaceHDR_PQ};
    const CFStringRef names[]={kCGColorSpaceSRGB,kCGColorSpaceExtendedLinearSRGB,
      kCGColorSpaceITUR_2020_sRGBGamma,kCGColorSpaceExtendedLinearSRGB,kCGColorSpaceITUR_2100_PQ};
    const enum WMTPixelFormat formats[]={WMTPixelFormatBGRA8Unorm,WMTPixelFormatBGRA8Unorm_sRGB,
      WMTPixelFormatRGB10A2Unorm,WMTPixelFormatBGR10A2Unorm,WMTPixelFormatRGBA16Float};
    unsigned cells=0;
    for (unsigned c=0; c<sizeof(colorspaces)/sizeof(*colorspaces); c++) {
      assert(CFEqual(GetColorSpaceName(colorspaces[c]),names[c]));
      for (unsigned f=0; f<sizeof(formats)/sizeof(*formats); f++) {
        request=request_for(layer);
        request.configuration.colorspace=colorspaces[c];
        request.configuration.pixel_format=formats[f];
        int oldwrites=writes, oldcalls=executor_calls, oldrefreshes=refreshes;
        dispatch_request(&request);
        assert(request.status==W7LA_OK);
        assert(writes==oldwrites+9 && executor_calls==oldcalls+1 && refreshes==oldrefreshes+1);
        assert(layer.pixelFormat==(MTLPixelFormat)formats[f]);
        assert(CFEqual(CGColorSpaceGetName(layer.colorspace),names[c]));
        assert(layer.wantsExtendedDynamicRangeContent==((colorspaces[c]&4)!=0));
        cells++;
      }
    }
    assert(cells==25 && creates==25 && releases==25);
    puts("PASS 25 colorspace/format combinations, one synchronous apply each");
  } else if (!strcmp(mode,"exception-retry")) {
    throw_at=4; dispatch_request(&request);
    assert(request.status==W7LA_FAILED && writes==4 && !refreshes && creates==releases);
    assert(request.config_revision==91); /* Same revision must be accepted again. */
    throw_at=0; dispatch_request(&request);
    assert(request.status==W7LA_OK && writes==13 && refreshes==1 && executor_calls==2);
    assert_complete(layer);
  } else if (!strcmp(mode,"reentry")) {
    write_hook=^{struct W7LARequest nested=request_for(layer); dispatch_request(&nested); assert(nested.status==W7LA_OK);};
    dispatch_request(&request);
    assert(request.status==W7LA_OK && writes==18 && refreshes==2 && executor_calls==2);
  } else if (!strcmp(mode,"worker") || !strcmp(mode,"worker-copy") || !strcmp(mode,"queued-shutdown")) {
    pthread_t thread; assert(!pthread_create(&thread,NULL,worker,NULL));
    wait_queued(); assert(!atomic_load(&done) && !writes);
    if (!strcmp(mode,"queued-shutdown")) {
      macdrv_main_executor_stop_v1();
      pthread_join(thread,NULL);
      assert(atomic_load(&done) && worker_result.status==W7LA_REJECTED);
      assert(!writes && !refreshes && creates==releases && layer_deallocs==1);
      struct W7LARequest result_before=worker_result;
      PerformRequest(NULL); /* Cancelled wrapper executes after caller/layer ended. */
      assert(!writes && !refreshes && !memcmp(&result_before,&worker_result,sizeof(result_before)));
      dispatch_request(&request); assert(request.status==W7LA_REJECTED && !writes);
    } else {
      if (!strcmp(mode,"worker-copy")) {
        /* Queue admission synchronizes publication. The blocked worker cannot
         * read configuration until after this main request has completed. */
        waiting_request->configuration.device=456;
        waiting_request->configuration.drawable_width=99;
        waiting_request->configuration.colorspace=WMTColorSpaceHDR_PQ;
      }
      for (int i=0; i<10000 && !atomic_load(&done); i++) CFRunLoopRunInMode(querymode,.001,true);
      assert(atomic_load(&done)); pthread_join(thread,NULL);
      assert(worker_result.status==W7LA_OK && writes==9 && refreshes==1 && layer_deallocs==1);
    }
  } else if (!strcmp(mode,"active-shutdown")) {
    write_hook=^{macdrv_main_executor_stop_v1();};
    dispatch_request(&request);
    assert(request.status==W7LA_OK && writes==9 && refreshes==1);
    dispatch_request(&request);
    assert(request.status==W7LA_REJECTED && writes==9 && refreshes==1);
  } else { assert(!"unknown mode"); }
  assert(creates==releases+creation_failures); [layer release]; teardown();
  printf("PASS %s\n",mode); return 0;
} }
