/* Opt-in metadata-only network diagnostic protocol. LGPL-2.1-or-later. */
#ifndef __WINE_NETDIAG_H
#define __WINE_NETDIAG_H
#include <stdint.h>

enum wine_netdiag_api
{
    ND_DNS = 1, ND_CONNECT, ND_CONNECTEX, ND_SEND, ND_RECV,
    ND_SOCKET, ND_CLOSE, ND_NETWORK_EVENT, ND_OVERLAPPED, ND_LEGACY_DNS
};
enum wine_netdiag_phase
{
    ND_BEGIN = 1, ND_ATTEMPT, ND_PENDING, ND_COMPLETE, ND_OBSERVED,
    ND_DNS_ANSWER, ND_OPEN, ND_CLOSED
};
enum wine_netdiag_flags
{
    ND_ERRNO_VALID = 1, ND_IS_PENDING = 2, ND_ENDPOINT_UNKNOWN = 4,
    ND_LIFETIME_OBSERVED = 8, ND_LIFETIME_UNKNOWN = 16
};

/* Pointer-free PE/Unix bridge and explicit on-disk record, same for x86/x64.
 * sequence is committed last in the mapped record. time_ns in a call context
 * holds operation start; record time_ns holds observation time. */
struct wine_netdiag_event
{
    uint64_t sequence, time_ns, operation_id, lifetime, handle, requested;
    int64_t returned;
    uint64_t elapsed_ns;
    uint32_t api, phase, status, error;
    int32_t native_fd, native_errno;
    uint32_t family, port, scope, flags;
    uint8_t address[16];
    uint32_t host_pid, thread_id;
};
_Static_assert(sizeof(struct wine_netdiag_event) == 128, "netdiag record ABI");

#define WINE_NETDIAG_CAPACITY 16384u
#define WINE_NETDIAG_MAP_HEADER 16384u
#define WINE_NETDIAG_MAX_NS UINT64_C(45000000000)
#define WINE_NETDIAG_MAGIC UINT64_C(0x31474454454e5756)

struct wine_netdiag_lifetime { uint64_t handle, lifetime; };
struct wine_netdiag_header
{
    uint64_t magic;
    uint32_t version, record_size, capacity, state;
    uint64_t start_ns, deadline_ns, reserved, dropped, next_operation, next_lifetime;
    uint32_t host_pid, windows_pid, lifetime_lock, reserved0;
    char token[32], executable[512];
    struct wine_netdiag_lifetime sockets[512];
};
_Static_assert(sizeof(struct wine_netdiag_header) <= WINE_NETDIAG_MAP_HEADER, "netdiag header bound");
#define WINE_NETDIAG_FILE_SIZE (WINE_NETDIAG_MAP_HEADER + WINE_NETDIAG_CAPACITY * 128u)

#endif
