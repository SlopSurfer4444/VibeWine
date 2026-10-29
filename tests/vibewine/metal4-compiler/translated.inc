/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Native execution of Wine test bytecode translated by the supplied airconv.
 * No game shader/cache is read. The no-op CS only checks pipeline/dispatch. */
static id<MTLLibrary> translated_library(id<MTLDevice> dev, const char *directory, const char *name) {
  NSString *path=[[NSString stringWithUTF8String:directory] stringByAppendingPathComponent:
      [[NSString stringWithUTF8String:name] stringByAppendingString:@".metallib"]];
  NSError *e=nil; id<MTLLibrary> library=[dev newLibraryWithURL:[NSURL fileURLWithPath:path] error:&e];
  no_error(e,"translated library load"); require(library!=nil,"translated library result"); return library;
}
static void translated_draw(id<MTLDevice> dev,id<MTLCommandQueue> queue,id<MTLRenderPipelineState> pso,
                            bool mrt,unsigned samples,unsigned char *output) {
  id<MTLTexture> target[2]={nil,nil}, multi[2]={nil,nil};
  MTLRenderPassDescriptor *pass=[MTLRenderPassDescriptor renderPassDescriptor];
  for(unsigned n=0;n<(mrt ? 2u:1u);n++) {
    MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
        width:SIDE height:SIDE mipmapped:NO]; d.storageMode=MTLStorageModeShared; d.usage=MTLTextureUsageRenderTarget;
    target[n]=[dev newTextureWithDescriptor:d]; require(target[n]!=nil,"translated target allocation");
    if(samples>1) { d.textureType=MTLTextureType2DMultisample; d.sampleCount=samples; d.storageMode=MTLStorageModePrivate;
      multi[n]=[dev newTextureWithDescriptor:d]; require(multi[n]!=nil,"translated MSAA allocation"); }
    pass.colorAttachments[n].texture=multi[n] ?: target[n];
    pass.colorAttachments[n].loadAction=MTLLoadActionClear;
    pass.colorAttachments[n].clearColor=MTLClearColorMake(16.0/255,32.0/255,48.0/255,64.0/255);
    pass.colorAttachments[n].storeAction=samples>1 ? MTLStoreActionMultisampleResolve:MTLStoreActionStore;
    if(samples>1) pass.colorAttachments[n].resolveTexture=target[n];
  }
  const float vertices[][8]={{-1,-1,0,1,64.0f/255,128.0f/255,192.0f/255,128.0f/255},
                            { 3,-1,0,1,64.0f/255,128.0f/255,192.0f/255,128.0f/255},
                            {-1, 3,0,1,64.0f/255,128.0f/255,192.0f/255,128.0f/255}};
  id<MTLBuffer> vb=[dev newBufferWithBytes:vertices length:sizeof(vertices) options:MTLResourceStorageModeShared];
  require(vb!=nil,"translated vertex allocation");
  id<MTLCommandBuffer> cb=[queue commandBuffer]; id<MTLRenderCommandEncoder> encoder=[cb renderCommandEncoderWithDescriptor:pass];
  require(encoder!=nil,"translated render encoder"); [encoder setRenderPipelineState:pso];
  if(!mrt) [encoder setVertexBuffer:vb offset:0 atIndex:0];
  [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3]; [encoder endEncoding]; completed(cb);
  for(unsigned n=0;n<(mrt ? 2u:1u);n++) {
    [target[n] getBytes:output+n*PIXEL_BYTES bytesPerRow:SIDE*4 fromRegion:MTLRegionMake2D(0,0,SIDE,SIDE) mipmapLevel:0];
    [multi[n] release]; [target[n] release];
  }
  [vb release];
}
static void translated_render(id<MTLDevice> dev,id<MTLCommandQueue> queue,const char *directory,
                              bool mrt,unsigned samples,bool blend,bool mask) {
  id<MTLRenderPipelineState> modern=nil,legacy=nil;
  @autoreleasepool {
    id<MTLLibrary> vl=translated_library(dev,directory,mrt ? "mrt-vs":"blend-vs");
    id<MTLLibrary> pl=translated_library(dev,directory,mrt ? "mrt-ps":"blend-ps");
    id<MTLFunction> v=ordinary(vl,"shader_main"), f=ordinary(pl,"shader_main");
    struct WMTRenderPipelineInfo info=render_info(v,f,samples,blend,mask);
    if(mrt) info.colors[1]=info.colors[0];
    MTLRenderPipelineDescriptor *d=render_desc(info);
    if(mrt) {
      d.colorAttachments[1].pixelFormat=MTLPixelFormatRGBA8Unorm;
      d.colorAttachments[1].writeMask=MTLColorWriteMaskAll;
      modern=native_render(dev,&info); /* Exact production factory, two outputs. */
    } else {
      /* CLI -A emits vertex attributes; production D3D11 normally emits vertex
       * pulling. This branch exercises the exact helper with an explicit layout,
       * and is labelled separately from the exact MRT factory above. */
      MTLVertexDescriptor *vd=[MTLVertexDescriptor vertexDescriptor];
      vd.attributes[0].format=MTLVertexFormatFloat4; vd.attributes[0].offset=0; vd.attributes[0].bufferIndex=0;
      vd.attributes[1].format=MTLVertexFormatFloat4; vd.attributes[1].offset=16; vd.attributes[1].bufferIndex=0;
      vd.layouts[0].stride=32; vd.layouts[0].stepFunction=MTLVertexStepFunctionPerVertex; vd.layouts[0].stepRate=1;
      d.vertexDescriptor=vd; NSError *error=nil;
      require(wmt_metal4_render(dev,d,&info,&modern,&error),"translated helper selected");
      no_error(error,"translated helper compile"); require(modern!=nil,"translated helper PSO");
    }
    NSError *e=nil; legacy=[dev newRenderPipelineStateWithDescriptor:d error:&e];
    no_error(e,"translated legacy compile"); require(legacy!=nil,"translated legacy PSO");
    [d release]; [v release]; [f release]; [vl release]; [pl release];
  } /* Function/library/descriptors and compiler are gone before first use. */
  unsigned char a[2*PIXEL_BYTES],b[2*PIXEL_BYTES],again[2*PIXEL_BYTES];
  translated_draw(dev,queue,modern,mrt,samples,a); translated_draw(dev,queue,legacy,mrt,samples,b);
  translated_draw(dev,queue,modern,mrt,samples,again);
  const unsigned char plain[]={64,128,192,128}, added[]={80,160,240,192}, masked[]={16,160,48,192};
  /* Wine intentionally tests register matching with reversed semantic names. */
  const unsigned char first[]={0,255,0,255},second[]={255,0,0,255};
  golden(a,mrt ? first:mask ? masked:blend ? added:plain,"translated independent golden");
  if(mrt) golden(a+PIXEL_BYTES,second,"translated second target golden");
  size_t bytes=(mrt ? 2u:1u)*PIXEL_BYTES;
  require(!memcmp(a,b,bytes),"translated Metal4 legacy byte parity");
  require(!memcmp(a,again,bytes),"translated retained PSO reuse byte parity");
  printf("PASS translated render corpus=%s samples=%u blend=%u mask=%u bytes=%zu golden+legacy+retained-reuse\n",
      mrt ? "MRT-exact-factory":"blend-exact-helper-attribute-layout",samples,blend,mask,bytes);
  [modern release]; [legacy release];
}
static void translated_compute(id<MTLDevice> dev,id<MTLCommandQueue> q,const char *directory) {
  id<MTLComputePipelineState> modern=nil,legacy=nil;
  @autoreleasepool {
    id<MTLLibrary> library=translated_library(dev,directory,"noop-cs"); id<MTLFunction> f=ordinary(library,"shader_main");
    struct WMTComputePipelineInfo info={0}; info.compute_function=(obj_handle_t)f;
    struct unixcall_mtldevice_newcomputepso p={0}; p.device=(obj_handle_t)dev; p.info.ptr=&info;
    require(_MTLDevice_newComputePipelineState(&p)==STATUS_SUCCESS,"translated compute factory status");
    no_error((NSError*)p.ret_error,"translated compute factory error"); require(p.ret_pso,"translated compute PSO"); modern=(id<MTLComputePipelineState>)p.ret_pso;
    NSError *e=nil; legacy=[dev newComputePipelineStateWithFunction:f error:&e];
    no_error(e,"translated legacy compute compile"); require(legacy!=nil,"translated legacy compute PSO");
    [f release]; [library release];
  }
  for(unsigned n=0;n<3;n++) { id<MTLCommandBuffer> cb=[q commandBuffer]; id<MTLComputeCommandEncoder> e=[cb computeCommandEncoder];
    require(e!=nil,"translated no-op compute encoder"); [e setComputePipelineState:n==1 ? legacy:modern];
    [e dispatchThreadgroups:MTLSizeMake(2,3,1) threadsPerThreadgroup:MTLSizeMake(1,1,1)]; [e endEncoding]; completed(cb); }
  [modern release]; [legacy release]; puts("PASS translated compute no-op exact-factory+legacy+retained-reuse (no resource/output claim)");
}
static int translated_all(const char *directory) {
  id<MTLDevice> dev=MTLCreateSystemDefaultDevice();
  if(!dev || !wmt_metal4_capable(dev)) { [dev release]; return 77; }
  id<MTLCommandQueue> q=[dev newCommandQueue]; require(q!=nil,"translated command queue");
  translated_render(dev,q,directory,false,1,false,false);
  translated_render(dev,q,directory,false,1,true,false);
  translated_render(dev,q,directory,false,1,true,true);
  translated_render(dev,q,directory,false,4,true,true);
  translated_render(dev,q,directory,true,1,false,false);
  translated_compute(dev,q,directory); [q release]; [dev release];
  puts("PASS translated five-library corpus; no game or performance claim"); return 0;
}
