/* Exact shipping hooks are supplied by run_native_hooks.py, never copied here.
 * External Wine/kernel dependencies below are deterministic unit-test seams.
 * LGPL-2.1-or-later. */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

typedef int32_t NTSTATUS;
typedef uintptr_t ULONG_PTR;
typedef unsigned int DWORD;
typedef int BOOL;
typedef void *HANDLE;
typedef void (*PIO_APC_ROUTINE)(void);
typedef struct { int unused; } IO_STATUS_BLOCK;
typedef struct { unsigned int len; char *buf; } WSABUF;
struct WS_sockaddr { unsigned short sa_family; char data[14]; };
struct afd_wsabuf_32 { unsigned int len, buf; };
struct async_fileio { HANDLE handle; };
struct async_recv_ioctl;
struct async_send_ioctl;
union unix_sockaddr { struct sockaddr addr; struct sockaddr_in in; struct sockaddr_in6 in6; };
#include "ntstatus.h"
#define TRUE 1
#define FALSE 0
#define NT_SUCCESS(status) ((NTSTATUS)(status) >= 0)
#define WS_AF_INET 2
#define WS_AF_INET6 23
#define WS_MSG_CTRUNC 0x200
#define SERVER_SOCKET_IO_FORCE_ASYNC 1
#define ULongToPtr(value) ((void *)(uintptr_t)(value))
#define TRACE(...) ((void)0)
#define WARN(...) ((void)0)
#define ERR(...) ((void)0)
#define FIXME(...) ((void)0)
#define WINE_NETDIAG_IMAGE_MATCH(value) ((void)(value), 1)
#define WINE_NETDIAG_WINDOWS_PID 4242
#define WINE_NETDIAG_THREAD_ID 77
#define wine_netdiag_now actual_netdiag_now
#define wine_netdiag_emit actual_netdiag_emit
#include "wine/netdiag_unix.h"
#undef wine_netdiag_now
#undef wine_netdiag_emit

static unsigned int observers;
static uint64_t wine_netdiag_now(void)
{
    uint64_t result = actual_netdiag_now();
    ++observers;
    errno = EDOM;
    return result;
}
static void wine_netdiag_emit(struct wine_netdiag_event *event)
{
    actual_netdiag_emit(event);
    ++observers;
    errno = ERANGE;
}

