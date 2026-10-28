using namespace dxmt;
static uint64_t image_hash=1469598103934665603ull;
static void consume(const uint32_t *pixels,unsigned count) {
  for(unsigned i=0;i<count;++i){image_hash^=pixels[i];image_hash*=1099511628211ull;}
}
static void dispatch(id<MTLDevice> device,id<MTLLibrary> library,id<MTLTexture> texture,const char *kernel,unsigned width,unsigned height,const std::function<uint32_t(unsigned)> &oracle,const char *message) {
  NSError *error=nil;id<MTLFunction> f=[library newFunctionWithName:[NSString stringWithUTF8String:kernel]];
  auto pipeline=[device newComputePipelineStateWithFunction:f error:&error];require(pipeline!=nil,"native compute pipeline");[f release];
  auto output=[device newBufferWithLength:width*height*4 options:MTLResourceStorageModeShared];require(output!=nil,"native output buffer");
  memset(output.contents,0,width*height*4);auto queue=[device newCommandQueue];auto command=[queue commandBuffer];auto encoder=[command computeCommandEncoder];
  [encoder setComputePipelineState:pipeline];[encoder setTexture:texture atIndex:0];[encoder setBuffer:output offset:0 atIndex:0];
  [encoder dispatchThreads:MTLSizeMake(width,height,1) threadsPerThreadgroup:MTLSizeMake(1,1,1)];[encoder endEncoding];[command commit];[command waitUntilCompleted];
  require(command.status==MTLCommandBufferStatusCompleted && command.error==nil,"native command completed successfully");
  auto pixels=(const uint32_t *)output.contents;for(unsigned i=0;i<width*height;++i)require(pixels[i]==oracle(i),message);consume(pixels,width*height);
  [queue release];[output release];[pipeline release];
}
int main() { @autoreleasepool {try {
  auto device=MTLCreateSystemDefaultDevice();require(device!=nil,"native Metal device");
  NSError *error=nil;auto library=[device newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\nkernel void buffer_image(texture_buffer<uint,access::read> src [[texture(0)]],device uint *out [[buffer(0)]],uint p [[thread_position_in_grid]]){out[p]=src.read(p).r;}\nkernel void texture_image(texture2d<uint,access::read> src [[texture(0)]],device uint *out [[buffer(0)]],uint2 p [[thread_position_in_grid]]){out[p.y*src.get_width()+p.x]=src.read(p).r;}" options:nil error:&error];require(library!=nil,"native shader library");
  {
    Buffer b(1024,WMT::Device{(obj_handle_t)device});std::vector<BufferViewKey> keys;
    // R32Uint is deliberately first; baseline creates the registered tail too.
    for(auto fmt:{WMTPixelFormatR32Uint,WMTPixelFormatR32Float,WMTPixelFormatR8Uint,WMTPixelFormatR16Uint})keys.push_back(b.createView({fmt}));
    auto native=[device newBufferWithLength:1024 options:MTLResourceStorageModeShared];auto p=(uint32_t *)native.contents;for(unsigned i=0;i<256;++i)p[i]=0x1000u+i*37u;
    b.current_=new BufferAllocation((obj_handle_t)native);auto &view=b.view_(keys[0]);require(bool(view.texture),"native buffer view");
    dispatch(device,library,(id<MTLTexture>)view.texture.handle,"buffer_image",256,1,[](unsigned i){return 0x1000u+i*37u;},"buffer GPU pixel oracle");
    auto prior=b.current_;auto newer=[device newBufferWithLength:1024 options:MTLResourceStorageModeShared];auto n=(uint32_t *)newer.contents;for(unsigned i=0;i<256;++i)n[i]=0x2000u+i*17u;
    auto next=Rc<BufferAllocation>(new BufferAllocation((obj_handle_t)newer));b.rename(std::move(next));auto &renamed=b.view_(keys[0]);
    dispatch(device,library,(id<MTLTexture>)renamed.texture.handle,"buffer_image",256,1,[](unsigned i){return 0x2000u+i*17u;},"buffer renamed GPU pixel oracle");
    auto &old=b.view_(keys[0],prior.ptr());dispatch(device,library,(id<MTLTexture>)old.texture.handle,"buffer_image",256,1,[](unsigned i){return 0x1000u+i*37u;},"buffer old replay GPU pixel oracle");
  }
  {
    Texture t;MTLTextureDescriptor *desc=[[MTLTextureDescriptor alloc]init];desc.textureType=MTLTextureType2DArray;desc.pixelFormat=MTLPixelFormatR32Uint;desc.width=8;desc.height=8;desc.arrayLength=6;desc.mipmapLevelCount=4;desc.usage=MTLTextureUsageShaderRead|MTLTextureUsagePixelFormatView;desc.storageMode=MTLStorageModeShared;
    auto native=[device newTextureWithDescriptor:desc];[desc release];require(native!=nil,"native array texture");
    for(unsigned slice=0;slice<6;++slice)for(unsigned mip=0;mip<4;++mip){unsigned width=8>>mip;std::array<uint32_t,64> pixels{};for(unsigned i=0;i<width*width;++i)pixels[i]=10000*slice+1000*mip+i;[native replaceRegion:MTLRegionMake2D(0,0,width,width) mipmapLevel:mip slice:slice withBytes:pixels.data() bytesPerRow:width*4 bytesPerImage:width*width*4];}
    t.current_=new TextureAllocation(&t,(obj_handle_t)native);auto key=t.createView({.format=WMTPixelFormatR32Uint,.type=WMTTextureType2DArray,.firstMiplevel=1,.miplevelCount=2,.firstArraySlice=2,.arraySize=3});auto scalar=t.checkViewUseArray(key,false);
    for(unsigned i=0;i<1000;++i)require(uint64_t(t.checkViewUseArray(key,false))==uint64_t(scalar),"native warm alias exact key");
    auto &view=t.view(scalar);require(bool(view.texture),"native scalar alias");
    dispatch(device,library,(id<MTLTexture>)view.texture.handle,"texture_image",4,4,[](unsigned i){return 21000+i;},"texture GPU pixel oracle");
  }
  [library release];[device release];std::cout<<"PASS real Metal: 784 uint pixels exact, buffer rename/replay and array mip/slice alias; image digest="<<image_hash<<"\n";return 0;
} catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;} } }
