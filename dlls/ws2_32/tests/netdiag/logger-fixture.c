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
