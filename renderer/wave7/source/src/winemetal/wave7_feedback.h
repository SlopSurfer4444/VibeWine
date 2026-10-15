#ifndef WAVE7_FEEDBACK_H
#define WAVE7_FEEDBACK_H
#include <stdint.h>
/* Optional transport through existing MTLCommandBuffer_property(request address,
 * (WMTCommandBufferProperty)W7FB_PROPERTY). Old donor's default returns zero.
 * Caller MUST require W7FB_REPLY and status==OK; no new imports or ordinals.
 * Same-process x64 ABI only. Request is synchronous; native never retains it.
 */
#define W7FB_PROPERTY UINT32_C(0x57374642)
#define W7FB_REPLY UINT64_C(0x5737464200000001)
#define W7FB_VERSION 1u
#define W7FB_RING_CAPACITY 64u
#define W7FB_SESSION_CAPACITY 16u
#define W7FB_CAP_PRESENTED_HANDLER UINT64_C(1) /* extension + OS available;
 * REGISTER additionally checks the actual drawable selectors */
#define W7FB_CLOCK_CA_HOST_SECONDS 1u /* MTLDrawable.presentedTime, seconds;
 * CoreAnimation host-time domain. Never compare directly to PE QPC timestamps. */
enum W7FBOp { W7FB_QUERY=1, W7FB_OPEN, W7FB_REGISTER, W7FB_POLL, W7FB_CLOSE, W7FB_STATUS, W7FB_CAPTURE_STATUS };
/* CAPTURE_STATUS requires no session: OK means native cleanup complete, PENDING
 * means a lease/release obligation remains, UNSUPPORTED means unavailable. */
enum W7FBStatus { W7FB_OK=0, W7FB_BAD_REQUEST=1, W7FB_UNSUPPORTED=2,
  W7FB_NO_SESSION=3, W7FB_LIMIT=4, W7FB_EMPTY=5, W7FB_STALE=6, W7FB_PENDING=7 };
#pragma pack(push, 8)
struct W7FBRecord {
  uint64_t generation, swapchain_serial, frame_id, config_revision;
  uint32_t drawable_width, drawable_height, clock_domain, reserved;
  double presented_seconds; /* zero/invalid time remains observable, not presented proof */
};
struct W7FBStats {
  uint64_t accepted, completed, abandoned, overflow, rejected, pending, queued;
};
struct W7FBRequest {
  uint32_t size, version, op, status;
  uint64_t capabilities, session, drawable;
  struct W7FBRecord record;
  struct W7FBStats stats;
};
#pragma pack(pop)
#if defined(__cplusplus)
static_assert(sizeof(W7FBRecord)==56, "feedback record ABI");
static_assert(sizeof(W7FBRequest)==152, "feedback request ABI");
#else
_Static_assert(sizeof(struct W7FBRecord)==56, "feedback record ABI");
_Static_assert(sizeof(struct W7FBRequest)==152, "feedback request ABI");
#endif
#endif
