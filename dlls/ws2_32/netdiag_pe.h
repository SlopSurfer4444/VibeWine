/* Opt-in, metadata-only network observations. LGPL-2.1-or-later. */
#ifndef __WINE_WS2_NETDIAG_PE_H
#define __WINE_WS2_NETDIAG_PE_H

#include "wine/netdiag.h"

/* Read once per translation unit. The switch must be set before process start.
 * Environment reads and every bridge invocation preserve the caller's error. */
static inline BOOL netdiag_enabled(void)
{
    static LONG state;
    LONG value = InterlockedCompareExchange( &state, 0, 0 );
    DWORD error;

    if (!value)
    {
        error = GetLastError();
        value = GetEnvironmentVariableA( "VIBEWINE_NETDIAG_FILE", NULL, 0 ) ? 2 : 1;
        InterlockedCompareExchange( &state, value, 0 );
        SetLastError( error );
    }
    return value == 2;
}

static inline void netdiag_call( struct wine_netdiag_event *event )
{
    DWORD error = GetLastError();
    WINE_UNIX_CALL( ws_unix_netdiag, event );
    SetLastError( error );
}

static inline void netdiag_endpoint( struct wine_netdiag_event *event,
                                     const struct sockaddr *addr, SIZE_T len )
{
    event->family = event->port = event->scope = 0;
    memset( event->address, 0, sizeof(event->address) );
    event->flags |= ND_ENDPOINT_UNKNOWN;
    if (!addr || len < sizeof(addr->sa_family)) return;

    if (addr->sa_family == AF_INET && len >= sizeof(struct sockaddr_in))
    {
        const struct sockaddr_in *in = (const struct sockaddr_in *)addr;
        event->family = AF_INET;
        event->port = ((const unsigned char *)&in->sin_port)[0] * 256
                      + ((const unsigned char *)&in->sin_port)[1];
        memcpy( event->address, &in->sin_addr, 4 );
    }
    else if (addr->sa_family == AF_INET6 && len >= sizeof(struct sockaddr_in6))
    {
        const struct sockaddr_in6 *in = (const struct sockaddr_in6 *)addr;
        event->family = AF_INET6;
        event->port = ((const unsigned char *)&in->sin6_port)[0] * 256
                      + ((const unsigned char *)&in->sin6_port)[1];
        event->scope = in->sin6_scope_id;
        memcpy( event->address, &in->sin6_addr, 16 );
    }
    else return;
    event->flags &= ~ND_ENDPOINT_UNKNOWN;
}

static inline struct wine_netdiag_event netdiag_begin( unsigned int api, SOCKET socket )
{
    struct wine_netdiag_event event = {0};

    if (!netdiag_enabled()) return event;
    event.api = api;
    event.phase = ND_BEGIN;
    event.handle = socket == INVALID_SOCKET ? UINT64_MAX : (UINT64)socket;
    event.native_fd = -1;
    event.flags = ND_ENDPOINT_UNKNOWN;
    netdiag_call( &event );
    return event;
}

static inline void netdiag_result( struct wine_netdiag_event *event, unsigned int phase,
                                   LONG status, unsigned int error, INT64 returned )
{
    if (!event->operation_id) return;
    event->phase = phase;
    event->status = status;
    event->error = error;
    event->returned = returned;
    event->flags &= ~ND_IS_PENDING;
    if (phase == ND_PENDING || status == STATUS_PENDING) event->flags |= ND_IS_PENDING;
    netdiag_call( event );
}

#define NETDIAG_WAIT_MAX WINE_NETDIAG_WAIT_LIMIT
struct netdiag_wait
{
    struct wine_netdiag_event summary;
    struct wine_netdiag_event members[NETDIAG_WAIT_MAX];
    unsigned int captured;
};

static inline void netdiag_wait_begin( struct netdiag_wait *wait, unsigned int api )
{
    wait->summary = netdiag_begin( api, INVALID_SOCKET );
    wait->captured = 0;
}

static inline void netdiag_wait_prepare( struct netdiag_wait *wait, unsigned int total, LONGLONG timeout )
{
    if (!wait->summary.operation_id) return;
    wait->summary.requested = total;
    wait->summary.wait.timeout_100ns = timeout;
    wait->summary.wait.total = total;
    wait->summary.wait.captured = 0;
    wait->summary.flags |= ND_WAIT_VALID;
    if (total > NETDIAG_WAIT_MAX) wait->summary.flags |= ND_TRUNCATED;
}

/* Snapshot only owned/copied metadata, before the IOCTL can compact its input.
 * ATTEMPT resolves the lifetime once in the bridge; output reuses that value. */
static inline void netdiag_wait_member( struct netdiag_wait *wait, SOCKET socket, unsigned int mask )
{
    struct wine_netdiag_event *member;

    if (!wait->summary.operation_id || wait->captured == NETDIAG_WAIT_MAX) return;
    member = &wait->members[wait->captured++];
    *member = wait->summary;
    member->handle = socket == INVALID_SOCKET ? UINT64_MAX : (UINT64)socket;
    member->lifetime = 0;
    member->flags &= ~(ND_LIFETIME_UNKNOWN | ND_LIFETIME_OBSERVED);
    member->flags |= ND_OUTPUT_UNKNOWN;
    member->requested = mask;
    member->returned = member->status = member->error = 0;
}

static inline void netdiag_wait_attempts( struct netdiag_wait *wait )
{
    unsigned int i;

    wait->summary.wait.captured = wait->captured;
    for (i = 0; i < wait->captured; ++i)
    {
        INT64 output = wait->members[i].returned;
        BOOL output_unknown = !!(wait->members[i].flags & ND_OUTPUT_UNKNOWN);
        wait->members[i].wait.captured = wait->captured;
        wait->members[i].flags |= ND_OUTPUT_UNKNOWN;
        netdiag_result( &wait->members[i], ND_ATTEMPT, 0, 0, 0 );
        wait->members[i].returned = output;
        if (!output_unknown) wait->members[i].flags &= ~ND_OUTPUT_UNKNOWN;
    }
}

static inline void netdiag_wait_finish( struct netdiag_wait *wait, NTSTATUS status, unsigned int error, int returned )
{
    unsigned int i;

    for (i = 0; i < wait->captured; ++i)
        netdiag_result( &wait->members[i], ND_OBSERVED, wait->members[i].status,
                        error, wait->members[i].returned );
    netdiag_result( &wait->summary, ND_COMPLETE, status, error, returned );
}

static inline void netdiag_dns_result( struct wine_netdiag_event *event, int result,
                                       const struct addrinfo *answers )
{
    const struct addrinfo *ai;
    struct wine_netdiag_event answer;
    unsigned int emitted = 0, families = 0;
    UINT64 count = 0;

    if (!event->operation_id) return;
    if (!result)
    {
        for (ai = answers; ai; ai = ai->ai_next)
        {
            count++;
            families |= ai->ai_family == AF_INET ? 1 : ai->ai_family == AF_INET6 ? 2 : 4;
            if (emitted++ >= 8) continue;
            answer = *event;
            netdiag_endpoint( &answer, ai->ai_addr, ai->ai_addrlen );
            netdiag_result( &answer, ND_DNS_ANSWER, 0, 0, 0 );
        }
    }
    /* DNS completion family is a set: bit 0 IPv4, bit 1 IPv6, bit 2 other.
     * ND_ENDPOINT_UNKNOWN marks the summary, which has no endpoint.
     * Answer records retain normal AF_* family values. */
    event->requested = count;
    event->family = families;
    netdiag_result( event, ND_COMPLETE, result, result, result );
}

#endif
