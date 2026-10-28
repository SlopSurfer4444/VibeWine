extern "C" void NSObject_retain(obj_handle_t handle) {[(id)handle retain];}
extern "C" void NSObject_release(obj_handle_t handle) {[(id)handle release];}
extern "C" bool MTLDevice_supportsFamily(obj_handle_t device,WMTGPUFamily family) {return [(id<MTLDevice>)device supportsFamily:(MTLGPUFamily)family];}
extern "C" obj_handle_t MTLBuffer_newTexture(obj_handle_t buffer,WMTTextureInfo *info,uint64_t offset,uint64_t row) {
  unixcall_mtlbuffer_newtexture params{};params.buffer=buffer;params.info.set(info);params.offset=offset;params.bytes_per_row=row;
  _MTLBuffer_newTexture(&params);return params.ret;
}
extern "C" obj_handle_t MTLTexture_newTextureView(obj_handle_t texture,WMTPixelFormat format,WMTTextureType type,
    uint16_t first,uint16_t levels,uint16_t slice,uint16_t slices,WMTTextureSwizzleChannels swizzle,uint64_t *gpu_id) {
  unixcall_mtltexture_newtextureview p{};p.texture=texture;p.format=format;p.texture_type=type;p.level_start=first;p.level_count=levels;p.slice_start=slice;p.slice_count=slices;p.swizzle=swizzle;
  _MTLTexture_newTextureView(&p);*gpu_id=p.gpu_resource_id;return p.ret;
}
