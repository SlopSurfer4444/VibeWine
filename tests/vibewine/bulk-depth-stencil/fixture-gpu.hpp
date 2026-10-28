// Native Metal fixture adapter. The production initializer and Metal.hpp still
// execute; only the Wine C endpoints are supplied by the bounded test process.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <chrono>
#include <thread>
inline id<MTLDevice> gpu_device=MTLCreateSystemDefaultDevice();
inline NSMutableDictionary<NSNumber *,id<MTLBuffer>> *gpu_buffers=[NSMutableDictionary new];
inline bool gpu_negative=false;
inline unsigned gpu_pairs=0, gpu_updates=0, gpu_notifications=0;
inline uint64_t gpu_bytes=0;
inline void gpuNewBuffer(obj_handle_t handle,WMTBufferInfo *info){
 id<MTLBuffer> buffer=[gpu_device newBufferWithLength:info->length options:MTLResourceOptions(info->options)];
 if(!buffer){std::cerr<<"native buffer unavailable\n";std::exit(2);}gpu_buffers[@(handle)]=buffer;
 memset(buffer.contents,0xCD,info->length);
 if(buffer.storageMode==MTLStorageModeManaged)[buffer didModifyRange:NSMakeRange(0,info->length)];
}
inline void gpuUpdate(obj_handle_t handle,uint64_t offset,const void *data,uint64_t length){
 id<MTLBuffer> buffer=gpu_buffers[@(handle)];assert(buffer);memcpy((char*)buffer.contents+offset,data,length);
 if(buffer.storageMode==MTLStorageModeManaged){[buffer didModifyRange:NSMakeRange(offset,length)];++gpu_notifications;}
 ++gpu_updates;gpu_bytes+=length;
}
inline void gpuCommit(){
 if(obs.planes.empty())return;
 assert(obs.planes.size()==2);const auto &depth=obs.planes[0].command;const auto &stencil=obs.planes[1].command;
 const size_t w=depth.size.width,h=depth.size.height,level=depth.level,slice=depth.slice;
 MTLTextureDescriptor *descriptor=[MTLTextureDescriptor new];descriptor.textureType=MTLTextureType2DArray;
 descriptor.pixelFormat=MTLPixelFormatDepth32Float_Stencil8;descriptor.width=size_t(w)<<level;descriptor.height=size_t(h)<<level;
 descriptor.mipmapLevelCount=level+1;descriptor.arrayLength=slice+1;descriptor.storageMode=MTLStorageModePrivate;descriptor.usage=MTLTextureUsageRenderTarget;
 id<MTLTexture> texture=[gpu_device newTextureWithDescriptor:descriptor];
 if(!texture){std::cerr<<"native depth/stencil texture unavailable\n";std::exit(2);}
 if(gpu_negative){id<MTLBuffer> b=gpu_buffers[@(stencil.src)];((uint8_t*)b.contents)[stencil.src_offset]^=0x80;
  if(b.storageMode==MTLStorageModeManaged)[b didModifyRange:NSMakeRange(stencil.src_offset,1)];}
 id<MTLCommandQueue> queue=[gpu_device newCommandQueue];id<MTLCommandBuffer> command=[queue commandBuffer];id<MTLBlitCommandEncoder> blit=[command blitCommandEncoder];
 id<MTLBuffer> outputs[2];
 for(unsigned plane=0;plane<2;++plane){const auto &c=obs.planes[plane].command;
  const MTLBlitOption option=plane?MTLBlitOptionStencilFromDepthStencil:MTLBlitOptionDepthFromDepthStencil;
  const size_t image=c.bytes_per_row*h;outputs[plane]=[gpu_device newBufferWithLength:image options:MTLResourceStorageModeShared];
  [blit copyFromBuffer:gpu_buffers[@(c.src)] sourceOffset:c.src_offset sourceBytesPerRow:c.bytes_per_row sourceBytesPerImage:c.bytes_per_image sourceSize:MTLSizeMake(w,h,1) toTexture:texture destinationSlice:slice destinationLevel:level destinationOrigin:MTLOriginMake(0,0,0) options:option];
  [blit copyFromTexture:texture sourceSlice:slice sourceLevel:level sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(w,h,1) toBuffer:outputs[plane] destinationOffset:0 destinationBytesPerRow:c.bytes_per_row destinationBytesPerImage:image options:option];
 }
 [blit endEncoding];[command commit];const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
 while(command.status!=MTLCommandBufferStatusCompleted&&command.status!=MTLCommandBufferStatusError&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
 if(command.status!=MTLCommandBufferStatusCompleted){std::cerr<<"native completion failure "<<(command.error?command.error.localizedDescription.UTF8String:"deadline")<<"\n";std::exit(2);}
 for(unsigned plane=0;plane<2;++plane){const auto &c=obs.planes[plane].command;const size_t logical=plane?w:size_t(w)*4;
  for(size_t row=0;row<h;++row)if(memcmp((char*)outputs[plane].contents+row*c.bytes_per_row,obs.bytes.at(c.src).data()+c.src_offset+row*c.bytes_per_row,logical)){
   std::cerr<<(gpu_negative?"EXPECTED_NEGATIVE native stencil source corruption":"native depth/stencil readback mismatch")<<"\n";std::exit(gpu_negative?20:2);}
 }
 ++gpu_pairs;
}
