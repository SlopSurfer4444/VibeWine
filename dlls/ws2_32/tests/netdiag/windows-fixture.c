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

static int check(int condition, int error)
{
    ++assertions;
    if (!condition)
    {
        ++failures;
        printf("assertion=%u error=%d\n", assertions, error);
    }
    return condition;
}

/* Capture after the API expression, independent of C argument order. */
static int check_api(int condition)
{
    return check(condition, WSAGetLastError());
}

static DWORD wait_limit(void)
{
    DWORD elapsed = GetTickCount() - started;
    if (elapsed >= 9000) return 0;
    return 9000 - elapsed < 2000 ? 9000 - elapsed : 2000;
}

static int wait_event(WSAEVENT event)
{
    DWORD ret = WaitForSingleObject(event, wait_limit());
    return check(ret == WAIT_OBJECT_0, ret == WAIT_FAILED ? GetLastError() : (int)ret);
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
            if (!wait_event(event))
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
    /* For refusal the bound, non-listening socket reserves an unused local port. */
    client = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (!check_api(client != INVALID_SOCKET)) goto done;
    event = WSACreateEvent();
    if (!check_api(event != WSA_INVALID_EVENT)) goto done;
    if (!check_api(WSAEventSelect(client, event, FD_CONNECT) == 0)) goto done;
    ret = connect(client, (struct sockaddr *)&address, sizeof(address));
    error = ret == SOCKET_ERROR ? WSAGetLastError() : 0;
    if (!check(ret == 0 || (ret == SOCKET_ERROR && (error == WSAEWOULDBLOCK
               || error == WSAEINPROGRESS || (refused && error == WSAECONNREFUSED))), error)) goto done;
    if (ret == SOCKET_ERROR && error == WSAECONNREFUSED)
        check(refused, error);
    else
    {
        if (!wait_event(event)) goto done;
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
    if (!wait_event(event)) goto done;
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
        if (!wait_event(event))
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
        tcp_connect(0);
        tcp_connect(1);
        udp_overlapped();
        check_api(WSACleanup() == 0);
    }
    check(GetTickCount() - started < 10000, WSAETIMEDOUT);
    printf("assertions=%u failures=%u error=%d\n", assertions, failures, failures ? 1 : 0);
    return failures ? 1 : 0;
}
