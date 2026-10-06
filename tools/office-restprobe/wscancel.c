#define WIN32_LEAN_AND_MEAN
#include "probe.h"
#include <winsock2.h>
#include <webservices.h>

static HANDLE received, release_server;
static SOCKET listener;
static WS_SERVICE_PROXY *proxy;
static WS_OPERATION_DESCRIPTION operation;
static WS_HEAP *heap;
static HRESULT call_result, abort_result;

static DWORD WINAPI server_thread(void *unused)
{
    SOCKET client;
    char data[4096];
    (void)unused;
    client = accept(listener, NULL, NULL);
    if (client != INVALID_SOCKET)
    {
        if (recv(client, data, sizeof(data), 0) > 0) SetEvent(received);
        WaitForSingleObject(release_server, 5000);
        closesocket(client);
    }
    return 0;
}

static DWORD WINAPI call_thread(void *unused)
{
    (void)unused;
    call_result = WsCall(proxy, &operation, NULL, heap, NULL, 0, NULL, NULL);
    return 0;
}

static DWORD WINAPI abort_thread(void *unused)
{
    (void)unused;
    abort_result = WsAbortServiceProxy(proxy, NULL);
    return 0;
}

int main(void)
{
    WS_XML_STRING req = {3, (BYTE *)"req"}, resp = {4, (BYTE *)"resp"}, ns = {2, (BYTE *)"ns"};
    WS_STRUCT_DESCRIPTION structure = {0};
    WS_ELEMENT_DESCRIPTION input = {0}, output = {0};
    WS_MESSAGE_DESCRIPTION input_msg = {0}, output_msg = {0};
    WS_CHANNEL_PROPERTY properties[2];
    WS_ENVELOPE_VERSION envelope = WS_ENVELOPE_VERSION_SOAP_1_1;
    WS_ADDRESSING_VERSION addressing = WS_ADDRESSING_VERSION_TRANSPORT;
    WS_ENDPOINT_ADDRESS address = {0};
    WS_SERVICE_PROXY_STATE state;
    struct sockaddr_in addr = {0};
    WSADATA data;
    HANDLE server, call, aborter;
    WCHAR url[96];
    int len = sizeof(addr);
    DWORD wait, begin;
    HRESULT hr;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (!probe_start()) return 1;
    if (WSAStartup(MAKEWORD(2,2), &data)) return probe_done(1);
    /* 仅本进程的临时 loopback 监听器，不接触其他服务或系统配置。 */
    listener = socket(AF_INET, SOCK_STREAM, 0);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (listener == INVALID_SOCKET || bind(listener, (struct sockaddr *)&addr, sizeof(addr)) ||
        listen(listener, 1) || getsockname(listener, (struct sockaddr *)&addr, &len)) return probe_done(1);
    received = CreateEventW(NULL, TRUE, FALSE, NULL);
    release_server = CreateEventW(NULL, TRUE, FALSE, NULL);
    server = CreateThread(NULL, 0, server_thread, NULL, 0, NULL);
    if (!received || !release_server || !server) return probe_done(1);
    properties[0].id = WS_CHANNEL_PROPERTY_ENVELOPE_VERSION;
    properties[0].value = &envelope;
    properties[0].valueSize = sizeof(envelope);
    properties[1].id = WS_CHANNEL_PROPERTY_ADDRESSING_VERSION;
    properties[1].value = &addressing;
    properties[1].valueSize = sizeof(addressing);
    hr = WsCreateServiceProxy(WS_CHANNEL_TYPE_REQUEST, WS_HTTP_CHANNEL_BINDING, NULL, NULL, 0,
                               properties, 2, &proxy, NULL);
    if (FAILED(hr)) return probe_done(1);
    swprintf(url, ARRAYSIZE(url), L"http://127.0.0.1:%u/", ntohs(addr.sin_port));
    address.url.chars = url;
    address.url.length = wcslen(url);
    if (FAILED(WsOpenServiceProxy(proxy, &address, NULL, NULL)) ||
        FAILED(WsCreateHeap(65536, 0, NULL, 0, &heap, NULL))) return probe_done(1);
    structure.size = 0;
    structure.alignment = 1;
    structure.typeLocalName = &req;
    structure.typeNs = &ns;
    input.elementLocalName = &req;
    input.elementNs = &ns;
    input.type = WS_STRUCT_TYPE;
    input.typeDescription = &structure;
    output = input;
    output.elementLocalName = &resp;
    input_msg.action = &req;
    input_msg.bodyElementDescription = &input;
    output_msg.action = &resp;
    output_msg.bodyElementDescription = &output;
    operation.versionInfo = 1;
    operation.inputMessageDescription = &input_msg;
    operation.outputMessageDescription = &output_msg;
    call = CreateThread(NULL, 0, call_thread, NULL, 0, NULL);
    if (!call) return probe_done(1);
    wait = WaitForSingleObject(received, 3000);
    printf("server_reached=%u wait=%lu\n", wait == WAIT_OBJECT_0, wait);
    begin = GetTickCount();
    aborter = CreateThread(NULL, 0, abort_thread, NULL, 0, NULL);
    if (!aborter) return probe_done(1);
    wait = WaitForSingleObject(aborter, 2000);
    printf("abort_completed_before_server_release=%u elapsed_ms=%lu\n", wait == WAIT_OBJECT_0, GetTickCount() - begin);
    SetEvent(release_server);
    if (WaitForSingleObject(call, 5000) != WAIT_OBJECT_0 ||
        WaitForSingleObject(aborter, 5000) != WAIT_OBJECT_0) return probe_done(1);
    state = 0xdeadbeef;
    hr = WsGetServiceProxyProperty(proxy, WS_PROXY_PROPERTY_STATE, &state, sizeof(state), NULL);
    printf("abort_result=%#lx call_result=%#lx state_query=%#lx state=%u\n", abort_result, call_result, hr, state);
    WsCloseServiceProxy(proxy, NULL, NULL);
    WsFreeServiceProxy(proxy);
    WsFreeHeap(heap);
    WaitForSingleObject(server, 2000);
    CloseHandle(server);
    CloseHandle(call);
    CloseHandle(aborter);
    CloseHandle(received);
    CloseHandle(release_server);
    closesocket(listener);
    WSACleanup();
    return probe_done(0);
}
