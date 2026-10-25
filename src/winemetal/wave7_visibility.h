#ifndef WAVE7_VISIBILITY_H
#define WAVE7_VISIBILITY_H
#include <stdint.h>
/* Optional diagnostics-only transport over the existing command-buffer property
 * thunk. Unknown bridges return zero. Require exact marker and request status.
 * Native never retains the request or waits for main. No presentation policy. */
#define W7VS_PROPERTY UINT32_C(0x57375653)
#define W7VS_REPLY UINT64_C(0x5737565300000001)
#define W7VS_VERSION 1u
#define W7VS_REFRESH_NS UINT64_C(500000000)
#define W7VS_EXPIRE_NS UINT64_C(1000000000)
enum W7VSStatus { W7VS_OK=0, W7VS_BAD_REQUEST=1, W7VS_UNSUPPORTED=2, W7VS_UNKNOWN=3, W7VS_STALE=4 };
enum W7VSFlags {
 W7VS_HAS_WINDOW=1u, W7VS_WINDOW_VISIBLE=2u, W7VS_MINIATURIZED=4u,
 W7VS_ON_ACTIVE_SPACE=8u, W7VS_VIEW_HIDDEN=16u, W7VS_OCCLUSION_VISIBLE=32u,
 W7VS_LAYER_HIDDEN=64u, W7VS_WINDOW_CLOSED=128u
};
#pragma pack(push,8)
struct W7VSSnapshot {
 uint64_t layer_identity, sequence, observed_ns, age_ns;
 uint64_t window_id, display_id, occlusion_state, flags;
};
struct W7VSRequest {
 uint32_t size, version, generation, status;
 uint64_t layer, reserved;
 struct W7VSSnapshot snapshot;
};
#pragma pack(pop)
/* observed_ns/age_ns use native MONOTONIC_RAW nanoseconds, not PE QPC.
 * UNKNOWN has no observed sample. STALE preserves the old sample with age.
 * A fresh sample may still have no hosting window; HAS_WINDOW distinguishes it.
 * Identities belong to associated-state lifetimes, never reusable layer pointers.
 * generation is caller capture identity echoed synchronously, not a native clock. */
#if defined(__cplusplus)
static_assert(sizeof(W7VSSnapshot)==64 && sizeof(W7VSRequest)==96,"visibility ABI");
#else
_Static_assert(sizeof(struct W7VSSnapshot)==64 && sizeof(struct W7VSRequest)==96,"visibility ABI");
#endif
#endif
