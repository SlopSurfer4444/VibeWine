#ifndef WAVE7_LAYER_APPLY_H
#define WAVE7_LAYER_APPLY_H
#include <stdint.h>
/* Optional synchronous request through the existing MTLCommandBuffer_property
 * thunk. An older native bridge returns zero for this unknown property tag.
 * Caller requires exact reply marker and status==OK; no new imports/ordinals.
 * Same-process 64-bit ABI. Request and caller objects never escape the call. */
#define W7LA_PROPERTY UINT32_C(0x57374c41)
#define W7LA_REPLY UINT64_C(0x57374c4100000001)
#define W7LA_VERSION 1u
enum W7LAOp { W7LA_QUERY=1, W7LA_APPLY=2 };
enum W7LAStatus { W7LA_OK=0, W7LA_BAD_REQUEST=1, W7LA_UNSUPPORTED=2,
  W7LA_REJECTED=3, W7LA_FAILED=4 };
#pragma pack(push,8)
struct W7LAConfiguration {
  uint64_t device;
  double contents_scale, drawable_width, drawable_height;
  uint32_t opaque, display_sync_enabled, framebuffer_only, pixel_format;
  uint32_t colorspace, reserved;
};
struct W7LARequest {
  uint32_t size, version, op, status;
  uint64_t layer, config_revision;
  struct W7LAConfiguration configuration;
};
#pragma pack(pop)
/* QUERY only negotiates protocol support. APPLY completes synchronously.
 * BAD_REQUEST/UNSUPPORTED/preparation FAILED do not mutate properties.
 * REJECTED means the main executor did not enter the application block.
 * FAILED after the block entered may leave partial properties: the caller MUST
 * invalidate its prior applied revision before APPLY and never draw on failure.
 * There is no PENDING state, deferred frame, queued renderer callback or session. */
#if defined(__cplusplus)
static_assert(sizeof(W7LAConfiguration)==56,"checked layer configuration ABI");
static_assert(sizeof(W7LARequest)==88,"checked layer request ABI");
#else
_Static_assert(sizeof(struct W7LAConfiguration)==56,"checked layer configuration ABI");
_Static_assert(sizeof(struct W7LARequest)==88,"checked layer request ABI");
#endif
#endif
