/* probe_ws: Windows.Networking.Sockets.MessageWebSocket against a web socket server in this process,
 * printing what the client sent and what it made of each frame.  Printed for diffing Windows against
 * Wine. */
#include <winsock2.h>
#define WIDL_using_Windows_Networking_Sockets
#define WIDL_using_Windows_Web
#define WIDL_using_Windows_Security_Cryptography_Certificates
#include "wr.h"
#include "wincrypt.h"
#include "windows.networking.sockets.h"
#include "windows.web.h"

static SOCKET listener;
static DWORD close_t0;
static int port;
static HANDLE server_done;
static const char *scenario;

static void print_escaped(const char *prefix, const char *data, int len)
{
    int i;
    printf("%s", prefix);
    for (i = 0; i < len; i++)
    {
        if (data[i] == '\r') printf("\\r");
        else if (data[i] == '\n') printf("\\n\n%s", i + 1 < len ? "    " : "");
        else if ((BYTE)data[i] < 0x20 || (BYTE)data[i] >= 0x7f) printf("\\x%02x", (BYTE)data[i]);
        else putchar(data[i]);
    }
    if (len && data[len - 1] != '\n') printf("\n");
}

static void send_all(SOCKET s, const void *data, int len)
{
    while (len > 0)
    {
        int ret = send(s, data, len, 0);
        if (ret <= 0) return;
        data = (const char *)data + ret;
        len -= ret;
    }
}

static int recv_all(SOCKET s, void *data, int len)
{
    int got = 0, ret;
    while (got < len)
    {
        if ((ret = recv(s, (char *)data + got, len - got, 0)) <= 0) return 0;
        got += ret;
    }
    return 1;
}

static void send_frame(SOCKET s, int fin, int opcode, const void *data, int len)
{
    BYTE head[4];
    int n = 2;
    head[0] = (fin ? 0x80 : 0) | opcode;
    if (len < 126) head[1] = len;
    else { head[1] = 126; head[2] = len >> 8; head[3] = len & 0xff; n = 4; }
    send_all(s, head, n);
    if (len) send_all(s, data, len);
}

/* one frame from the client, printed; returns the opcode, or -1 */
static int read_frame(SOCKET s, BYTE *payload, int *payload_len)
{
    BYTE head[2], ext[8], mask[4];
    UINT64 len;
    int i;

    if (!recv_all(s, head, 2)) return -1;
    len = head[1] & 0x7f;
    if (len == 126) { if (!recv_all(s, ext, 2)) return -1; len = (ext[0] << 8) | ext[1]; }
    else if (len == 127) { if (!recv_all(s, ext, 8)) return -1; len = 0; for (i = 0; i < 8; i++) len = (len << 8) | ext[i]; }
    if (head[1] & 0x80) { if (!recv_all(s, mask, 4)) return -1; }
    if (len > 4096) return -1;
    if (!recv_all(s, payload, (int)len)) return -1;
    if (head[1] & 0x80) for (i = 0; i < len; i++) payload[i] ^= mask[i % 4];
    *payload_len = (int)len;
    printf("SERVER frame: fin %d rsv %d opcode %d masked %d len %d:", head[0] >> 7, (head[0] >> 4) & 7, head[0] & 0xf,
           head[1] >> 7, (int)len);
    for (i = 0; i < len && i < 64; i++)
        if (payload[i] >= 0x20 && payload[i] < 0x7f) printf("%c", payload[i]); else printf("\\x%02x", payload[i]);
    printf("\n");
    return head[0] & 0xf;
}

static void accept_key(const char *key, char *out)
{
    static const char guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char buffer[128];
    HCRYPTPROV prov;
    HCRYPTHASH hash;
    BYTE digest[20];
    DWORD len = sizeof(digest), out_len = 64;

    sprintf(buffer, "%s%s", key, guid);
    CryptAcquireContextA(&prov, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT);
    CryptCreateHash(prov, CALG_SHA1, 0, 0, &hash);
    CryptHashData(hash, (BYTE *)buffer, strlen(buffer), 0);
    CryptGetHashParam(hash, HP_HASHVAL, digest, &len, 0);
    CryptDestroyHash(hash);
    CryptReleaseContext(prov, 0);
    CryptBinaryToStringA(digest, 20, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, out, &out_len);
}

