static NTSTATUS
_MetalLayer_getProps(void *obj) {
  /* Serialize AppKit/layer reads and display metadata with native setters.
   * execute_on_main executes inline on the main thread (no self-dispatch). */
  execute_on_main(^{
    struct unixcall_generic_obj_ptr_noret *params = obj;
    CAMetalLayer *layer = (CAMetalLayer *)params->handle;
    struct WMTLayerProps *props = params->arg.ptr;
    props->device = (obj_handle_t)layer.device;
    props->opaque = layer.opaque;
    props->framebuffer_only = layer.framebufferOnly;
    props->contents_scale = layer.contentsScale;
    props->display_sync_enabled = layer.displaySyncEnabled;
    props->drawable_height = layer.drawableSize.height;
    props->drawable_width = layer.drawableSize.width;
    props->pixel_format = layer.pixelFormat;
  });
  return STATUS_SUCCESS;
}
