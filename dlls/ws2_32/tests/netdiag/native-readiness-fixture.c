/* Exact shipping C is supplied by run_native_readiness.py. External dependencies
 * are deterministic native unit seams; this is not Wine integration.
 * LGPL-2.1-or-later. */
#define _GNU_SOURCE
#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "ntstatus.h"
#include "wine/netdiag.h"
typedef uintptr_t SOCKET;
typedef void *HANDLE;
typedef int32_t NTSTATUS, LONG;
typedef uint32_t DWORD, ULONG;
typedef uint64_t UINT64, ULONGLONG;
typedef int64_t INT64, LONGLONG;
typedef intptr_t INT_PTR;
typedef size_t SIZE_T;
typedef int BOOL;
typedef unsigned char BOOLEAN;
typedef struct { NTSTATUS Status; } IO_STATUS_BLOCK;
/* Windows layouts/constants from winsock2.h and wine/afd.h. Host fd_set and
 * timeval names are intentionally kept separate from the shipping functions. */
typedef struct wine_fd_set { unsigned int fd_count; SOCKET fd_array[64]; } wine_fd_set;
typedef struct { SOCKET fd; short events, revents; } WSAPOLLFD;
struct wine_timeval { int32_t tv_sec, tv_usec; };
#pragma pack(push,4)
struct afd_poll_params
{
    LONGLONG timeout;
    unsigned int count;
    BOOLEAN exclusive, padding[3];
    struct afd_poll_socket { SOCKET socket; int flags, status; } sockets[1];
};
#pragma pack(pop)
#define WINAPI
#define TRUE 1
#define FALSE 0
#define INVALID_SOCKET (~(SOCKET)0)
#define SOCKET_ERROR (-1)
#define TIMEOUT_INFINITE INT64_MAX
#define WSAEINVAL 10022
#define WSAEFAULT 10014
#define WSAENOBUFS 10055
#define WSAENOTSOCK 10038
#define WAIT_FAILED (~0u)
#define IOCTL_AFD_POLL 0x12024
#define AFD_POLL_READ 0x0001
#define AFD_POLL_OOB 0x0002
#define AFD_POLL_WRITE 0x0004
#define AFD_POLL_HUP 0x0008
#define AFD_POLL_RESET 0x0010
#define AFD_POLL_CLOSE 0x0020
#define AFD_POLL_CONNECT 0x0040
#define AFD_POLL_ACCEPT 0x0080
#define AFD_POLL_CONNECT_ERR 0x0100
#define POLLERR 0x0001
#define POLLHUP 0x0002
#define POLLNVAL 0x0004
#define POLLWRNORM 0x0010
#define POLLRDNORM 0x0100
#define POLLRDBAND 0x0200
#define TRACE(...) ((void)0)
#define FIXME(...) ((void)0)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
static DWORD last_error;
static unsigned int allocations, fail_allocation, kernel_calls, observations, event_count;
static int sync_failure, armed = 1;
static int wait_failure;
static int compact_output;
static SOCKET only_ready_socket;
static NTSTATUS kernel_status, pending_status;
static unsigned int ready_flags;
static struct wine_netdiag_event events[256];
static DWORD GetLastError(void) { return last_error; }
static void SetLastError(DWORD value) { last_error = value; }
static LONG InterlockedCompareExchange(LONG *target, LONG value, LONG expected)
{ LONG old = *target; if (old == expected) *target = value; return old; }
static DWORD GetEnvironmentVariableA(const char *name, char *value, DWORD size)
{ (void)name; (void)value; (void)size; last_error = 0xabc1; ++observations; return armed; }
static void observe(struct wine_netdiag_event *event)
{
    ++observations;
    last_error = 0xabc2;
    if (event->phase == ND_BEGIN && !event->operation_id) event->operation_id = 17;
    if (event->phase == ND_ATTEMPT) event->lifetime = 9000 + event->handle;
    assert(event_count < ARRAY_SIZE(events));
    events[event_count++] = *event;
}
#define WINE_UNIX_CALL(code,event) observe(event)
static HANDLE get_sync_event(void)
{ if (sync_failure) { SetLastError(1234); return NULL; } return (HANDLE)1; }
static DWORD NtStatusToWSAError(NTSTATUS status)
{ return status == STATUS_SUCCESS ? 0 : 10000u + ((unsigned int)status & 255); }
static int socket_list_find(SOCKET socket) { return socket != 0 && socket != 999; }
static unsigned int wait_event_alertable(HANDLE event)
{ (void)event; if (wait_failure) { SetLastError(4321); return WAIT_FAILED; } return 0; }
static NTSTATUS NtDeviceIoControlFile(HANDLE socket, HANDLE event, void *apc, void *context,
                                     IO_STATUS_BLOCK *io, unsigned int code, void *input,
                                     ULONG input_size, void *output, ULONG output_size)
{
    struct afd_poll_params *params = input;
    unsigned int i;
    (void)socket; (void)event; (void)apc; (void)context; (void)code;
    (void)input_size; (void)output; (void)output_size;
    ++kernel_calls;
    for (i = 0; i < params->count; ++i)
        params->sockets[i].flags &= (!only_ready_socket || params->sockets[i].socket == only_ready_socket) ? ready_flags : 0;
    if (compact_output)
    {
        unsigned int output_count = 0;
        for (i = 0; i < params->count; ++i)
            if (params->sockets[i].flags) params->sockets[output_count++] = params->sockets[i];
        params->count = output_count;
        assert(params->count == 1 && params->sockets[0].socket == only_ready_socket);
    }
    io->Status = pending_status;
    return kernel_status;
}
static void *fixture_calloc(size_t count, size_t size)
{ return ++allocations == fail_allocation ? NULL : calloc(count, size); }
static void *fixture_malloc(size_t size)
{ return ++allocations == fail_allocation ? NULL : malloc(size); }
#define calloc fixture_calloc
#define malloc fixture_malloc
#define fd_set wine_fd_set
#define timeval wine_timeval
#define select wine_select
/* The unmodified Wine bodies contain ignored API parameters and signed/unsigned
 * comparisons. Keep these existing warnings scoped to the extracted source. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "shipping-readiness.inc"
#pragma GCC diagnostic pop
#undef select
#undef timeval
#undef fd_set
#undef malloc
#undef calloc

static void reset(void)
{
    allocations = fail_allocation = kernel_calls = observations = event_count = 0;
    sync_failure = wait_failure = 0;
    compact_output = 0;
    only_ready_socket = 0;
    last_error = 77;
    kernel_status = pending_status = STATUS_SUCCESS;
    ready_flags = 0;
}
static const struct wine_netdiag_event *summary(unsigned int api, unsigned int total,
                                                unsigned int captured, int result, DWORD error)
{
    const struct wine_netdiag_event *complete = NULL;
    unsigned int i, begins = 0, attempts = 0, observed = 0;
    for (i = 0; i < event_count; ++i)
    {
        const struct wine_netdiag_event *event = &events[i];
        assert(event->api == api && event->operation_id == 17);
        if (event->phase == ND_BEGIN) ++begins;
        else if (event->phase == ND_ATTEMPT)
        {
            ++attempts;
            assert(event->wait.total == total && event->wait.captured == captured);
        }
        else if (event->phase == ND_OBSERVED)
        {
            const struct wine_netdiag_event *attempt = &events[observed + 1];
            assert(attempt->phase == ND_ATTEMPT);
            assert(event->handle == attempt->handle && event->requested == attempt->requested);
            assert(event->lifetime == attempt->lifetime && event->lifetime == 9000 + event->handle);
            ++observed;
        }
        else if (event->phase == ND_COMPLETE) { assert(!complete); complete = event; }
        else assert(0);
    }
    assert(begins == 1 && attempts == captured && observed == captured && complete);
    assert(complete->wait.total == total && complete->wait.captured == captured);
    assert(complete->returned == result && complete->error == error);
    assert(last_error == error);
    return complete;
}
static void *inaccessible(void)
{
    long size = sysconf(_SC_PAGESIZE);
    void *page = mmap(NULL, (size_t)size, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(page != MAP_FAILED);
    return page;
}
static wine_fd_set *inaccessible_members(void)
{
    long size = sysconf(_SC_PAGESIZE);
    char *base = mmap(NULL, (size_t)size * 2, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    wine_fd_set *set;
    assert(base != MAP_FAILED && !mprotect(base, (size_t)size, PROT_READ | PROT_WRITE));
    set = (wine_fd_set *)(base + size - offsetof(wine_fd_set, fd_array));
    set->fd_count = 1;
    return set;
}
static void validation(void)
{
    void *bad = inaccessible();
    wine_fd_set empty = {0};
    reset(); assert(WSAPoll(NULL, 0, 0) == -1 && last_error == WSAEINVAL);
    assert(!(summary(ND_WSAPOLL, 0, 0, -1, WSAEINVAL)->flags & ND_WAIT_VALID));
    assert(!kernel_calls);
    reset(); assert(WSAPoll(bad, 0, 0) == -1 && last_error == WSAEINVAL);
    summary(ND_WSAPOLL, 0, 0, -1, WSAEINVAL);
    reset(); assert(WSAPoll(NULL, 1, 0) == -1 && last_error == WSAEFAULT);
    assert(!(summary(ND_WSAPOLL, 1, 0, -1, WSAEFAULT)->flags & ND_WAIT_VALID));
    reset(); sync_failure = 1;
    assert(wine_select(0, bad, bad, bad, bad) == -1 && last_error == 1234);
    summary(ND_SELECT, 0, 0, -1, 1234);
    reset(); sync_failure = 1;
    assert(WSAPoll(bad, 1, 0) == -1 && last_error == 1234);
    summary(ND_WSAPOLL, 1, 0, -1, 1234);
    reset(); assert(wine_select(0, &empty, NULL, NULL, bad) == -1 && last_error == WSAEINVAL);
    summary(ND_SELECT, 0, 0, -1, WSAEINVAL);
    assert(!kernel_calls);
    reset(); assert(wine_select(0, NULL, NULL, NULL, bad) == -1 && last_error == WSAEINVAL);
    summary(ND_SELECT, 0, 0, -1, WSAEINVAL);
    puts("PASS validation: NULL/count precedence and pre-validator inaccessible pointers preserve errors");
}
static void allocation(void)
{
    void *bad = inaccessible();
    wine_fd_set *guard = inaccessible_members();
    wine_fd_set set = {1, {42}};
    reset(); fail_allocation = 1;
    assert(wine_select(0, &set, NULL, NULL, bad) == -1 && last_error == WSAENOBUFS);
    assert(!(summary(ND_SELECT, 1, 0, -1, WSAENOBUFS)->flags & ND_WAIT_VALID));
    assert(!kernel_calls);
    reset(); fail_allocation = 1;
    assert(wine_select(0, guard, NULL, NULL, bad) == -1 && last_error == WSAENOBUFS);
    summary(ND_SELECT, 1, 0, -1, WSAENOBUFS);
    assert(!kernel_calls);
    reset(); fail_allocation = 1;
    assert(WSAPoll(bad, 1, 0) == -1 && last_error == WSAENOBUFS);
    assert(!(summary(ND_WSAPOLL, 1, 0, -1, WSAENOBUFS)->flags & ND_WAIT_VALID));
    assert(!kernel_calls);
    reset(); fail_allocation = 2;
    assert(wine_select(0, &set, NULL, NULL, NULL) == -1 && last_error == WSAENOBUFS);
    summary(ND_SELECT, 1, 0, -1, WSAENOBUFS);
    assert(!kernel_calls);
    puts("PASS allocation: parameter failure never reads protected timeout, poll members, or select member array; read-copy failure retains ENOBUFS");
}
static void alias_case(void)
{
    wine_fd_set set = {2, {42, 43}};
    struct wine_timeval timeout = {0, 0};
    reset(); ready_flags = AFD_POLL_READ | AFD_POLL_WRITE | AFD_POLL_OOB;
    assert(wine_select(0, &set, &set, &set, &timeout) == 2);
    assert(set.fd_count == 2 && set.fd_array[0] == 42 && set.fd_array[1] == 43 && last_error == 0);
    assert(kernel_calls == 1);
    assert(summary(ND_SELECT, 6, 6, 2, 0)->wait.timeout_100ns == 0);
    assert(events[1].requested == (AFD_POLL_READ | AFD_POLL_ACCEPT | AFD_POLL_HUP | AFD_POLL_RESET));
    assert(events[3].requested == AFD_POLL_WRITE);
    assert(events[5].requested == (AFD_POLL_OOB | AFD_POLL_CONNECT_ERR));
    assert(events[7].returned == AFD_POLL_READ && events[9].returned == AFD_POLL_WRITE && events[11].returned == AFD_POLL_OOB);
    puts("PASS alias: shared read/write/except set, exact input mask snapshots, one summary, and stable member lifetimes");
}
static void bounded(void)
{
    WSAPOLLFD fds[17]; wine_fd_set set = {17, {0}}; unsigned int i;
    reset();
    for (i = 0; i < ARRAY_SIZE(fds); ++i)
    { fds[i] = (WSAPOLLFD){42 + i, POLLRDNORM, 0}; set.fd_array[i] = 42 + i; }
    assert(WSAPoll(fds, ARRAY_SIZE(fds), -1) == 0 && last_error == 0);
    assert(kernel_calls == 1);
    assert(summary(ND_WSAPOLL, 17, 16, 0, 0)->flags & ND_TRUNCATED);
    assert(event_count == 34 && events[33].wait.timeout_100ns == TIMEOUT_INFINITE);
    for (i = 0; i < 16; ++i) assert(events[i + 1].handle == 42 + i && events[i + 1].requested == POLLRDNORM);
    reset();
    assert(wine_select(0, &set, NULL, NULL, NULL) == 0 && set.fd_count == 0);
    assert(summary(ND_SELECT, 17, 16, 0, 0)->flags & ND_TRUNCATED);
    assert(event_count == 34);
    puts("PASS bounded: select and poll each report total17/captured16, exact distinct snapshots, truncation, stable lifetimes, and one summary");
}
static void compact_case(void)
{
    wine_fd_set set = {2, {42, 43}};
    WSAPOLLFD fds[2] = {{42, POLLRDNORM, 0}, {43, POLLRDNORM, 0}};
    reset(); ready_flags = AFD_POLL_READ; only_ready_socket = 43; compact_output = 1;
    assert(wine_select(0, &set, NULL, NULL, NULL) == 1 && set.fd_count == 1 && set.fd_array[0] == 43);
    summary(ND_SELECT, 2, 2, 1, 0);
    assert(events[1].handle == 42 && events[2].handle == 43);
    assert(events[3].handle == 42 && events[3].returned == 0);
    assert(events[4].handle == 43 && events[4].returned == AFD_POLL_READ);
    reset(); ready_flags = AFD_POLL_READ; only_ready_socket = 43; compact_output = 1;
    assert(WSAPoll(fds, 2, 0) == 1 && !fds[0].revents && fds[1].revents == POLLRDNORM);
    summary(ND_WSAPOLL, 2, 2, 1, 0);
    assert(events[3].handle == 42 && events[3].returned == 0);
    assert(events[4].handle == 43 && events[4].returned == POLLRDNORM);
    puts("PASS compact: kernel compaction moves second socket to output index0; select/poll snapshots and bound lifetimes retain original members");
}
static void poll_case(void)
{
    WSAPOLLFD mixed[2] = {{999, POLLRDNORM, 0}, {42, POLLRDNORM, 0}};
    WSAPOLLFD invalid = {999, POLLRDNORM, 0};
    reset(); assert(WSAPoll(mixed, 2, 0) == 0 && mixed[0].revents == POLLNVAL && !mixed[1].revents && last_error == 0);
    summary(ND_WSAPOLL, 2, 2, 0, 0);
    assert(events[1].flags & ND_MASK_UNKNOWN);
    assert(events[3].returned == POLLNVAL && events[4].returned == 0);
    reset(); assert(WSAPoll(&invalid, 1, 0) == -1 && invalid.revents == POLLNVAL && last_error == WSAENOTSOCK);
    summary(ND_WSAPOLL, 1, 1, -1, WSAENOTSOCK);
    reset(); ready_flags = AFD_POLL_READ;
    assert(WSAPoll(&mixed[1], 1, 0) == 1 && mixed[1].revents == POLLRDNORM && last_error == 0);
    summary(ND_WSAPOLL, 1, 1, 1, 0);
    puts("PASS poll: mixed invalid/valid empty count, all-invalid ENOTSOCK, and ready event mask");
}
static void observer(void)
{
    WSAPOLLFD fd = {42, POLLRDNORM, 0};
    struct netdiag_wait wait;
    reset();
    netdiag_wait_begin(&wait, ND_WSAPOLL);
    assert(last_error == 77 && observations == 2);
    netdiag_wait_prepare(&wait, 1, 0);
    netdiag_wait_member(&wait, 42, POLLRDNORM);
    netdiag_wait_attempts(&wait);
    assert(last_error == 77);
    netdiag_wait_finish(&wait, 0, 77, 0);
    assert(last_error == 77);
    reset(); ready_flags = AFD_POLL_READ;
    assert(WSAPoll(&fd, 1, 0) == 1 && last_error == 0);
    summary(ND_WSAPOLL, 1, 1, 1, 0);
    assert(observations >= 4 && event_count == 4);
    puts("PASS observer: shipping poll result error survives deterministic observation poisoning");
}
static void status_case(void)
{
    WSAPOLLFD fd = {42, POLLRDNORM, 0}; wine_fd_set set = {1, {42}};
    reset(); kernel_status = STATUS_TIMEOUT;
    assert(WSAPoll(&fd, 1, 25) == 0 && !fd.revents);
    assert(summary(ND_WSAPOLL, 1, 1, 0, 0)->status == STATUS_SUCCESS);
    assert(events[event_count - 1].wait.timeout_100ns == -250000);
    reset(); kernel_status = STATUS_PENDING; ready_flags = AFD_POLL_READ;
    assert(wine_select(0, &set, NULL, NULL, NULL) == 1);
    summary(ND_SELECT, 1, 1, 1, 0);
    reset(); kernel_status = STATUS_INVALID_HANDLE;
    assert(WSAPoll(&fd, 1, 0) == -1);
    assert(summary(ND_WSAPOLL, 1, 1, -1, NtStatusToWSAError(STATUS_INVALID_HANDLE))->status == (uint32_t)STATUS_INVALID_HANDLE);
    reset(); kernel_status = STATUS_PENDING; wait_failure = 1;
    assert(WSAPoll(&fd, 1, 0) == -1 && last_error == 4321);
    summary(ND_WSAPOLL, 1, 1, -1, 4321);
    puts("PASS status: timeout normalization, finite timeout units, pending success, kernel error, and failed wait preserve return/error semantics");
}
static void unarmed(void)
{
    WSAPOLLFD fd = {42, POLLRDNORM, 0};
    reset(); armed = 0;
    assert(WSAPoll(&fd, 1, 0) == 0 && last_error == 0 && event_count == 0);
    assert(observations == 1);
    puts("PASS unarmed: disabled observation emits no events and preserves poll result");
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!strcmp(argv[1], "validation")) validation();
    else if (!strcmp(argv[1], "allocation")) allocation();
    else if (!strcmp(argv[1], "alias")) alias_case();
    else if (!strcmp(argv[1], "bounded")) bounded();
    else if (!strcmp(argv[1], "compact")) compact_case();
    else if (!strcmp(argv[1], "poll")) poll_case();
    else if (!strcmp(argv[1], "observer")) observer();
    else if (!strcmp(argv[1], "status")) status_case();
    else if (!strcmp(argv[1], "unarmed")) unarmed();
    else return 2;
    return 0;
}