static DWORD WINAPI server_thread(void *arg)
{
    for (;;)
    {
        char buf[4096], head[1024], accept_value[64], *key, *end, *proto;
        BYTE payload[4096];
        int len = 0, ret, plen, opcode;
        SOCKET s = accept(listener, NULL, NULL);

        if (s == INVALID_SOCKET) return 0;
        for (;;)
        {
            ret = recv(s, buf + len, sizeof(buf) - 1 - len, 0);
            if (ret <= 0) break;
            len += ret;
            buf[len] = 0;
            if (strstr(buf, "\r\n\r\n")) break;
        }
        if (!strstr(buf, "GET /codes") && !strstr(buf, "GET /recv")) print_escaped("SERVER got: ", buf, len);
        if (strstr(buf, "GET /notfound"))
        {
            static const char msg[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
            send_all(s, msg, sizeof(msg) - 1);
            closesocket(s);
            SetEvent(server_done);
            continue;
        }
        key = strstr(buf, "Sec-WebSocket-Key: ");
        if (!key) key = strstr(buf, "sec-websocket-key: ");
        if (!key) { closesocket(s); SetEvent(server_done); continue; }
        key += 19;
        end = strstr(key, "\r\n");
        *end = 0;
        accept_key(key, accept_value);
        *end = '\r';
        proto = strstr(buf, "Sec-WebSocket-Protocol: ");
        sprintf(head, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                "Sec-WebSocket-Accept: %s\r\n%s\r\n", accept_value, proto ? "Sec-WebSocket-Protocol: chat\r\n" : "");
        send_all(s, head, strlen(head));
        if (strstr(buf, "GET /hold"))
        {
            while ((opcode = read_frame(s, payload, &plen)) >= 0)
                if (opcode == 8) break;
            Sleep(1500);
            printf("SERVER lets go\n");
            closesocket(s);
            SetEvent(server_done);
            continue;
        }
        if (strstr(buf, "GET /drop") || strstr(buf, "GET /reset"))
        {
            if (strstr(buf, "GET /reset"))
            {
                struct linger linger = {1, 0};
                setsockopt(s, SOL_SOCKET, SO_LINGER, (char *)&linger, sizeof(linger));
            }
            Sleep(300);
            printf("SERVER %s\n", strstr(buf, "GET /reset") ? "resets" : "drops");
            closesocket(s);
            SetEvent(server_done);
            continue;
        }

        if (strstr(buf, "GET /codes"))
        {
            /* whatever close frame comes, answered with 1000 "ok" */
            static const BYTE ok[] = {0x03, 0xe8, 'o', 'k'};
            while ((opcode = read_frame(s, payload, &plen)) >= 0)
                if (opcode == 8) { send_frame(s, 1, 8, ok, sizeof(ok)); break; }
            closesocket(s);
            SetEvent(server_done);
            continue;
        }
        if (strstr(buf, "GET /recv"))
        {
            /* the server closes first, with one of these */
            static const struct { int len; BYTE data[8]; } closes[] =
            {
                {2, {0x03, 0xe7}}, {2, {0x03, 0xed}}, {2, {0x03, 0xee}}, {2, {0x03, 0xf7}}, {2, {0x13, 0x88}},
                {1, {0x03}}, {3, {0x03, 0xe8, 0xff}}, {0},
            };
            int n = atoi(strstr(buf, "GET /recv") + 9);
            send_frame(s, 1, 8, closes[n].data, closes[n].len);
            while ((opcode = read_frame(s, payload, &plen)) >= 0)
                if (opcode == 8) break;
            closesocket(s);
            SetEvent(server_done);
            continue;
        }

        /* what each scenario does once the socket is up */
        if (!strcmp(scenario, "echo"))
        {
            while ((opcode = read_frame(s, payload, &plen)) >= 0)
            {
                if (opcode == 8)
                {
                    send_frame(s, 1, 8, payload, plen);
                    break;
                }
                if (plen >= 4 && !memcmp(payload, "send", 4))
                {
                    send_frame(s, 1, 1, "world", 5);
                    send_frame(s, 1, 2, "\x01\x02\x03", 3);
                    send_frame(s, 0, 1, "frag", 4);
                    send_frame(s, 1, 0, "ment", 4);
                    send_frame(s, 1, 1, "\xe4\xb8\xad\xc3\xa9", 5);
                }
                if (plen >= 3 && !memcmp(payload, "bye", 3))
                {
                    BYTE close[5] = {0x03, 0xe8, 'b', 'y', 'e'};
                    send_frame(s, 1, 8, close, 5);
                    read_frame(s, payload, &plen);
                    break;
                }
            }
        }
        closesocket(s);
        SetEvent(server_done);
    }
}

static void start_server(void)
{
    struct sockaddr_in addr = {0};
    int addr_len = sizeof(addr);
    WSADATA wsa;

    WSAStartup(MAKEWORD(2, 2), &wsa);
    listener = socket(AF_INET, SOCK_STREAM, 0);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(listener, (struct sockaddr *)&addr, sizeof(addr));
    getsockname(listener, (struct sockaddr *)&addr, &addr_len);
    port = ntohs(addr.sin_port);
    listen(listener, 16);
    server_done = CreateEventW(NULL, FALSE, FALSE, NULL);
    CreateThread(NULL, 0, server_thread, NULL, 0, NULL);
}

/* the handlers */
struct message_handler
{
    ITypedEventHandler_MessageWebSocket_MessageWebSocketMessageReceivedEventArgs iface;
    LONG ref;
    int count;
    HANDLE event;
};

static HRESULT WINAPI message_QueryInterface(ITypedEventHandler_MessageWebSocket_MessageWebSocketMessageReceivedEventArgs *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_ITypedEventHandler_MessageWebSocket_MessageWebSocketMessageReceivedEventArgs))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI message_AddRef(ITypedEventHandler_MessageWebSocket_MessageWebSocketMessageReceivedEventArgs *iface) { return 2; }
static ULONG WINAPI message_Release(ITypedEventHandler_MessageWebSocket_MessageWebSocketMessageReceivedEventArgs *iface) { return 1; }

static void read_reader(IDataReader *reader)
{
    UINT32 left = 0, i;
    UnicodeEncoding encoding = 99;
    ByteOrder order = 99;
    InputStreamOptions options = 99;
    HSTRING str = NULL;
    HRESULT hr;

    IDataReader_get_UnconsumedBufferLength(reader, &left);
    IDataReader_get_UnicodeEncoding(reader, &encoding);
    IDataReader_get_ByteOrder(reader, &order);
    IDataReader_get_InputStreamOptions(reader, &options);
    printf("    reader: unconsumed %u encoding %d order %d options %d\n", left, encoding, order, options);
    print_class("    reader", reader);
    hr = IDataReader_ReadString(reader, left, &str);
    printf("    ReadString %#lx %s\n", hr, P(str));
    WindowsDeleteString(str);
    IDataReader_get_UnconsumedBufferLength(reader, &left);
    printf("    after: unconsumed %u\n", left);
    (void)i;
}

