/* Portable predicates used by the native transport and its offline badpath tests. */
#ifndef WINE_MACDRV_AX_VALIDATION_H
#define WINE_MACDRV_AX_VALIDATION_H
#include "ax_protocol.h"
#include <string.h>
#include <stdlib.h>

/* Native opt-in matches the PE startup flag. Never changes ordinary Wine AX. */
static inline int macdrv_ax_bridge_enabled(void)
{
    const char *enabled = getenv("WINE_MACDRV_AX_BRIDGE");
    return enabled && !strcmp(enabled, "1");
}

static inline int macdrv_ax_text_valid(const uint16_t *text)
{
    unsigned int i;
    for (i = 0; i < MACDRV_AX_TEXT; i++)
    {
        if (!text[i]) return 1;
        if (text[i] >= 0xd800 && text[i] <= 0xdbff)
        {
            if (++i >= MACDRV_AX_TEXT || text[i] < 0xdc00 || text[i] > 0xdfff) return 0;
        }
        else if (text[i] >= 0xdc00 && text[i] <= 0xdfff) return 0;
    }
    return 0;
}

static inline int macdrv_ax_request_same_identity(const struct macdrv_ax_request *request,
                                                 const struct macdrv_ax_request *lease)
{
    return lease->operation && request->hwnd == lease->hwnd &&
           request->generation == lease->generation && request->revision == lease->revision &&
           request->serial == lease->serial && request->token == lease->token && request->node_id == lease->node_id &&
           !memcmp(request->value, lease->value, sizeof(request->value));
}

static inline int macdrv_ax_request_matches(const struct macdrv_ax_request *request,
                                           const struct macdrv_ax_request *lease,
                                           uint64_t now, uint64_t deadline)
{
    return now < deadline && macdrv_ax_request_same_identity(request, lease);
}

static inline int macdrv_ax_snapshot_valid(const struct macdrv_ax_snapshot *snapshot)
{
    unsigned int i, j, depth;
    if (snapshot->count > MACDRV_AX_MAX_NODES) return 0;
    for (i = 0; i < snapshot->count; i++)
    {
        const struct macdrv_ax_node *node = &snapshot->nodes[i];
        uint32_t parent = node->parent;
        if (!node->id || !node->token || node->width < 0 || node->height < 0 ||
            (node->role != MACDRV_AX_ROLE_BUTTON && node->role != MACDRV_AX_ROLE_EDIT && node->role != MACDRV_AX_ROLE_GROUP &&
             node->role != MACDRV_AX_ROLE_CHECKBOX && node->role != MACDRV_AX_ROLE_RADIO) ||
            (node->flags & ~(MACDRV_AX_ENABLED | MACDRV_AX_FOCUSED | MACDRV_AX_READONLY | MACDRV_AX_PASSWORD |
                             MACDRV_AX_PRESSABLE | MACDRV_AX_SETTABLE | MACDRV_AX_CHECKED | MACDRV_AX_MIXED)) ||
            ((node->flags & (MACDRV_AX_CHECKED | MACDRV_AX_MIXED)) &&
             node->role != MACDRV_AX_ROLE_CHECKBOX && node->role != MACDRV_AX_ROLE_RADIO) ||
            ((node->flags & MACDRV_AX_CHECKED) && (node->flags & MACDRV_AX_MIXED)) ||
            !macdrv_ax_text_valid(node->name) || !macdrv_ax_text_valid(node->value)) return 0;
        for (j = 0; j < i; j++) if (snapshot->nodes[j].id == node->id || snapshot->nodes[j].token == node->token) return 0;
        for (depth = 0; parent; depth++)
        {
            if (parent == node->id || depth >= snapshot->count) return 0;
            for (j = 0; j < snapshot->count && snapshot->nodes[j].id != parent; j++);
            if (j == snapshot->count) return 0;
            parent = snapshot->nodes[j].parent;
        }
    }
    return 1;
}
#endif