static unsigned int calls, outcomes, cursor, releases, closes, allocations;
static unsigned int entered, left, checked, updated, downstream;
static int errors[8], output_flags[8], fd_error, close_needed;
static ssize_t results[8];
static int use_kernel_writewatch, virtual_mutex;
static void *protected_page;
static size_t page_size;
static NTSTATUS sock_errno_to_status(int error)
{
    if (error == EWOULDBLOCK) return STATUS_DEVICE_NOT_READY;
    if (error == EFAULT) return STATUS_ACCESS_VIOLATION;
    return STATUS_UNSUCCESSFUL;
}
static ssize_t mock_recvmsg(int fd, struct msghdr *hdr, int flags)
{
    unsigned int index = cursor++;
    (void)fd; (void)flags;
    assert(index < outcomes);
    ++calls;
    if (hdr != protected_page) hdr->msg_flags = output_flags[index];
    errno = errors[index];
    return results[index];
}
static ssize_t mock_sendmsg(int fd, const struct msghdr *hdr, int flags)
{
    (void)hdr;
    return mock_recvmsg(fd, protected_page, flags);
}
static int mock_close(int fd) { (void)fd; ++closes; return 0; }
static int mock_getsockopt(int fd, int level, int option, void *value, socklen_t *len)
{
    (void)fd; (void)level; (void)len;
    *(int *)value = option == SO_TYPE ? SOCK_STREAM : 0;
    return 0;
}
static void server_enter_uninterrupted_section(void *mutex, sigset_t *set)
{ (void)mutex; (void)set; ++entered; }
static void server_leave_uninterrupted_section(void *mutex, sigset_t *set)
{ (void)mutex; (void)set; ++left; errno = ENOTTY; }
static int check_write_access(void *base, size_t length, BOOL *watch)
{ (void)base; (void)length; ++checked; *watch = TRUE; return 0; }
static void update_write_watches(void *base, size_t length, int value)
{ (void)base; (void)length; (void)value; ++updated; }
static int in_wow64_call(void) { return 0; }
static ssize_t fixup_icmp_over_dgram(struct msghdr *hdr, union unix_sockaddr *addr,
                                    HANDLE handle, ssize_t ret, NTSTATUS *status)
{ (void)hdr; (void)addr; (void)handle; (void)ret; (void)status; abort(); }
static BOOL convert_control_headers(struct msghdr *hdr, WSABUF *control)
{ (void)hdr; (void)control; abort(); }
static BOOL wow64_translate_control(WSABUF *buf, void *control)
{ (void)buf; (void)control; abort(); }
static int sockaddr_from_unix(union unix_sockaddr *from, struct WS_sockaddr *to, int len)
{ (void)from; (void)to; (void)len; abort(); }
static int sockaddr_to_unix(const struct WS_sockaddr *from, int len, union unix_sockaddr *to)
{ (void)from; (void)to; (void)len; abort(); }
static NTSTATUS server_get_unix_fd(HANDLE handle, unsigned int access, int *fd,
                                   int *needs_close, void *type, void *options)
{
    (void)handle; (void)access; (void)type; (void)options;
    *fd = 19; *needs_close = close_needed;
    return fd_error;
}
static void release_fileio(struct async_fileio *io) { (void)io; ++releases; }
static struct async_fileio *alloc_fileio(DWORD size,
                                       BOOL (*callback)(void *, ULONG_PTR *, unsigned int *), HANDLE handle)
{ (void)size; (void)callback; (void)handle; ++allocations; return NULL; }
static NTSTATUS sock_recv(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, void *user,
                          IO_STATUS_BLOCK *io, int fd, struct async_recv_ioctl *async, int force)
{
    (void)handle; (void)event; (void)apc; (void)user; (void)io;
    (void)fd; (void)async; (void)force; ++downstream; abort();
}
static NTSTATUS sock_send(HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, void *user,
                          IO_STATUS_BLOCK *io, int fd, struct async_send_ioctl *async, int force)
{
    (void)handle; (void)event; (void)apc; (void)user; (void)io;
    (void)fd; (void)async; (void)force; ++downstream; abort();
}
ssize_t virtual_locked_recvmsg(int, struct msghdr *, int, struct wine_netdiag_event *);
#define recvmsg mock_recvmsg
#define sendmsg mock_sendmsg
#define close mock_close
#define getsockopt mock_getsockopt
/* These warnings also occur in the unchanged Wine bodies under this host's
 * socket types; suppress only here, never for the seam or assertions. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "shipping-hooks.inc"
#pragma GCC diagnostic pop
#undef recvmsg
#undef sendmsg
#undef close
#undef getsockopt

static struct wine_netdiag_event *records;
static void reset(void)
{
    uint64_t now = actual_netdiag_now();
    calls = cursor = outcomes = releases = closes = allocations = observers = 0;
    entered = left = checked = updated = downstream = 0;
    fd_error = close_needed = use_kernel_writewatch = 0;
    memset(errors, 0, sizeof(errors));
    memset(output_flags, 0, sizeof(output_flags));
    memset(results, 0, sizeof(results));
    memset(records, 0, WINE_NETDIAG_CAPACITY * sizeof(*records));
    wine_netdiag_mapping->reserved = 0;
    wine_netdiag_mapping->dropped = 0;
    wine_netdiag_mapping->state = 1;
    wine_netdiag_mapping->start_ns = now - 1;
    wine_netdiag_mapping->deadline_ns = now + WINE_NETDIAG_MAX_NS - UINT64_C(1000000000);
}
static void outcome(ssize_t result, int error, int flags)
{
    assert(outcomes < 8);
    results[outcomes] = result; errors[outcomes] = error; output_flags[outcomes++] = flags;
}
static struct wine_netdiag_event context(void)
{
    struct wine_netdiag_event event;
    wine_netdiag_begin(&event, ND_RECV, 17);
    assert(event.operation_id);
    wine_netdiag_mapping->reserved = 0;
    return event;
}
static void test_unarmed(void)
{
    struct wine_netdiag_event event = {0};
    struct msghdr hdr = {0};
    reset(); outcome(-1, EFAULT, 0);
    assert(netdiag_recvmsg(3, protected_page, 0, &event) == -1);
    assert(errno == EFAULT && calls == 1 && !observers && !wine_netdiag_mapping->reserved);
    reset(); outcome(-1, EINVAL, 0);
    hdr.msg_iov = protected_page; hdr.msg_iovlen = 50;
    hdr.msg_name = protected_page; hdr.msg_namelen = 100;
    assert(netdiag_recvmsg(3, &hdr, 0, &event) == -1);
    assert(errno == EINVAL && calls == 1 && !observers);
    reset(); outcome(5, E2BIG, 0);
    assert(netdiag_recvmsg(3, &hdr, 0, NULL) == 5);
    assert(errno == E2BIG && calls == 1 && !observers);
    puts("unarmed operation-id-zero/null bypass accepts inaccessible hdr/iov/name without diagnostic reads");
}
static void test_errno_and_metadata(void)
{
    struct wine_netdiag_event event;
    struct sockaddr_in address = {0};
    struct iovec iov[2] = {{NULL, SIZE_MAX}, {NULL, 10}};
    struct msghdr hdr = {0};
    reset(); event = context();
    hdr.msg_iov = iov; hdr.msg_iovlen = 2;
    hdr.msg_name = protected_page; hdr.msg_namelen = sizeof(address);
    outcome(-1, EWOULDBLOCK, 0);
    assert(netdiag_recvmsg(3, &hdr, 0, &event) == -1);
    assert(errno == EWOULDBLOCK && observers >= 2);
    assert(records[0].native_errno == EWOULDBLOCK && records[0].returned == -1);
    assert(records[0].requested == UINT64_MAX && (records[0].flags & ND_ENDPOINT_UNKNOWN));
    reset(); event = context();
    address.sin_family = AF_INET; address.sin_port = htons(27015);
    address.sin_addr.s_addr = htonl(0x7f000001);
    hdr.msg_name = &address; hdr.msg_namelen = sizeof(address);
    iov[0].iov_base = protected_page; iov[0].iov_len = 7; hdr.msg_iovlen = 1;
    outcome(7, E2BIG, 0);
    assert(netdiag_recvmsg(3, &hdr, 0, &event) == 7 && errno == E2BIG);
    assert(records[0].requested == 7 && records[0].returned == 7 && !records[0].native_errno);
    assert(records[0].family == WS_AF_INET && records[0].port == 27015);
    assert(!(records[0].flags & ND_ENDPOINT_UNKNOWN) && records[0].address[0] == 127);
    puts("raw syscall errno survives deliberately poisoned clocks/emission; metadata saturation and endpoint capture read no payload");
}
static void test_writewatch(void)
{
    struct wine_netdiag_event event;
    struct iovec iov = {protected_page, 17};
    struct msghdr hdr = {0};
    reset(); event = context(); hdr.msg_iov = &iov; hdr.msg_iovlen = 1;
    outcome(-1, EFAULT, 0); outcome(17, E2BIG, 0);
    assert(virtual_locked_recvmsg(3, &hdr, 0, &event) == 17 && errno == E2BIG);
    assert(calls == 2 && entered == 1 && left == 1 && checked == 1 && updated == 1);
    assert(wine_netdiag_mapping->reserved == 2);
    assert(records[0].phase == ND_ATTEMPT && records[0].native_errno == EFAULT);
    assert(records[1].phase == ND_ATTEMPT && records[1].returned == 17);
    assert(records[0].operation_id == records[1].operation_id && records[0].sequence == 1 && records[1].sequence == 2);
    puts("shipping virtual write-watch EFAULT retry runs both actual hooks and real mapped logger commits both correlated attempts");
}
static struct async_recv_ioctl recv_context(void)
{
    struct async_recv_ioctl async = {0};
    async.netdiag = context(); async.count = 1;
    async.iov[0].iov_base = protected_page; async.iov[0].iov_len = 11;
    return async;
}
static void test_async(void)
{
    struct async_recv_ioctl recv;
    struct async_send_ioctl send = {0};
    ULONG_PTR info;
    unsigned int status;
    reset(); recv = recv_context(); info = 0; status = STATUS_ALERTED; close_needed = 1;
    outcome(11, E2BIG, MSG_TRUNC);
    assert(async_recv_proc(&recv, &info, &status));
    assert(status == (unsigned int)STATUS_BUFFER_OVERFLOW && info == 11 && releases == 1 && closes == 1);
    assert(records[1].phase == ND_COMPLETE && records[1].returned == 11);
    reset(); recv = recv_context(); info = 999; status = STATUS_CANCELLED;
    assert(async_recv_proc(&recv, &info, &status) && releases == 1);
    assert(records[0].phase == ND_COMPLETE && records[0].returned == 0);
    reset(); recv = recv_context(); info = 999; status = STATUS_ALERTED;
    outcome(-1, EWOULDBLOCK, 0);
    assert(!async_recv_proc(&recv, &info, &status) && !releases);
    assert(status == (unsigned int)STATUS_DEVICE_NOT_READY && wine_netdiag_mapping->reserved == 1);
    reset(); recv = recv_context(); info = 999; status = STATUS_ALERTED;
    outcome(-1, ECONNRESET, 0);
    assert(async_recv_proc(&recv, &info, &status) && releases == 1);
    assert(status == (unsigned int)STATUS_UNSUCCESSFUL && records[1].returned == 0);
    assert(wine_netdiag_mapping->reserved == 2 && records[1].phase == ND_COMPLETE);
    reset(); send.netdiag = context(); send.fd = 12; send.sent_len = 9; info = 0; status = STATUS_CANCELLED;
    assert(async_send_proc(&send, &info, &status) && releases == 1 && closes == 1 && info == 9);
    assert(records[0].returned == 9 && records[0].status == (unsigned int)STATUS_CANCELLED);
    reset(); send.netdiag = context(); send.fd = 12; send.count = 1;
    send.sent_len = 0; send.iov[0].iov_base = protected_page; send.iov[0].iov_len = 9;
    info = 999; status = STATUS_ALERTED; outcome(-1, ECONNRESET, 0);
    assert(async_send_proc(&send, &info, &status) && releases == 1 && closes == 1);
    assert(status == (unsigned int)STATUS_UNSUCCESSFUL && info == 0 && records[1].returned == 0);
    assert(wine_netdiag_mapping->reserved == 2 && records[1].phase == ND_COMPLETE);
    puts("shipping async callbacks retain truncation bytes, defer EWOULDBLOCK, and release on cancellation/syscall error");
}
static void test_recv_fd_error(void)
{
    struct async_recv_ioctl recv;
    ULONG_PTR info = 999;
    unsigned int status = STATUS_ALERTED;
    reset(); recv = recv_context(); fd_error = STATUS_INVALID_HANDLE;
    assert(async_recv_proc(&recv, &info, &status) && releases == 0);
    assert(status == (unsigned int)STATUS_INVALID_HANDLE && records[0].returned == 0);
    assert(wine_netdiag_mapping->reserved == 1 && records[0].phase == ND_COMPLETE);
    assert(records[0].status == (unsigned int)STATUS_INVALID_HANDLE);
    puts("shipping async recv fd lookup error emits COMPLETE and preserves baseline release count zero (preexisting leak)");
}
static void test_send_fd_error(void)
{
    struct async_send_ioctl send = {0};
    ULONG_PTR info = 999;
    unsigned int status = STATUS_ALERTED;
    reset(); send.netdiag = context(); send.fd = -1; fd_error = STATUS_INVALID_HANDLE;
    assert(async_send_proc(&send, &info, &status) && releases == 0);
    assert(records[0].returned == 0 && records[0].status == (unsigned int)STATUS_INVALID_HANDLE);
    assert(wine_netdiag_mapping->reserved == 1 && records[0].phase == ND_COMPLETE);
    puts("shipping async send fd lookup error emits COMPLETE and preserves baseline release count zero (preexisting leak)");
}
static void test_send_errno(void)
{
    struct async_send_ioctl async = {0};
    reset(); async.netdiag = context(); async.count = 1;
    async.iov[0].iov_base = protected_page; async.iov[0].iov_len = 5;
    outcome(-1, EINTR, 0); outcome(5, E2BIG, 0);
    assert(try_send(3, &async) == STATUS_SUCCESS && async.sent_len == 5);
    assert(errno == E2BIG && calls == 2 && wine_netdiag_mapping->reserved == 2);
    assert(records[0].native_errno == EINTR && records[1].returned == 5);
    puts("shipping send retry preserves raw errno under observer poisoning and logs both attempts");
}
static void test_allocation_failure(void)
{
    reset();
    assert(sock_ioctl_recv(NULL, NULL, NULL, protected_page, protected_page, 3,
                           protected_page, 2, protected_page, protected_page, protected_page,
                           protected_page, 0, 0) == STATUS_NO_MEMORY);
    assert(allocations == 1 && !downstream && !observers && !wine_netdiag_mapping->reserved);
    assert(sock_ioctl_send(NULL, NULL, NULL, protected_page, protected_page, 3,
                           protected_page, 2, protected_page, 99, 0, 0) == STATUS_NO_MEMORY);
    assert(allocations == 2 && !downstream && !observers && !wine_netdiag_mapping->reserved);
    puts("shipping recv/send allocation failure returns NO_MEMORY with inaccessible caller buffers and no logger/downstream access");
}
int main(int argc, char **argv)
{
    static const struct { const char *name; void (*run)(void); } tests[] = {
        {"unarmed", test_unarmed}, {"errno", test_errno_and_metadata},
        {"writewatch", test_writewatch}, {"async", test_async},
        {"send_errno", test_send_errno}, {"allocation_failure", test_allocation_failure},
        {"recv_fd_error", test_recv_fd_error}, {"send_fd_error", test_send_fd_error}
    };
    size_t i;
    int ran = 0;
    page_size = (size_t)sysconf(_SC_PAGESIZE);
    protected_page = mmap(NULL, page_size, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(protected_page != MAP_FAILED);
    wine_netdiag_mapping = mmap(NULL, WINE_NETDIAG_FILE_SIZE, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(wine_netdiag_mapping != MAP_FAILED);
    wine_netdiag_initializing = 2;
    wine_netdiag_mapping->host_pid = getpid();
    records = (struct wine_netdiag_event *)((char *)wine_netdiag_mapping + WINE_NETDIAG_MAP_HEADER);
    assert(argc <= 2);
    for (i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i)
        if (argc == 1 || !strcmp(argv[1], tests[i].name)) { tests[i].run(); ++ran; }
    assert(ran);
    assert(!munmap(protected_page, page_size));
    assert(!munmap(wine_netdiag_mapping, WINE_NETDIAG_FILE_SIZE));
    return 0;
}