static HRESULT WINAPI message_Invoke(ITypedEventHandler_MessageWebSocket_MessageWebSocketMessageReceivedEventArgs *iface,
                                     IMessageWebSocket *sender, IMessageWebSocketMessageReceivedEventArgs *args)
{
    struct message_handler *impl = CONTAINING_RECORD(iface, struct message_handler, iface);
    IMessageWebSocketMessageReceivedEventArgs2 *args2;
    IDataReader *reader = NULL, *reader2 = NULL;
    IInputStream *stream = NULL;
    SocketMessageType type = 99;
    boolean complete = 2;
    HRESULT hr;

    impl->count++;
    IMessageWebSocketMessageReceivedEventArgs_get_MessageType(args, &type);
    if (SUCCEEDED(IMessageWebSocketMessageReceivedEventArgs_QueryInterface(args, &IID_IMessageWebSocketMessageReceivedEventArgs2, (void **)&args2)))
    {
        IMessageWebSocketMessageReceivedEventArgs2_get_IsMessageComplete(args2, &complete);
        IMessageWebSocketMessageReceivedEventArgs2_Release(args2);
    }
    printf("  MessageReceived %d: type %d complete %d thread %s\n", impl->count, type, complete, "");
    print_class("    args", args);
    if (impl->count == 1)
    {
        hr = IMessageWebSocketMessageReceivedEventArgs_GetDataStream(args, &stream);
        printf("    GetDataStream %#lx %p\n", hr, stream);
        if (stream)
        {
            IAsyncOperationWithProgress_IBuffer_UINT32 *op;
            IBuffer *buffer = make_buffer(NULL, 0), *result = NULL;
            HRESULT error;
            print_class("    stream", stream);
            {
                IBuffer *big;
                IBufferFactory *factory = get_factory(L"Windows.Storage.Streams.Buffer", &IID_IBufferFactory);
                IBufferFactory_Create(factory, 100, &big);
                IBufferFactory_Release(factory);
                IBuffer_Release(buffer);
                buffer = big;
            }
            hr = IInputStream_ReadAsync(stream, buffer, 100, InputStreamOptions_Partial, &op);
            printf("    ReadAsync %#lx\n", hr);
            if (SUCCEEDED(hr))
            {
                wait_async(op, &error);
                IAsyncOperationWithProgress_IBuffer_UINT32_GetResults(op, &result);
                if (result) { dump_buffer("    stream read", result); IBuffer_Release(result); }
                IAsyncOperationWithProgress_IBuffer_UINT32_Release(op);
            }
            IBuffer_Release(buffer);
            IInputStream_Release(stream);
        }
        hr = IMessageWebSocketMessageReceivedEventArgs_GetDataReader(args, &reader);
        printf("    GetDataReader after GetDataStream %#lx %p\n", hr, reader);
        if (reader) { read_reader(reader); IDataReader_Release(reader); }
    }
    else
    {
        hr = IMessageWebSocketMessageReceivedEventArgs_GetDataReader(args, &reader);
        printf("    GetDataReader %#lx %p\n", hr, reader);
        hr = IMessageWebSocketMessageReceivedEventArgs_GetDataReader(args, &reader2);
        printf("    GetDataReader again %#lx same %d\n", hr, reader == reader2);
        if (reader) read_reader(reader);
        if (reader) IDataReader_Release(reader);
        if (reader2) IDataReader_Release(reader2);
        hr = IMessageWebSocketMessageReceivedEventArgs_GetDataStream(args, &stream);
        printf("    GetDataStream after GetDataReader %#lx %p\n", hr, stream);
        if (stream) IInputStream_Release(stream);
    }
    SetEvent(impl->event);
    return S_OK;
}

static const ITypedEventHandler_MessageWebSocket_MessageWebSocketMessageReceivedEventArgsVtbl message_vtbl =
{
    message_QueryInterface, message_AddRef, message_Release, message_Invoke,
};

static HRESULT WINAPI error_message_Invoke(ITypedEventHandler_MessageWebSocket_MessageWebSocketMessageReceivedEventArgs *iface,
                                           IMessageWebSocket *sender, IMessageWebSocketMessageReceivedEventArgs *args)
{
    struct message_handler *impl = CONTAINING_RECORD(iface, struct message_handler, iface);
    IMessageWebSocketMessageReceivedEventArgs2 *args2;
    IDataReader *reader = NULL;
    IInputStream *stream = NULL;
    SocketMessageType type = 99;
    boolean complete = 2;
    HRESULT hr;

    hr = IMessageWebSocketMessageReceivedEventArgs_get_MessageType(args, &type);
    printf("  MessageReceived: MessageType %#lx %d", hr, type);
    if (SUCCEEDED(IMessageWebSocketMessageReceivedEventArgs_QueryInterface(args, &IID_IMessageWebSocketMessageReceivedEventArgs2, (void **)&args2)))
    {
        hr = IMessageWebSocketMessageReceivedEventArgs2_get_IsMessageComplete(args2, &complete);
        printf(" IsMessageComplete %#lx %d", hr, complete);
        IMessageWebSocketMessageReceivedEventArgs2_Release(args2);
    }
    hr = IMessageWebSocketMessageReceivedEventArgs_GetDataReader(args, &reader);
    printf(" GetDataReader %#lx %p", hr, reader);
    if (reader) IDataReader_Release(reader);
    hr = IMessageWebSocketMessageReceivedEventArgs_GetDataStream(args, &stream);
    printf(" GetDataStream %#lx %p\n", hr, stream);
    if (stream) IInputStream_Release(stream);
    SetEvent(impl->event);
    return S_OK;
}

