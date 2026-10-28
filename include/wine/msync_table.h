/*
 * Stable page directories for the MSync client and server.
 *
 * Copyright 2026 VibeWine contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef __WINE_MSYNC_TABLE_H
#define __WINE_MSYNC_TABLE_H

#include <stdlib.h>

/* MSync reserves the high four object-index bits for its Mach protocol. */
#define MSYNC_MAX_SHM_IDX ((1u << 28) - 1)

/* The directory is fixed, not the page table: allocate only a requested segment.
 * Segment 0 covers [0,128), then [128,256), [256,512), etc. Even a sparse
 * lookup does not allocate preceding segments. All segments AND page mappings
 * live until process exit. No reader reclamation epoch or lookup lock is needed.
 * The owner serializes slot creation; readers acquire both publication levels. */
struct msync_table
{
    void **segments[32];
};

static inline unsigned int msync_table_segment( unsigned int entry )
{
    return entry < 128 ? 0 : 32 - __builtin_clz( entry ) - 7;
}

static inline void *msync_table_get( struct msync_table *table, unsigned int entry )
{
    unsigned int segment = msync_table_segment( entry );
    unsigned int base = segment ? 1u << (segment + 6) : 0;
    void **slots = __atomic_load_n( &table->segments[segment], __ATOMIC_ACQUIRE );

    return slots ? __atomic_load_n( &slots[entry - base], __ATOMIC_ACQUIRE ) : NULL;
}

/* Caller holds the owner's writer lock. Allocation failure leaves the directory
 * unchanged. A published segment is never resized, replaced, or freed. */
static inline void **msync_table_slot( struct msync_table *table, unsigned int entry )
{
    unsigned int segment = msync_table_segment( entry );
    unsigned int count = segment ? 1u << (segment + 6) : 128;
    unsigned int base = segment ? count : 0;
    void **slots = __atomic_load_n( &table->segments[segment], __ATOMIC_ACQUIRE );

    if (!slots)
    {
        if (!(slots = calloc( count, sizeof(*slots) ))) return NULL;
        __atomic_store_n( &table->segments[segment], slots, __ATOMIC_RELEASE );
    }
    return &slots[entry - base];
}
#endif
