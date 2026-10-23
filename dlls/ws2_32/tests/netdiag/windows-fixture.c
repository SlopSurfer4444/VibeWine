/* Console-only, loopback-only API fixture. LGPL-2.1-or-later.
 * Compile as a PE console executable and link ws2_32. No HWND, external names,
 * external addresses, credentials or payload output. Every socket is local.
 * Individual waits are at most 2 seconds, with a shared 9-second wait budget.
 */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

/* Exported by ws2_32; omitted from some LLVM-MinGW header versions. */
INT WSAAPI GetAddrInfoExOverlappedResult(LPOVERLAPPED);

static unsigned int assertions, failures;
static DWORD started;

static int check_at(int condition, int error, const char *stage, unsigned int line)
{
    ++assertions;
    if (!condition)
    {
        ++failures;
        printf("assertion=%u error=%d stage=%s line=%u\n", assertions, error, stage, line);
    }
    return condition;
}

#define check(condition, error) check_at((condition), (error), __func__, __LINE__)

/* Capture after the API expression, independent of C argument order. */
static int check_api_at(int condition, const char *stage, unsigned int line)
{
    return check_at(condition, WSAGetLastError(), stage, line);
}
#define check_api(condition) check_api_at((condition), __func__, __LINE__)

static DWORD wait_limit(void)
{
    DWORD elapsed = GetTickCount() - started;
    if (elapsed >= 9000) return 0;
    return 9000 - elapsed < 2000 ? 9000 - elapsed : 2000;
}

static int wait_event(WSAEVENT event, const char *label)
{
    DWORD ret = WaitForSingleObject(event, wait_limit());
    return check_at(ret == WAIT_OBJECT_0, ret == WAIT_FAILED ? GetLastError() : (int)ret, label, __LINE__);
}