static const ITypedEventHandler_MessageWebSocket_MessageWebSocketMessageReceivedEventArgsVtbl error_message_vtbl =
{
    message_QueryInterface, message_AddRef, message_Release, error_message_Invoke,
};

struct closed_handler
{
    ITypedEventHandler_IWebSocket_WebSocketClosedEventArgs iface;
    HANDLE event;
};

static HRESULT WINAPI closed_QueryInterface(ITypedEventHandler_IWebSocket_WebSocketClosedEventArgs *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_ITypedEventHandler_IWebSocket_WebSocketClosedEventArgs))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI closed_AddRef(ITypedEventHandler_IWebSocket_WebSocketClosedEventArgs *iface) { return 2; }
static ULONG WINAPI closed_Release(ITypedEventHandler_IWebSocket_WebSocketClosedEventArgs *iface) { return 1; }
static HRESULT WINAPI closed_Invoke(ITypedEventHandler_IWebSocket_WebSocketClosedEventArgs *iface, IWebSocket *sender,
                                    IWebSocketClosedEventArgs *args)
{
    struct closed_handler *impl = CONTAINING_RECORD(iface, struct closed_handler, iface);
    UINT16 code = 0;
    HSTRING reason = NULL;
    IWebSocketClosedEventArgs_get_Code(args, &code);
    IWebSocketClosedEventArgs_get_Reason(args, &reason);
    printf("  Closed: code %u reason %s%s\n", code, P(reason), close_t0 && GetTickCount() - close_t0 < 1000 ? " (at once)" :
           close_t0 ? " (late)" : "");
    print_class("    closed args", args);
    WindowsDeleteString(reason);
    SetEvent(impl->event);
    return S_OK;
}
static const ITypedEventHandler_IWebSocket_WebSocketClosedEventArgsVtbl closed_vtbl =
{
    closed_QueryInterface, closed_AddRef, closed_Release, closed_Invoke,
};

static IUriRuntimeClass *ws_url(const char *path)
{
    WCHAR buf[256];
    swprintf(buf, ARRAY_SIZE(buf), L"ws://127.0.0.1:%d%S", port, path);
    return make_uri(buf);
}

static void show_information(IMessageWebSocket *socket, const char *what)
{
    IWebSocketInformation *info = NULL, *base;
    IWebSocketInformation2 *info2;
    IHostName *host = NULL;
    HSTRING str = NULL;
    BandwidthStatistics stats;
    HRESULT hr;

    hr = IMessageWebSocket_get_Information(socket, &info);
    printf("%s: Information %#lx %p\n", what, hr, info);
    if (!info) return;
    print_class("  information", info);
    IWebSocketInformation_QueryInterface(info, &IID_IWebSocketInformation, (void **)&base);
    hr = IWebSocketInformation_get_LocalAddress(base, &host);
    printf("  LocalAddress %#lx %p", hr, host);
    if (host) { IHostName_get_CanonicalName(host, &str); printf(" %s", P(str)); WindowsDeleteString(str); str = NULL; IHostName_Release(host); }
    printf("\n");
    memset(&stats, 0xcc, sizeof(stats));
    hr = IWebSocketInformation_get_BandwidthStatistics(base, &stats);
    printf("  BandwidthStatistics %#lx %I64u %I64u %d %d\n", hr, stats.OutboundBitsPerSecond, stats.InboundBitsPerSecond,
           stats.OutboundBandwidthPeaked, stats.InboundBandwidthPeaked);
    hr = IWebSocketInformation_get_Protocol(base, &str);
    printf("  Protocol %#lx %s\n", hr, P(str));
    WindowsDeleteString(str);
    if (SUCCEEDED(IWebSocketInformation_QueryInterface(base, &IID_IWebSocketInformation2, (void **)&info2)))
    {
        ICertificate *cert = (void *)0xdeadbeef;
        SocketSslErrorSeverity severity = 99;
        hr = IWebSocketInformation2_get_ServerCertificate(info2, &cert);
        printf("  ServerCertificate %#lx %p\n", hr, cert);
        hr = IWebSocketInformation2_get_ServerCertificateErrorSeverity(info2, &severity);
        printf("  ServerCertificateErrorSeverity %#lx %d\n", hr, severity);
        IWebSocketInformation2_Release(info2);
    }
    IWebSocketInformation_Release(base);
    IWebSocketInformation_Release(info);
}

static void write_message(IMessageWebSocket *socket, const char *text, const char *what)
{
    IAsyncOperationWithProgress_UINT32_UINT32 *op;
    IOutputStream *stream = NULL, *stream2 = NULL;
    IWebSocket *base;
    UINT32 written = 0;
    HRESULT hr, error;
    AsyncStatus status;
    IBuffer *buffer = make_buffer(text, strlen(text));

    IMessageWebSocket_QueryInterface(socket, &IID_IWebSocket, (void **)&base);
    hr = IWebSocket_get_OutputStream(base, &stream);
    IWebSocket_get_OutputStream(base, &stream2);
    printf("%s: OutputStream %#lx same %d\n", what, hr, stream == stream2);
    if (stream2) IOutputStream_Release(stream2);
    if (!stream) { IWebSocket_Release(base); IBuffer_Release(buffer); return; }
    print_class("  output stream", stream);
    hr = IOutputStream_WriteAsync(stream, buffer, &op);
    printf("  WriteAsync %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        print_class("  write op", op);
        status = wait_async(op, &error);
        hr = IAsyncOperationWithProgress_UINT32_UINT32_GetResults(op, &written);
        printf("  write status %d error %#lx results %#lx written %u\n", status, error, hr, written);
        IAsyncOperationWithProgress_UINT32_UINT32_Release(op);
    }
    {
        IAsyncOperation_boolean *flush;
        boolean done = 2;
        hr = IOutputStream_FlushAsync(stream, &flush);
        if (SUCCEEDED(hr))
        {
            wait_async(flush, &error);
            IAsyncOperation_boolean_GetResults(flush, &done);
            printf("  FlushAsync %#lx result %d\n", hr, done);
            IAsyncOperation_boolean_Release(flush);
        }
        else printf("  FlushAsync %#lx\n", hr);
    }
    IOutputStream_Release(stream);
    IWebSocket_Release(base);
    IBuffer_Release(buffer);
}

