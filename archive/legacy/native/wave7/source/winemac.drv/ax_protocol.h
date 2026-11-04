/* Wine MSAA bridge protocol v3. No native pointers, COM objects or process-global HWND-only identities. */
#ifndef WINE_MACDRV_AX_PROTOCOL_H
#define WINE_MACDRV_AX_PROTOCOL_H
#include <stdint.h>
#define MACDRV_AX_VERSION 3
#define MACDRV_AX_MAX_NODES 64
#define MACDRV_AX_TEXT 256
#define MACDRV_AX_MAX_WINDOWS 32
#define MACDRV_AX_SNAPSHOT_MS 250
#define MACDRV_AX_DEADLINE_MS 500
#define MACDRV_AX_CACHE_MS 1000
#define MACDRV_AX_NONE 0
#define MACDRV_AX_SNAPSHOT 1
#define MACDRV_AX_PRESS 2
#define MACDRV_AX_SET_VALUE 3
#define MACDRV_AX_VALIDATE 4
#define MACDRV_AX_ROLE_GROUP 1
#define MACDRV_AX_ROLE_BUTTON 2
#define MACDRV_AX_ROLE_EDIT 3
#define MACDRV_AX_ROLE_CHECKBOX 4
#define MACDRV_AX_ROLE_RADIO 5
#define MACDRV_AX_ENABLED 1
#define MACDRV_AX_FOCUSED 2
#define MACDRV_AX_READONLY 4
#define MACDRV_AX_PASSWORD 8
#define MACDRV_AX_PRESSABLE 16
#define MACDRV_AX_SETTABLE 32
#define MACDRV_AX_CHECKED 64
#define MACDRV_AX_MIXED 128
#define MACDRV_AX_OK 0
#define MACDRV_AX_UNAVAILABLE 1
#define MACDRV_AX_DENIED 2
#define MACDRV_AX_STALE 3
#define MACDRV_AX_TIMEOUT 4
#pragma pack(push, 8)
struct macdrv_ax_node {
    uint32_t id, parent, role, flags;
    uint64_t token; /* PE-issued nonreused provider/control identity, never a traversal index. */
    int32_t x, y, width, height; /* Windows screen coordinates; native applies existing Wine conversion. */
    uint16_t name[MACDRV_AX_TEXT], value[MACDRV_AX_TEXT];
};
/* Poll input version+size, output one request; zero operation means no work.
 * VALIDATE input repeats request identity and returns result, without consuming work. */
struct macdrv_ax_request {
    uint32_t version, size, operation, hwnd;
    uint64_t generation, revision, serial, token;
    uint32_t node_id, result;
    uint16_t value[MACDRV_AX_TEXT];
};
/* Publish is synchronous COPY into native-owned storage, never keeps caller pointer.
 * The native owner admits only a matching outstanding serial+generation before its deadline.
 * Actions return result only; snapshots carry count and incrementing revision. */
struct macdrv_ax_snapshot {
    uint32_t version, size, operation, hwnd;
    uint64_t generation, revision, serial;
    uint32_t result, count;
    struct macdrv_ax_node nodes[MACDRV_AX_MAX_NODES];
};
#pragma pack(pop)
/* Identical layout in PE32, PE64 and Unix; WOW64 needs no pointer conversion. */
_Static_assert(sizeof(struct macdrv_ax_node) == 1064, "AX node wire layout");
_Static_assert(sizeof(struct macdrv_ax_request) == 568, "AX request wire layout");
_Static_assert(sizeof(struct macdrv_ax_snapshot) == 68144, "AX snapshot wire layout");
_Static_assert(__builtin_offsetof(struct macdrv_ax_request, generation) == 16, "AX generation offset");
_Static_assert(__builtin_offsetof(struct macdrv_ax_snapshot, nodes) == 48, "AX nodes offset");
#endif