static SOCKET local_socket(int type, struct sockaddr_in *address)
{
    SOCKET socket = WSASocketW(AF_INET, type, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    int len = sizeof(*address);

    if (!check_api(socket != INVALID_SOCKET)) return INVALID_SOCKET;
    memset(address, 0, sizeof(*address));
    address->sin_family = AF_INET;
    address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (!check_api(bind(socket, (struct sockaddr *)address, sizeof(*address)) == 0)
        || !check_api(getsockname(socket, (struct sockaddr *)address, &len) == 0))
    {
        closesocket(socket);
        return INVALID_SOCKET;
    }
    return socket;
}

static void numeric_dns(void)
{
    struct addrinfo hints, *result = NULL;
    ADDRINFOEXW hints_w, *result_w = NULL;
    OVERLAPPED overlapped;
    WSAEVENT event = WSA_INVALID_EVENT;
    int ret, error;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    ret = getaddrinfo("127.0.0.1", "9", &hints, &result);
    check(ret == 0, ret);
    check(result && result->ai_family == AF_INET, ret);
    if (result) freeaddrinfo(result);
    result = NULL;
    ret = getaddrinfo(NULL, NULL, NULL, &result);
    check(ret == WSAHOST_NOT_FOUND, ret);
    check(result == NULL, ret);

    event = WSACreateEvent();
    if (!check_api(event != WSA_INVALID_EVENT)) return;
    memset(&hints_w, 0, sizeof(hints_w));
    memset(&overlapped, 0, sizeof(overlapped));
    hints_w.ai_family = AF_INET;
    hints_w.ai_socktype = SOCK_STREAM;
    hints_w.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    overlapped.hEvent = event;
    ret = GetAddrInfoExW(L"127.0.0.1", L"9", NS_DNS, NULL, &hints_w, &result_w,
                         NULL, &overlapped, NULL, NULL);
    error = WSAGetLastError();
    if (check(ret == 0 || ret == WSA_IO_PENDING, ret ? ret : error))
    {
        if (ret == WSA_IO_PENDING)
        {
            if (!wait_event(event, "numeric_dns_async"))
            {
                /* A still-active resolver owns these stack outputs. Exit the
                 * process immediately rather than returning and invalidating them. */
                printf("assertions=%u failures=%u error=%d\n", assertions, failures, WSAETIMEDOUT);
                ExitProcess(1);
            }
            ret = GetAddrInfoExOverlappedResult(&overlapped);
        }
        check(ret == 0, ret);
        check(result_w && result_w->ai_family == AF_INET, ret);
    }
    if (result_w) FreeAddrInfoExW(result_w);
    WSACloseEvent(event);
}

/* Observe readiness without consuming a connect error through SO_ERROR or an
 * FD_CONNECT event. A zero timeout may already be ready on fast loopback. */
static void tcp_readiness(SOCKET socket, int refused)
{
    fd_set writes, exceptions;
    WSAPOLLFD pollfd;
    struct timeval timeout;
    DWORD limit;
    unsigned int attempt;
    int ret, error;

    for (attempt = 0; attempt < 2; ++attempt)
    {
        limit = attempt ? wait_limit() : 0;
        FD_ZERO(&writes);
        FD_ZERO(&exceptions);
        FD_SET(socket, &writes);
        FD_SET(socket, &exceptions);
        timeout.tv_sec = limit / 1000;
        timeout.tv_usec = (limit % 1000) * 1000;
        ret = select(0, NULL, &writes, &exceptions, &timeout);
        error = WSAGetLastError();
        printf("stage=tcp_%s_select_%s ret=%d error=%d write=%u except=%u\n",
               refused ? "refused" : "success", attempt ? "finite" : "zero", ret, error,
               (unsigned int)writes.fd_count, (unsigned int)exceptions.fd_count);
        if (check(ret >= 0, error))
        {
            check(error == 0, error);
            if (attempt) check(ret == 1, ret);
            if (ret)
            {
                check(FD_ISSET(socket, refused ? &exceptions : &writes), 0);
                check((refused ? writes.fd_count : exceptions.fd_count) == 0, 0);
            }
        }
        if (attempt) check(limit != 0, WSAETIMEDOUT);
    }

    for (attempt = 0; attempt < 2; ++attempt)
    {
        limit = attempt ? wait_limit() : 0;
        memset(&pollfd, 0, sizeof(pollfd));
        pollfd.fd = socket;
        pollfd.events = POLLWRNORM;
        ret = WSAPoll(&pollfd, 1, (int)limit);
        error = WSAGetLastError();
        printf("stage=tcp_%s_poll_%s ret=%d error=%d revents=%u\n",
               refused ? "refused" : "success", attempt ? "finite" : "zero", ret, error,
               (unsigned int)(unsigned short)pollfd.revents);
        if (check(ret >= 0, error))
        {
            check(error == 0, error);
            if (attempt) check(ret == 1, ret);
            if (ret) check((pollfd.revents & (refused ? POLLERR : POLLWRNORM)) != 0, 0);
            else check(pollfd.revents == 0, 0);
        }
        if (attempt) check(limit != 0, WSAETIMEDOUT);
    }
}

static void tcp_connect(int refused)
{
    struct sockaddr_in address;
    SOCKET server = INVALID_SOCKET, client = INVALID_SOCKET, accepted = INVALID_SOCKET;
    WSAEVENT event = WSA_INVALID_EVENT;
    WSANETWORKEVENTS events;
    u_long nonblocking = 1;
    int ret, error, socket_error, len;

    server = local_socket(SOCK_STREAM, &address);
    if (server == INVALID_SOCKET) goto done;
    if (!refused && !check_api(listen(server, 1) == 0)) goto done;
    /* Reserve a local port, then close it immediately before the refusal
     * attempt. On macOS a bound but non-listening socket can silently drop SYN
     * instead of refusing it (observed with tcp.blackhole=2). */
    client = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (!check_api(client != INVALID_SOCKET)) goto done;
    event = WSACreateEvent();
    if (!check_api(event != WSA_INVALID_EVENT)) goto done;
    if (!check_api(WSAEventSelect(client, event, FD_CONNECT) == 0)) goto done;
    if (refused)
    {
        if (!check_api(closesocket(server) == 0)) goto done;
        server = INVALID_SOCKET;
    }
    ret = connect(client, (struct sockaddr *)&address, sizeof(address));
    error = ret == SOCKET_ERROR ? WSAGetLastError() : 0;
    printf("stage=%s connect_return=%d connect_error=%d\n", refused ? "tcp_refused" : "tcp_success", ret, error);
    if (!check(ret == 0 || (ret == SOCKET_ERROR && (error == WSAEWOULDBLOCK
               || error == WSAEINPROGRESS || (refused && error == WSAECONNREFUSED))), error)) goto done;
    if (ret != SOCKET_ERROR || error != WSAECONNREFUSED)
        tcp_readiness(client, refused);
    if (ret == SOCKET_ERROR && error == WSAECONNREFUSED)
        check(refused, error);
    else
    {
        if (!wait_event(event, refused ? "tcp_refused_fd_connect" : "tcp_success_fd_connect"))
        {
            /* Keep the failed wait as a failure. Observe this socket only after
             * failure to distinguish missing event delivery from pending/error. */
            memset(&events, 0, sizeof(events));
            ret = WSAEnumNetworkEvents(client, event, &events);
            error = ret == SOCKET_ERROR ? WSAGetLastError() : 0;
            printf("stage=%s after_timeout_enum_return=%d enum_error=%d events=%ld fd_connect_error=%d\n",
                   refused ? "tcp_refused" : "tcp_success", ret, error,
                   events.lNetworkEvents, events.iErrorCode[FD_CONNECT_BIT]);
            socket_error = -1;
            len = sizeof(socket_error);
            ret = getsockopt(client, SOL_SOCKET, SO_ERROR, (char *)&socket_error, &len);
            error = ret == SOCKET_ERROR ? WSAGetLastError() : 0;
            printf("stage=%s after_timeout_so_error_return=%d api_error=%d socket_error=%d\n",
                   refused ? "tcp_refused" : "tcp_success", ret, error, socket_error);
            goto done;
        }
        memset(&events, 0, sizeof(events));
        if (!check_api(WSAEnumNetworkEvents(client, event, &events) == 0)) goto done;
        check((events.lNetworkEvents & FD_CONNECT) != 0, 0);
        check(events.iErrorCode[FD_CONNECT_BIT] == (refused ? WSAECONNREFUSED : 0),
              events.iErrorCode[FD_CONNECT_BIT]);
    }
    socket_error = -1;
    len = sizeof(socket_error);
    if (check_api(getsockopt(client, SOL_SOCKET, SO_ERROR, (char *)&socket_error, &len) == 0))
        check(socket_error == 0 || (refused && socket_error == WSAECONNREFUSED), socket_error);
    if (!refused)
    {
        if (!check_api(ioctlsocket(server, FIONBIO, &nonblocking) == 0)) goto done;
        accepted = accept(server, NULL, NULL);
        check_api(accepted != INVALID_SOCKET);
    }
done:
    if (accepted != INVALID_SOCKET) closesocket(accepted);
    if (client != INVALID_SOCKET) closesocket(client);
    if (server != INVALID_SOCKET) closesocket(server);
    if (event != WSA_INVALID_EVENT) WSACloseEvent(event);
}

static void readiness_validation(void)
{
    struct timeval zero = {0, 0};
    fd_set bad;
    WSAPOLLFD pollfd;
    int ret, error;

    WSASetLastError(0x1234);
    ret = select(0, NULL, NULL, NULL, NULL);
    error = WSAGetLastError();
    check(ret == SOCKET_ERROR && error == WSAEINVAL, error);
    /* The original empty-set validation returns before touching timeout. */
    WSASetLastError(0x1234);
    ret = select(0, NULL, NULL, NULL, (const struct timeval *)(ULONG_PTR)1);
    error = WSAGetLastError();
    check(ret == SOCKET_ERROR && error == WSAEINVAL, error);
    FD_ZERO(&bad);
    FD_SET(INVALID_SOCKET, &bad);
    WSASetLastError(0x1234);
    ret = select(0, &bad, NULL, NULL, &zero);
    error = WSAGetLastError();
    check(ret == SOCKET_ERROR && error == WSAENOTSOCK, error);

    WSASetLastError(0x1234);
    ret = WSAPoll(NULL, 0, 0);
    error = WSAGetLastError();
    check(ret == SOCKET_ERROR && error == WSAEINVAL, error);
    /* A zero count likewise validates before reading the array pointer. */
    WSASetLastError(0x1234);
    ret = WSAPoll((WSAPOLLFD *)(ULONG_PTR)1, 0, 0);
    error = WSAGetLastError();
    check(ret == SOCKET_ERROR && error == WSAEINVAL, error);
    WSASetLastError(0x1234);
    ret = WSAPoll(NULL, 1, 0);
    error = WSAGetLastError();
    check(ret == SOCKET_ERROR && error == WSAEFAULT, error);
    memset(&pollfd, 0, sizeof(pollfd));
    pollfd.fd = INVALID_SOCKET;
    pollfd.events = POLLRDNORM;
    WSASetLastError(0x1234);
    ret = WSAPoll(&pollfd, 1, 0);
    error = WSAGetLastError();
    check(ret == SOCKET_ERROR && error == WSAENOTSOCK, error);
    check(pollfd.revents == POLLNVAL, 0);
}

/* A separate loopback UDP pair makes empty/ready results deterministic and
 * leaves the original overlapped receive and its payload checks intact. */
static void readiness_datagram(void)
{
    struct sockaddr_in receiver_address, sender_address;
    SOCKET receiver = INVALID_SOCKET, sender = INVALID_SOCKET;
    struct timeval timeout;
    WSAPOLLFD pollfd[2];
    fd_set reads, shared;
    u_long nonblocking = 1;
    char byte = 'r', received = 0;
    DWORD limit;
    unsigned int attempt;
    int ret, error;

    receiver = local_socket(SOCK_DGRAM, &receiver_address);
    sender = local_socket(SOCK_DGRAM, &sender_address);
    if (receiver == INVALID_SOCKET || sender == INVALID_SOCKET) goto done;
    if (!check_api(ioctlsocket(receiver, FIONBIO, &nonblocking) == 0)
        || !check_api(ioctlsocket(sender, FIONBIO, &nonblocking) == 0)) goto done;

    for (attempt = 0; attempt < 2; ++attempt)
    {
        limit = attempt ? wait_limit() : 0;
        if (limit > 25) limit = 25;
        if (attempt) check(limit != 0, WSAETIMEDOUT);
        FD_ZERO(&reads);
        FD_SET(receiver, &reads);
        timeout.tv_sec = 0;
        timeout.tv_usec = limit * 1000;
        ret = select(0, &reads, NULL, NULL, &timeout);
        error = WSAGetLastError();
        printf("stage=udp_empty_select_%s ret=%d error=%d count=%u\n",
               attempt ? "finite" : "zero", ret, error, (unsigned int)reads.fd_count);
        check(ret == 0 && error == 0, error);
        check(reads.fd_count == 0, 0);

        limit = attempt ? wait_limit() : 0;
        if (limit > 25) limit = 25;
        if (attempt) check(limit != 0, WSAETIMEDOUT);
        memset(pollfd, 0, sizeof(pollfd));
        pollfd[0].fd = receiver;
        pollfd[0].events = POLLRDNORM;
        ret = WSAPoll(pollfd, 1, (int)limit);
        error = WSAGetLastError();
        printf("stage=udp_empty_poll_%s ret=%d error=%d revents=%u\n",
               attempt ? "finite" : "zero", ret, error,
               (unsigned int)(unsigned short)pollfd[0].revents);
        check(ret == 0 && error == 0, error);
        check(pollfd[0].revents == 0, 0);
    }

    /* Windows permits aliasing read/write fd_set pointers. The output is the
     * union, with each socket counted once; the empty UDP socket is writable. */
    FD_ZERO(&shared);
    FD_SET(sender, &shared);
    timeout.tv_sec = timeout.tv_usec = 0;
    ret = select(0, &shared, &shared, NULL, &timeout);
    error = WSAGetLastError();
    check(ret == 1 && error == 0 && shared.fd_count == 1 && FD_ISSET(sender, &shared), error);

    /* An invalid member retains POLLNVAL while a valid empty member times out;
     * existing Wine/Windows semantics do not count the invalid entry in ret. */
    memset(pollfd, 0, sizeof(pollfd));
    pollfd[0].fd = receiver;
    pollfd[0].events = POLLRDNORM;
    pollfd[1].fd = INVALID_SOCKET;
    pollfd[1].events = POLLRDNORM;
    ret = WSAPoll(pollfd, 2, 0);
    error = WSAGetLastError();
    check(ret == 0 && error == 0, error);
    check(pollfd[0].revents == 0 && pollfd[1].revents == POLLNVAL, 0);

    ret = sendto(sender, &byte, 1, 0, (struct sockaddr *)&receiver_address, sizeof(receiver_address));
    if (!check(ret == 1, WSAGetLastError())) goto done;
    FD_ZERO(&reads);
    FD_SET(receiver, &reads);
    limit = wait_limit();
    timeout.tv_sec = limit / 1000;
    timeout.tv_usec = (limit % 1000) * 1000;
    ret = select(0, &reads, NULL, NULL, &timeout);
    error = WSAGetLastError();
    check(ret == 1 && error == 0 && reads.fd_count == 1 && FD_ISSET(receiver, &reads), error);
    memset(pollfd, 0, sizeof(pollfd));
    pollfd[0].fd = receiver;
    pollfd[0].events = POLLRDNORM;
    ret = WSAPoll(pollfd, 1, (int)wait_limit());
    error = WSAGetLastError();
    check(ret == 1 && error == 0 && (pollfd[0].revents & POLLRDNORM), error);
    /* Neither readiness call consumes the queued datagram. */
    ret = recv(receiver, &received, 1, 0);
    error = WSAGetLastError();
    check(ret == 1 && received == byte, error);
done:
    if (receiver != INVALID_SOCKET) closesocket(receiver);
    if (sender != INVALID_SOCKET) closesocket(sender);
}

static void readiness_capacity(void)
{
    /* Seventeen distinct sockets cross the diagnostic member limit of sixteen
     * without relying on duplicate-descriptor counting behavior. */
    struct sockaddr_in address;
    SOCKET sockets[17];
    WSAPOLLFD pollfd[17];
    fd_set reads;
    struct timeval zero = {0, 0};
    unsigned int i;
    int ret, error;

    for (i = 0; i < 17; ++i) sockets[i] = INVALID_SOCKET;
    memset(pollfd, 0, sizeof(pollfd));
    FD_ZERO(&reads);
    for (i = 0; i < 17; ++i)
    {
        sockets[i] = local_socket(SOCK_DGRAM, &address);
        if (sockets[i] == INVALID_SOCKET) goto done;
        FD_SET(sockets[i], &reads);
        pollfd[i].fd = sockets[i];
        pollfd[i].events = POLLRDNORM;
    }
    ret = select(0, &reads, NULL, NULL, &zero);
    error = WSAGetLastError();
    printf("stage=capacity17_select ret=%d error=%d count=%u\n",
           ret, error, (unsigned int)reads.fd_count);
    check(ret == 0 && error == 0 && reads.fd_count == 0, error);
    ret = WSAPoll(pollfd, 17, 0);
    error = WSAGetLastError();
    printf("stage=capacity17_poll ret=%d error=%d\n", ret, error);
    check(ret == 0 && error == 0, error);
    for (i = 0; i < 17; ++i) check(pollfd[i].revents == 0, 0);
done:
    for (i = 0; i < 17; ++i)
        if (sockets[i] != INVALID_SOCKET) closesocket(sockets[i]);
}

static void udp_overlapped(void)
{
    static const char sentinel[] = "VIBEWINE_PAYLOAD_MUST_NOT_APPEAR_7e2d9173";
    struct sockaddr_in receiver_address, sender_address, source;
    SOCKET receiver = INVALID_SOCKET, sender = INVALID_SOCKET;
    WSAEVENT event = WSA_INVALID_EVENT;
    OVERLAPPED overlapped;
    WSABUF buffer;
    char received[128];
    DWORD bytes = 0, flags = 0;
    int source_len = sizeof(source), ret, error;
    BOOL pending = FALSE;
    u_long nonblocking = 1;

    receiver = local_socket(SOCK_DGRAM, &receiver_address);
    sender = local_socket(SOCK_DGRAM, &sender_address);
    if (receiver == INVALID_SOCKET || sender == INVALID_SOCKET) goto done;
    if (!check_api(ioctlsocket(sender, FIONBIO, &nonblocking) == 0)) goto done;
    event = WSACreateEvent();
    if (!check_api(event != WSA_INVALID_EVENT)) goto done;
    memset(&overlapped, 0, sizeof(overlapped));
    memset(&source, 0, sizeof(source));
    overlapped.hEvent = event;
    buffer.buf = received;
    buffer.len = sizeof(received);
    ret = WSARecvFrom(receiver, &buffer, 1, &bytes, &flags, (struct sockaddr *)&source,
                      &source_len, &overlapped, NULL);
    error = WSAGetLastError();
    if (!check(ret == SOCKET_ERROR && error == WSA_IO_PENDING, error)) goto done;
    pending = TRUE;
    ret = WSAGetOverlappedResult(receiver, &overlapped, &bytes, FALSE, &flags);
    error = WSAGetLastError();
    check(!ret && error == WSA_IO_INCOMPLETE, error);
    ret = sendto(sender, sentinel, sizeof(sentinel), 0,
                 (struct sockaddr *)&receiver_address, sizeof(receiver_address));
    if (!check(ret == (int)sizeof(sentinel), ret == SOCKET_ERROR ? WSAGetLastError() : ret)) goto done;
    if (!wait_event(event, "udp_receive_completion")) goto done;
    ret = WSAGetOverlappedResult(receiver, &overlapped, &bytes, FALSE, &flags);
    if (!check_api(ret)) goto done;
    pending = FALSE;
    check(bytes == sizeof(sentinel), 0);
    check(bytes == sizeof(sentinel) && !memcmp(received, sentinel, sizeof(sentinel)), 0);
    check(source_len >= (int)sizeof(source) && source.sin_family == AF_INET
          && source.sin_addr.s_addr == sender_address.sin_addr.s_addr
          && source.sin_port == sender_address.sin_port, 0);
done:
    if (pending)
    {
        /* Keep OVERLAPPED and receive/source buffers alive through cancellation.
         * Closing a socket cancels the receive; wait remains strictly bounded. */
        closesocket(receiver);
        receiver = INVALID_SOCKET;
        if (!wait_event(event, "udp_cancel_completion"))
        {
            printf("assertions=%u failures=%u error=%d\n", assertions, failures, WSAETIMEDOUT);
            ExitProcess(1);
        }
    }
    if (receiver != INVALID_SOCKET) closesocket(receiver);
    if (sender != INVALID_SOCKET) closesocket(sender);
    if (event != WSA_INVALID_EVENT) WSACloseEvent(event);
}

int main(int argc, char **argv)
{
    WSADATA data;
    int ret, wait_arm;
    SOCKET claim;

    if (argc != 1 && (argc != 2 || strcmp(argv[1], "--wait-arm")))
    {
        printf("assertions=0 failures=1 error=%d\n", WSAEINVAL);
        return 1;
    }
    wait_arm = argc == 2;
    started = GetTickCount();
    ret = WSAStartup(MAKEWORD(2, 2), &data);
    if (check(ret == 0, ret))
    {
        if (wait_arm)
        {
            claim = WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, WSA_FLAG_OVERLAPPED);
            if (!check_api(claim != INVALID_SOCKET))
            {
                WSACleanup();
                printf("assertions=%u failures=%u error=%d\n", assertions, failures, 1);
                return 1;
            }
            closesocket(claim);
            printf("ready=1 windows_pid=%lu\n", (unsigned long)GetCurrentProcessId());
            fflush(stdout);
            Sleep(30000);
            started = GetTickCount();
        }
        numeric_dns();
        readiness_validation();
        tcp_connect(0);
        tcp_connect(1);
        readiness_datagram();
        readiness_capacity();
        udp_overlapped();
        check_api(WSACleanup() == 0);
    }
    check(GetTickCount() - started < 10000, WSAETIMEDOUT);
    printf("assertions=%u failures=%u error=%d\n", assertions, failures, failures ? 1 : 0);
    return failures ? 1 : 0;
}
