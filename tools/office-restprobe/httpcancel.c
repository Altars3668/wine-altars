#define WIN32_LEAN_AND_MEAN
#include "probe.h"
#include <winsock2.h>
#include <winhttp.h>

static HANDLE received, release_server;
static SOCKET listener;
static HINTERNET request;
static BOOL response_result;
static DWORD response_error;

static DWORD WINAPI server_thread(void *unused)
{
    SOCKET client;
    char buffer[4096];
    (void)unused;
    client = accept(listener, NULL, NULL);
    if (client != INVALID_SOCKET)
    {
        if (recv(client, buffer, sizeof(buffer), 0) > 0) SetEvent(received);
        WaitForSingleObject(release_server, 5000);
        closesocket(client);
    }
    return 0;
}

static DWORD WINAPI request_thread(void *unused)
{
    HINTERNET handle = request;
    (void)unused;
    response_result = WinHttpSendRequest(handle, NULL, 0, NULL, 0, 0, 0);
    if (response_result) response_result = WinHttpReceiveResponse(handle, NULL);
    response_error = GetLastError();
    return 0;
}

int main(void)
{
    struct sockaddr_in address = {0};
    HINTERNET session, connect;
    WSADATA data;
    HANDLE server, reader;
    int size = sizeof(address);
    DWORD wait;
    BOOL closed;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (!probe_start()) return 1;
    if (WSAStartup(MAKEWORD(2,2), &data)) return probe_done(1);
    listener = socket(AF_INET, SOCK_STREAM, 0);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (listener == INVALID_SOCKET || bind(listener, (struct sockaddr *)&address, sizeof(address)) ||
        listen(listener, 1) || getsockname(listener, (struct sockaddr *)&address, &size)) return probe_done(1);
    received = CreateEventW(NULL, TRUE, FALSE, NULL);
    release_server = CreateEventW(NULL, TRUE, FALSE, NULL);
    server = CreateThread(NULL, 0, server_thread, NULL, 0, NULL);
    session = WinHttpOpen(L"Wine Altars local cancellation probe", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
    connect = session ? WinHttpConnect(session, L"127.0.0.1", ntohs(address.sin_port), 0) : NULL;
    request = connect ? WinHttpOpenRequest(connect, L"GET", L"/", NULL, NULL, NULL, 0) : NULL;
    if (!received || !release_server || !server || !request) return probe_done(1);
    reader = CreateThread(NULL, 0, request_thread, NULL, 0, NULL);
    if (!reader) return probe_done(1);
    wait = WaitForSingleObject(received, 3000);
    printf("server_reached=%u wait=%lu\n", wait == WAIT_OBJECT_0, wait);
    closed = WinHttpCloseHandle(request);
    wait = WaitForSingleObject(reader, 2000);
    printf("close_ok=%u response_completed_before_server_release=%u\n", closed, wait == WAIT_OBJECT_0);
    SetEvent(release_server);
    if (WaitForSingleObject(reader, 5000) != WAIT_OBJECT_0) return probe_done(1);
    printf("response_ok=%u response_error=%lu\n", response_result, response_error);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    WaitForSingleObject(server, 2000);
    CloseHandle(reader);
    CloseHandle(server);
    CloseHandle(received);
    CloseHandle(release_server);
    closesocket(listener);
    WSACleanup();
    return probe_done(0);
}