int main(void)
{
    static const INT32 errors[] = { 0, E_FAIL, 0x80072efd, 0x80072ee2, 0x80072ee7, 0x80072f8f, 0x80072f7d, 0x80190194,
                                    0x80190191, 0x801901f4, 0x80072efe, 0x80072eff, 0x80072f78, 0x800704cd, 0x80072f0d,
                                    0x80072f06, 0x80072f05, E_ABORT, 0x800704c7, 0x80190130 };
    struct message_handler message = {{&message_vtbl}, 1};
    struct closed_handler closed = {{&closed_vtbl}};
    IMessageWebSocketControl2 *control2;
    IMessageWebSocketControl *control;
    IMessageWebSocket *socket;
    IWebSocketControl *base_control;
    IWebSocket *base;
    IAsyncAction *action;
    IVector_HSTRING *protocols;
    EventRegistrationToken token;
    UINT32 u32;
    SocketMessageType type;
    MessageWebSocketReceiveMode mode;
    TimeSpan span;
    HRESULT hr, error;
    AsyncStatus status;
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    RoInitialize(RO_INIT_MULTITHREADED);
    start_server();
    message.event = CreateEventW(NULL, FALSE, FALSE, NULL);
    closed.event = CreateEventW(NULL, FALSE, FALSE, NULL);

    {
        IWebSocketErrorStatics *ws_error = get_factory(L"Windows.Networking.Sockets.WebSocketError", &IID_IWebSocketErrorStatics);
        IWebErrorStatics *web_error = get_factory(L"Windows.Web.WebError", &IID_IWebErrorStatics);
        for (i = 0; i < ARRAY_SIZE(errors); i++)
        {
            WebErrorStatus a = 999, b = 999;
            HRESULT hr1 = ws_error ? IWebSocketErrorStatics_GetStatus(ws_error, errors[i], &a) : E_FAIL;
            HRESULT hr2 = web_error ? IWebErrorStatics_GetStatus(web_error, errors[i], &b) : E_FAIL;
            printf("GetStatus(%#lx): websocket %#lx %d web %#lx %d\n", (ULONG)errors[i], hr1, a, hr2, b);
        }
    }

    socket = activate(L"Windows.Networking.Sockets.MessageWebSocket", &IID_IMessageWebSocket);
    print_class("socket", socket);
    print_string("socket", socket);
    QI("socket", socket, IMessageWebSocket2);
    QI("socket", socket, IMessageWebSocket3);
    QI("socket", socket, IClosable);
    hr = IMessageWebSocket_get_Control(socket, &control);
    printf("get_Control %#lx\n", hr);
    print_class("control", control);
    IMessageWebSocketControl_get_MaxMessageSize(control, &u32);
    printf("  MaxMessageSize %u\n", u32);
    IMessageWebSocketControl_get_MessageType(control, &type);
    printf("  MessageType %d\n", type);
    IMessageWebSocketControl_QueryInterface(control, &IID_IWebSocketControl, (void **)&base_control);
    IWebSocketControl_get_OutboundBufferSizeInBytes(base_control, &u32);
    printf("  OutboundBufferSizeInBytes %u\n", u32);
    hr = IWebSocketControl_get_SupportedProtocols(base_control, &protocols);
    printf("  SupportedProtocols %#lx\n", hr);
    print_class("  protocols", protocols);
    IVector_HSTRING_Append(protocols, S(L"chat"));
    IVector_HSTRING_Release(protocols);
    IMessageWebSocketControl_QueryInterface(control, &IID_IMessageWebSocketControl2, (void **)&control2);
    IMessageWebSocketControl2_get_ReceiveMode(control2, &mode);
    printf("  ReceiveMode %d\n", mode);
    IMessageWebSocketControl2_get_DesiredUnsolicitedPongInterval(control2, &span);
    printf("  DesiredUnsolicitedPongInterval %I64d\n", span.Duration);
    IMessageWebSocketControl2_get_ActualUnsolicitedPongInterval(control2, &span);
    printf("  ActualUnsolicitedPongInterval %I64d\n", span.Duration);
    show_information(socket, "before connect");

    IMessageWebSocket_QueryInterface(socket, &IID_IWebSocket, (void **)&base);
    hr = IWebSocket_SetRequestHeader(base, S(L"X-Test"), S(L"1"));
    printf("SetRequestHeader %#lx\n", hr);
    hr = IWebSocket_SetRequestHeader(base, S(L"Cookie"), S(L"c=1"));
    printf("SetRequestHeader(Cookie) %#lx\n", hr);
    IMessageWebSocket_add_MessageReceived(socket, &message.iface, &token);
    IWebSocket_add_Closed(base, &closed.iface, &token);
    {
        IOutputStream *stream = NULL;
        hr = IWebSocket_get_OutputStream(base, &stream);
        printf("OutputStream before connect %#lx %p\n", hr, stream);
        if (stream) IOutputStream_Release(stream);
    }

    scenario = "echo";
    hr = IWebSocket_ConnectAsync(base, ws_url("/ws"), &action);
    printf("ConnectAsync %#lx\n", hr);
    print_class("  connect action", action);
    status = wait_async(action, &error);
    printf("  connect status %d error %#lx\n", status, error);
    IAsyncAction_Release(action);
    show_information(socket, "after connect");
    IMessageWebSocketControl_put_MessageType(control, SocketMessageType_Utf8);
    write_message(socket, "hello", "utf8 message");
    Sleep(300);
    IMessageWebSocketControl_put_MessageType(control, SocketMessageType_Binary);
    write_message(socket, "bin", "binary message");
    Sleep(300);
    {
        IMessageWebSocket3 *socket3;
        if (SUCCEEDED(IMessageWebSocket_QueryInterface(socket, &IID_IMessageWebSocket3, (void **)&socket3)))
        {
            IAsyncOperationWithProgress_UINT32_UINT32 *op;
            IBuffer *buffer = make_buffer("ab", 2);
            UINT32 written = 0;
            hr = IMessageWebSocket3_SendNonfinalFrameAsync(socket3, buffer, &op);
            wait_async(op, &error);
            IAsyncOperationWithProgress_UINT32_UINT32_GetResults(op, &written);
            printf("SendNonfinalFrameAsync %#lx error %#lx written %u\n", hr, error, written);
            IAsyncOperationWithProgress_UINT32_UINT32_Release(op);
            IBuffer_Release(buffer);
            buffer = make_buffer("cd", 2);
            hr = IMessageWebSocket3_SendFinalFrameAsync(socket3, buffer, &op);
            wait_async(op, &error);
            IAsyncOperationWithProgress_UINT32_UINT32_GetResults(op, &written);
            printf("SendFinalFrameAsync %#lx error %#lx written %u\n", hr, error, written);
            IAsyncOperationWithProgress_UINT32_UINT32_Release(op);
            IBuffer_Release(buffer);
            IMessageWebSocket3_Release(socket3);
        }
    }
    Sleep(300);
    /* ask the server for its messages */
    IMessageWebSocketControl_put_MessageType(control, SocketMessageType_Utf8);
    write_message(socket, "send", "trigger");
    for (i = 0; i < 4; i++) if (WaitForSingleObject(message.event, 5000)) break;
    printf("received %d messages\n", message.count);

    /* the server closes */
    write_message(socket, "bye", "server close");
    printf("wait closed %lu\n", WaitForSingleObject(closed.event, 5000));
    {
        IOutputStream *stream = NULL;
        hr = IWebSocket_get_OutputStream(base, &stream);
        printf("OutputStream after close %#lx %p\n", hr, stream);
        if (stream)
        {
            IAsyncOperationWithProgress_UINT32_UINT32 *op;
            IBuffer *buffer = make_buffer("late", 4);
            hr = IOutputStream_WriteAsync(stream, buffer, &op);
            printf("  WriteAsync after close %#lx\n", hr);
            if (SUCCEEDED(hr))
            {
                status = wait_async(op, &error);
                printf("  status %d error %#lx\n", status, error);
                IAsyncOperationWithProgress_UINT32_UINT32_Release(op);
            }
            IBuffer_Release(buffer);
            IOutputStream_Release(stream);
        }
    }
    IWebSocket_Release(base);
    IMessageWebSocket_Release(socket);

    /* the client closes */
    socket = activate(L"Windows.Networking.Sockets.MessageWebSocket", &IID_IMessageWebSocket);
    IMessageWebSocket_QueryInterface(socket, &IID_IWebSocket, (void **)&base);
    IMessageWebSocket_add_MessageReceived(socket, &message.iface, &token);
    IWebSocket_add_Closed(base, &closed.iface, &token);
    hr = IWebSocket_ConnectAsync(base, ws_url("/ws2"), &action);
    status = wait_async(action, &error);
    printf("second connect status %d error %#lx\n", status, error);
    IAsyncAction_Release(action);
    hr = IWebSocket_CloseWithStatus(base, 1000, S(L"done"));
    printf("Close(1000, done) %#lx\n", hr);
    printf("wait closed %lu\n", WaitForSingleObject(closed.event, 3000));
    WaitForSingleObject(server_done, 3000);
    hr = IWebSocket_ConnectAsync(base, ws_url("/ws3"), &action);
    printf("ConnectAsync after close %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        status = wait_async(action, &error);
        printf("  status %d error %#lx\n", status, error);
        IAsyncAction_Release(action);
    }
    IWebSocket_Release(base);
    IMessageWebSocket_Release(socket);

    /* a server that will not upgrade */
    socket = activate(L"Windows.Networking.Sockets.MessageWebSocket", &IID_IMessageWebSocket);
    IMessageWebSocket_QueryInterface(socket, &IID_IWebSocket, (void **)&base);
    hr = IWebSocket_ConnectAsync(base, ws_url("/notfound"), &action);
    status = wait_async(action, &error);
    printf("404 connect status %d error %#lx\n", status, error);
    IAsyncAction_Release(action);
    IWebSocket_Release(base);
    IMessageWebSocket_Release(socket);

    /* nothing listening */
    socket = activate(L"Windows.Networking.Sockets.MessageWebSocket", &IID_IMessageWebSocket);
    IMessageWebSocket_QueryInterface(socket, &IID_IWebSocket, (void **)&base);
    {
        IUriRuntimeClass *uri = make_uri(L"ws://127.0.0.1:1/x");
        hr = IWebSocket_ConnectAsync(base, uri, &action);
        status = wait_async(action, &error);
        printf("refused connect status %d error %#lx\n", status, error);
        IAsyncAction_Release(action);
        uri = make_uri(L"http://127.0.0.1/x");
        hr = IWebSocket_ConnectAsync(base, uri, &action);
        printf("http scheme ConnectAsync %#lx\n", hr);
        if (SUCCEEDED(hr)) { status = wait_async(action, &error); printf("  status %d error %#lx\n", status, error); IAsyncAction_Release(action); }
    }
    IWebSocket_Release(base);
    IMessageWebSocket_Release(socket);

    /* closing when the server does not answer the close frame */
    {
        int j;
        for (j = 0; j < 2; j++)
        {
            socket = activate(L"Windows.Networking.Sockets.MessageWebSocket", &IID_IMessageWebSocket);
            IMessageWebSocket_QueryInterface(socket, &IID_IWebSocket, (void **)&base);
            IWebSocket_add_Closed(base, &closed.iface, &token);
            hr = IWebSocket_ConnectAsync(base, ws_url("/hold"), &action);
            status = wait_async(action, &error);
            printf("hold connect status %d error %#lx\n", status, error);
            IAsyncAction_Release(action);
            close_t0 = GetTickCount();
            if (j)
            {
                IClosable *closable;
                IMessageWebSocket_QueryInterface(socket, &IID_IClosable, (void **)&closable);
                printf("IClosable::Close %#lx\n", IClosable_Close(closable));
                IClosable_Release(closable);
            }
            else
            {
                IOutputStream *stream = NULL;
                printf("Close(1000, x) %#lx\n", IWebSocket_CloseWithStatus(base, 1000, S(L"x")));
                IWebSocket_get_OutputStream(base, &stream);
                if (stream)
                {
                    IAsyncOperationWithProgress_UINT32_UINT32 *op;
                    IBuffer *buffer = make_buffer("more", 4);
                    hr = IOutputStream_WriteAsync(stream, buffer, &op);
                    printf("  WriteAsync after Close(1000, x) %#lx\n", hr);
                    if (SUCCEEDED(hr))
                    {
                        status = wait_async(op, &error);
                        printf("  status %d error %#lx\n", status, error);
                        IAsyncOperationWithProgress_UINT32_UINT32_Release(op);
                    }
                    IBuffer_Release(buffer);
                    IOutputStream_Release(stream);
                }
            }
            printf("wait closed %lu\n", WaitForSingleObject(closed.event, 3000));
            WaitForSingleObject(server_done, 3000);
            close_t0 = 0;
            IWebSocket_Release(base);
            IMessageWebSocket_Release(socket);
        }
    }

    /* the client disposes of a connected socket */
    socket = activate(L"Windows.Networking.Sockets.MessageWebSocket", &IID_IMessageWebSocket);
    IMessageWebSocket_QueryInterface(socket, &IID_IWebSocket, (void **)&base);
    IWebSocket_add_Closed(base, &closed.iface, &token);
    hr = IWebSocket_ConnectAsync(base, ws_url("/ws4"), &action);
    status = wait_async(action, &error);
    printf("dispose connect status %d error %#lx\n", status, error);
    IAsyncAction_Release(action);
    {
        IClosable *closable;
        IOutputStream *stream = NULL;

        IMessageWebSocket_QueryInterface(socket, &IID_IClosable, (void **)&closable);
        hr = IClosable_Close(closable);
        printf("IClosable::Close %#lx\n", hr);
        printf("wait closed %lu\n", WaitForSingleObject(closed.event, 2000));
        printf("server done %lu\n", WaitForSingleObject(server_done, 3000));
        hr = IWebSocket_get_OutputStream(base, &stream);
        printf("OutputStream after dispose %#lx %p\n", hr, stream);
        if (stream)
        {
            IAsyncOperationWithProgress_UINT32_UINT32 *op;
            IBuffer *buffer = make_buffer("gone", 4);
            hr = IOutputStream_WriteAsync(stream, buffer, &op);
            printf("  WriteAsync after dispose %#lx\n", hr);
            if (SUCCEEDED(hr))
            {
                status = wait_async(op, &error);
                printf("  status %d error %#lx\n", status, error);
                IAsyncOperationWithProgress_UINT32_UINT32_Release(op);
            }
            IBuffer_Release(buffer);
            IOutputStream_Release(stream);
        }
        hr = IClosable_Close(closable);
        printf("IClosable::Close again %#lx\n", hr);
        hr = IWebSocket_CloseWithStatus(base, 1000, S(L"late"));
        printf("Close(1000) after dispose %#lx\n", hr);
        IClosable_Release(closable);
    }
    hr = IWebSocket_ConnectAsync(base, ws_url("/ws5"), &action);
    printf("ConnectAsync after dispose %#lx\n", hr);
    if (SUCCEEDED(hr)) { status = wait_async(action, &error); printf("  status %d error %#lx\n", status, error); IAsyncAction_Release(action); }
    IWebSocket_Release(base);
    IMessageWebSocket_Release(socket);

    /* the server goes away without a close frame */
    {
        static const char *paths[] = {"/drop", "/reset"};
        int j;
        for (j = 0; j < 2; j++)
        {
            socket = activate(L"Windows.Networking.Sockets.MessageWebSocket", &IID_IMessageWebSocket);
            IMessageWebSocket_QueryInterface(socket, &IID_IWebSocket, (void **)&base);
            IWebSocket_add_Closed(base, &closed.iface, &token);
            {
                static struct message_handler error_message = {{&error_message_vtbl}};
                if (!error_message.event) error_message.event = CreateEventW(NULL, FALSE, FALSE, NULL);
                IMessageWebSocket_add_MessageReceived(socket, &error_message.iface, &token);
            }
            hr = IWebSocket_ConnectAsync(base, ws_url(paths[j]), &action);
            status = wait_async(action, &error);
            printf("%s connect status %d error %#lx\n", paths[j], status, error);
            IAsyncAction_Release(action);
            printf("wait closed %lu\n", WaitForSingleObject(closed.event, 3000));
            {
                IOutputStream *stream = NULL;
                IWebSocket_get_OutputStream(base, &stream);
                if (stream)
                {
                    IAsyncOperationWithProgress_UINT32_UINT32 *op;
                    IBuffer *buffer = make_buffer("gone", 4);
                    hr = IOutputStream_WriteAsync(stream, buffer, &op);
                    printf("  WriteAsync after %s %#lx\n", paths[j], hr);
                    if (SUCCEEDED(hr))
                    {
                        status = wait_async(op, &error);
                        printf("  status %d error %#lx\n", status, error);
                        IAsyncOperationWithProgress_UINT32_UINT32_Release(op);
                    }
                    IBuffer_Release(buffer);
                    IOutputStream_Release(stream);
                }
            }
            printf("  calling Close(1000, bye)\n");
            hr = IWebSocket_CloseWithStatus(base, 1000, S(L"bye"));
            printf("  Close returned %#lx\n", hr);
            printf("  wait closed %lu\n", WaitForSingleObject(closed.event, 2000));
            IWebSocket_Release(base);
            IMessageWebSocket_Release(socket);
        }
    }

    /* which close codes and reasons go out, and how */
    {
        static const UINT16 codes[] = {0, 999, 1000, 1004, 1005, 1006, 1012, 1015, 2999, 3000, 5000, 65535};
        WCHAR long_reason[131], wide_reason[51];
        int j;

        for (j = 0; j < 130; j++) long_reason[j] = 'x';
        long_reason[130] = 0;
        for (j = 0; j < 50; j++) wide_reason[j] = 0x4e2d;
        wide_reason[50] = 0;
        for (j = 0; j < ARRAY_SIZE(codes) + 3; j++)
        {
            UINT16 code = j < ARRAY_SIZE(codes) ? codes[j] : 1000;
            const WCHAR *reason = j < ARRAY_SIZE(codes) ? L"r" : j == ARRAY_SIZE(codes) ? L"" :
                                  j == ARRAY_SIZE(codes) + 1 ? long_reason : wide_reason;

            socket = activate(L"Windows.Networking.Sockets.MessageWebSocket", &IID_IMessageWebSocket);
            IMessageWebSocket_QueryInterface(socket, &IID_IWebSocket, (void **)&base);
            IWebSocket_add_Closed(base, &closed.iface, &token);
            hr = IWebSocket_ConnectAsync(base, ws_url("/codes"), &action);
            status = wait_async(action, &error);
            IAsyncAction_Release(action);
            if (j == ARRAY_SIZE(codes)) code = 1005;
            hr = IWebSocket_CloseWithStatus(base, code, S(reason));
            printf("Close(%u, %u chars) connect %d %#lx: %#lx\n", code, (UINT)wcslen(reason), status, error, hr);
            if (FAILED(hr)) printf("  then Close(1000, ok) %#lx\n", IWebSocket_CloseWithStatus(base, 1000, S(L"ok")));
            printf("  wait closed %lu\n", WaitForSingleObject(closed.event, 3000));
            WaitForSingleObject(server_done, 3000);
            IWebSocket_Release(base);
            IMessageWebSocket_Release(socket);
        }
    }

    /* what a close frame from the server is taken to say */
    {
        int j;
        for (j = 0; j < 8; j++)
        {
            char path[16];
            static struct message_handler error_message = {{&error_message_vtbl}};
            if (!error_message.event) error_message.event = CreateEventW(NULL, FALSE, FALSE, NULL);
            sprintf(path, "/recv%d", j);
            ResetEvent(closed.event);
            ResetEvent(error_message.event);
            socket = activate(L"Windows.Networking.Sockets.MessageWebSocket", &IID_IMessageWebSocket);
            IMessageWebSocket_QueryInterface(socket, &IID_IWebSocket, (void **)&base);
            IWebSocket_add_Closed(base, &closed.iface, &token);
            IMessageWebSocket_add_MessageReceived(socket, &error_message.iface, &token);
            hr = IWebSocket_ConnectAsync(base, ws_url(path), &action);
            status = wait_async(action, &error);
            IAsyncAction_Release(action);
            printf("%s connect %d %#lx\n", path, status, error);
            printf("  wait message %lu\n", WaitForSingleObject(error_message.event, 2000));
            {
                IOutputStream *stream = NULL;
                IWebSocket_get_OutputStream(base, &stream);
                if (stream)
                {
                    IAsyncOperationWithProgress_UINT32_UINT32 *op;
                    IBuffer *buffer = make_buffer("w", 1);
                    hr = IOutputStream_WriteAsync(stream, buffer, &op);
                    printf("  WriteAsync %#lx\n", hr);
                    if (SUCCEEDED(hr))
                    {
                        status = wait_async(op, &error);
                        printf("  status %d error %#lx\n", status, error);
                        IAsyncOperationWithProgress_UINT32_UINT32_Release(op);
                    }
                    IBuffer_Release(buffer);
                    IOutputStream_Release(stream);
                }
            }
            printf("  wait closed %lu\n", WaitForSingleObject(closed.event, 500));
            printf("  calling Close(1000, bye)\n");
            hr = IWebSocket_CloseWithStatus(base, 1000, S(L"bye"));
            printf("  Close returned %#lx\n", hr);
            printf("  wait closed %lu\n", WaitForSingleObject(closed.event, 2000));
            printf("  server done %lu\n", WaitForSingleObject(server_done, 3000));
            IWebSocket_Release(base);
            IMessageWebSocket_Release(socket);
        }
    }

    RoUninitialize();
    printf("done\n");
    return 0;
}
