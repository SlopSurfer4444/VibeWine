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
