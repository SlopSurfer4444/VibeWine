/* Native Unix-header regression fixture; no Wine runtime required.
 * LGPL-2.1-or-later. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#define _DEFAULT_SOURCE 1
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/socket.h>
#define FIXTURE_EXE "C:\\fixture\\netdiag.exe"
#define WINE_NETDIAG_IMAGE_MATCH(s) (!strcmp((s), FIXTURE_EXE))
#define WINE_NETDIAG_WINDOWS_PID UINT32_C(4242)
#define WINE_NETDIAG_THREAD_ID UINT32_C(77)
#include "wine/netdiag_unix.h"

static void begin(struct wine_netdiag_event *e, uint32_t api, uint64_t handle)
{
    errno = EDOM; wine_netdiag_begin(e, api, handle); assert(errno == EDOM);
}
static void emit(struct wine_netdiag_event *e)
{
    errno = ERANGE; wine_netdiag_emit(e); assert(errno == ERANGE);
}
static void *worker(void *arg)
{
    uintptr_t i, base = (uintptr_t)arg;
    for (i = 1; i <= 500; ++i)
    {
        struct wine_netdiag_event e;
        begin(&e, ND_SEND, UINT64_MAX);
        e.phase = ND_COMPLETE; e.requested = base + i; e.returned = -(int64_t)e.requested;
        e.native_errno = EAGAIN; e.flags |= ND_ERRNO_VALID;
        emit(&e);
    }
    return NULL;
}
static void lifetimes(void)
{
    struct wine_netdiag_event e, pending, later;
    uint64_t observed, opened;
    begin(&e, ND_RECV, 42);
    if (!e.operation_id) return;
    observed = e.lifetime; assert(observed && (e.flags & ND_LIFETIME_OBSERVED));
    begin(&pending, ND_RECV, 42); assert(pending.lifetime == observed);
    begin(&e, ND_SOCKET, 42); e.phase = ND_OPEN; emit(&e);
    opened = e.lifetime; assert(opened && opened != observed);
    assert(!(e.flags & (ND_LIFETIME_OBSERVED | ND_LIFETIME_UNKNOWN)));
    /* A delayed close for the observed generation cannot erase the new one. */
    pending.phase = ND_CLOSED; emit(&pending);
    begin(&later, ND_SEND, 42); assert(later.lifetime == opened);
    e.phase = ND_CLOSED; emit(&e);
    begin(&later, ND_SEND, 42); assert(later.lifetime && later.lifetime != opened);
    assert(later.flags & ND_LIFETIME_OBSERVED);
    begin(&e, ND_SOCKET, 42); e.phase = ND_OPEN; emit(&e);
    assert(e.lifetime != later.lifetime);
}
static void errno_branches(void)
{
    struct wine_netdiag_event e;
    begin(&e, ND_SEND, 0); e.phase = ND_COMPLETE; emit(&e);
    memset(&e, 0, sizeof(e)); emit(&e);
    if (!wine_netdiag_mapping) return;
    __atomic_store_n(&wine_netdiag_mapping->lifetime_lock, 1, __ATOMIC_RELEASE);
    begin(&e, ND_RECV, 99);
    __atomic_store_n(&wine_netdiag_mapping->lifetime_lock, 0, __ATOMIC_RELEASE);
    if (e.operation_id) assert(e.flags & ND_LIFETIME_UNKNOWN);
}
static void encoded_wait(uint32_t api, int64_t timeout, uint32_t total, uint32_t captured, int invalid)
{
    struct wine_netdiag_header *map = wine_netdiag_active();
    struct wine_netdiag_event summary, member;
    unsigned int i;
    if (!map) return;
    memset(&summary, 0, sizeof(summary));
    summary.api = api; summary.phase = ND_BEGIN; summary.handle = UINT64_MAX;
    summary.native_fd = -1; summary.flags = ND_ENDPOINT_UNKNOWN | ND_WAIT_VALID;
    if (captured < total) summary.flags |= ND_TRUNCATED;
    summary.time_ns = wine_netdiag_now();
    summary.operation_id = __atomic_add_fetch(&map->next_operation, 1, __ATOMIC_RELAXED);
    summary.requested = total;
    summary.wait.timeout_100ns = timeout; summary.wait.total = total; summary.wait.captured = captured;
    emit(&summary);
    for (i = 0; i < captured; ++i)
    {
        member = summary; member.phase = ND_ATTEMPT; member.handle = 100 + i;
        member.flags |= ND_OUTPUT_UNKNOWN;
        member.requested = api == ND_SELECT ? 1u | 4u : 0x100u | 0x10u;
        if (invalid)
        {
            member.handle = UINT64_MAX; member.requested = 0;
            member.flags |= ND_MASK_UNKNOWN | ND_LIFETIME_UNKNOWN;
        }
        else
        {
            wine_netdiag_lifetime(map, &member, 0, 0);
            assert(member.lifetime);
        }
        emit(&member);
        member.phase = ND_OBSERVED;
        if (i != 1) member.flags &= ~ND_OUTPUT_UNKNOWN;
        member.returned = i ? 0 : (api == ND_SELECT ? 1 : invalid ? 4 : 0x100);
        member.status = i == 1 ? UINT32_C(0xc0000001) : 0;
        member.error = 0;
        emit(&member);
    }
    summary.phase = ND_COMPLETE; summary.returned = 1;
    emit(&summary);
}
static void encoded_metadata(void)
{
    struct wine_netdiag_event e;
    _Static_assert(sizeof(e) == 128 && offsetof(struct wine_netdiag_event, address) == 104 &&
                   offsetof(struct wine_netdiag_event, wait) == 104, "version 1 union layout");
    begin(&e, ND_SOCKET, UINT64_MAX);
    e.phase = ND_OPEN; e.handle = 77; e.family = 2;
    e.requested = UINT64_C(6) << 32 | 1; e.flags |= ND_SOCKET_INFO;
    emit(&e);
    begin(&e, ND_SOCKET, UINT64_MAX);
    e.phase = ND_OPEN; e.handle = 78; e.family = 23;
    e.requested = (uint64_t)(uint32_t)-2 << 32 | (uint32_t)-1; e.flags |= ND_SOCKET_INFO;
    emit(&e);
    encoded_wait(ND_SELECT, -123450, 2, 2, 0);
    encoded_wait(ND_WSAPOLL, INT64_MAX, 1, 1, 0);
    encoded_wait(ND_SELECT, 0, 19, 16, 0);
    encoded_wait(ND_WSAPOLL, -10000, 1, 1, 1);
    /* A failed call before safe input capture has no timeout/count metadata. */
    begin(&e, ND_WSAPOLL, UINT64_MAX);
    e.phase = ND_COMPLETE; e.returned = -1; e.status = UINT32_C(0xc0000005); e.error = 10014;
    e.wait.timeout_100ns = -999; e.wait.total = 999; e.wait.captured = 999;
    emit(&e);
}
int main(void)
{
    struct wine_netdiag_event e;
    char line[128], ch = 0;
    int pair[2];
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    setvbuf(stdout, NULL, _IOLBF, 0);
    begin(&e, ND_SEND, UINT64_MAX); /* claims before controller arms */
    printf("{\"ready\":true,\"claimed\":%s}\n", wine_netdiag_mapping ? "true" : "false");
    while (fgets(line, sizeof(line), stdin))
    {
        if (!strncmp(line, "emit ", 5))
        {
            unsigned int i, count = (unsigned int)strtoul(line + 5, NULL, 10);
            for (i = 0; i < count; ++i)
            {
                begin(&e, ND_SEND, UINT64_MAX); e.phase = ND_COMPLETE;
                e.requested = i + 1; e.returned = -(int64_t)e.requested; emit(&e);
            }
        }
        else if (!strcmp(line, "concurrent\n"))
        {
            pthread_t threads[4]; unsigned int i;
            for (i = 0; i < 4; ++i) assert(!pthread_create(threads + i, NULL, worker, (void *)(uintptr_t)(i * 1000)));
            for (i = 0; i < 4; ++i) assert(!pthread_join(threads[i], NULL));
        }
        else if (!strcmp(line, "lifetimes\n")) lifetimes();
        else if (!strcmp(line, "errno\n")) errno_branches();
        else if (!strcmp(line, "metadata\n")) encoded_metadata();
        else if (!strcmp(line, "socket\n"))
        {
            begin(&e, ND_SEND, pair[0]);
            assert(send(pair[0], "x", 1, 0) == 1 && recv(pair[1], &ch, 1, 0) == 1 && ch == 'x');
            e.phase = ND_COMPLETE; e.requested = 1; e.returned = 1; emit(&e);
            begin(&e, ND_RECV, pair[1]);
            errno = 0; assert(recv(pair[1], &ch, 1, MSG_DONTWAIT) == -1);
            assert(errno == EAGAIN || errno == EWOULDBLOCK);
            e.native_errno = errno; e.flags |= ND_ERRNO_VALID; e.returned = -1; e.phase = ND_COMPLETE;
            { int saved = errno; wine_netdiag_emit(&e); assert(errno == saved); }
        }
        else if (!strcmp(line, "quit\n")) break;
        else return 2;
        printf("{\"ok\":true,\"active\":%s}\n", wine_netdiag_active() ? "true" : "false");
    }
    close(pair[0]); close(pair[1]); return 0;
}
