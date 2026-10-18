/* Combined checked synchronous apply. Included after the native color helpers. */
#include "../wave7_layer_apply.h"
#include <math.h>

static uint64_t w7la_dispatch(struct W7LARequest *request)
{
    if (!request || request->size < 16) return 0;
    if (request->size != sizeof(*request) || request->version != W7LA_VERSION) {
        request->status = W7LA_BAD_REQUEST;
        return W7LA_REPLY;
    }
    request->status = W7LA_BAD_REQUEST;
    if (request->op == W7LA_QUERY) {
        request->status = W7LA_OK;
        return W7LA_REPLY;
    }
    if (request->op != W7LA_APPLY) return W7LA_REPLY;
    // Copy before dispatch; never leave a request/caller pointer in native work.
    const struct W7LAConfiguration config = request->configuration;
    if (!request->layer || !config.device || !request->config_revision || config.reserved ||
        !isfinite(config.contents_scale) || config.contents_scale <= 0 ||
        !isfinite(config.drawable_width) || config.drawable_width <= 0 ||
        !isfinite(config.drawable_height) || config.drawable_height <= 0 ||
        config.opaque > 1 || config.display_sync_enabled > 1 || config.framebuffer_only > 1)
        return W7LA_REPLY;
    switch (config.pixel_format) {
    case WMTPixelFormatBGRA8Unorm:
    case WMTPixelFormatBGRA8Unorm_sRGB:
    case WMTPixelFormatRGB10A2Unorm:
    case WMTPixelFormatBGR10A2Unorm:
    case WMTPixelFormatRGBA16Float:
        break;
    default:
        return W7LA_REPLY;
    }
    CFStringRef name = GetColorSpaceName((enum WMTColorSpace)config.colorspace);
    if (!name) return W7LA_REPLY;
    CGColorSpaceRef color = CGColorSpaceCreateWithName(name);
    if (!color) {
        request->status = W7LA_FAILED;
        return W7LA_REPLY;
    }
    CAMetalLayer *layer = (CAMetalLayer *)request->layer;
    __block bool entered = false, completed = false;
    bool accepted = false;
    @try {
        accepted = execute_on_main_checked(^{
            entered = true;
            @try {
                layer.device = (id<MTLDevice>)config.device;
                layer.opaque = config.opaque;
                layer.framebufferOnly = config.framebuffer_only;
                layer.contentsScale = config.contents_scale;
                layer.displaySyncEnabled = config.display_sync_enabled;
                layer.drawableSize = CGSizeMake(config.drawable_width, config.drawable_height);
                layer.pixelFormat = to_metal_pixel_format((enum WMTPixelFormat)config.pixel_format);
                layer.colorspace = color;
                layer.wantsExtendedDynamicRangeContent = WMT_COLORSPACE_IS_HDR(config.colorspace);
                w7ds_refresh_layer_main(layer);
                completed = true;
            } @catch (id exception) {
                (void)exception;
            }
        });
    } @catch (id exception) {
        (void)exception;
    }
    CGColorSpaceRelease(color);
    request->status = accepted && completed ? W7LA_OK : entered ? W7LA_FAILED : W7LA_REJECTED;
    return W7LA_REPLY;
}
