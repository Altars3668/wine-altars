/* probe_winhttp_ws: the WinHTTP web socket API against a server in this process: what the upgrade
 * request looks like, and what the client can still do once the server has sent its close frame. */
#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <stdio.h>

static SOCKET listener;
static int port;
static HANDLE server_done;
static int server_frames = 4;
static int server_silent;   /* send no close frame; echo the client's */
static int server_mode;     /* 2: an empty close frame first; 3: answer a close frame with an empty one;
                             * 4: the close frame closes[server_close] first */
static int server_close;
/* raw frames the server may start with */
static const struct { int len; BYTE data[16]; int reset; } closes[] =
{
    {4, {0x88, 2, 0x03, 0xe7}}, {4, {0x88, 2, 0x03, 0xed}}, {4, {0x88, 2, 0x03, 0xee}}, {4, {0x88, 2, 0x03, 0xf7}},
    {4, {0x88, 2, 0x13, 0x88}}, {3, {0x88, 1, 0x03}}, {5, {0x88, 3, 0x03, 0xe8, 0xff}}, {4, {0x88, 2, 0x00, 0x00}},
    {4, {0x88, 2, 0x03, 0xf4}}, {6, {0x88, 4, 0x03, 0xe8, 0xed, 0xa0}}, {7, {0x88, 5, 0x03, 0xe8, 0xc0, 0xaf, 'x'}},
    {3, {0x83, 1, 'a'}}, {3, {0xc1, 1, 'a'}}, {7, {0x81, 0x81, 1, 2, 3, 4, 'a' ^ 1}}, {3, {0x81, 1, 0xff}},
    {3, {0x80, 1, 'a'}}, {4, {0x08, 2, 0x03, 0xe8}}, {3, {0x01, 1, 'a'}},
    {6, {0x01, 1, 'a', 0x81, 1, 'b'}}, {8, {0x89, 3, 'a', 'b', 'c', 0x81, 1, 'z'}}, {5, {0x89, 0, 0x81, 1, 'z'}},
    {7, {0x8a, 2, 'x', 'y', 0x81, 1, 'z'}}, {7, {0x01, 2, 0xe4, 0xb8, 0x80, 1, 0xad}}, {4, {0x81, 2, 0xe4, 0xb8}},
    {3, {0x01, 1, 'a'}, 1}, {3, {0x81, 1, 'a'}, 1}, {5, {0x09, 1, 'p', 0x80, 0}},
};
static char server_log[2048];

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
    BYTE head[2];
    head[0] = (fin ? 0x80 : 0) | opcode;
    head[1] = len;
    send_all(s, head, 2);
    if (len) send_all(s, data, len);
}

static int read_frame(SOCKET s)
{
    BYTE head[2], mask[4], payload[256];
    int len, i;

    if (!recv_all(s, head, 2)) return -1;
    len = head[1] & 0x7f;
    if (len > 125) return -1;
    if (head[1] & 0x80) { if (!recv_all(s, mask, 4)) return -1; }
    if (!recv_all(s, payload, len)) return -1;
    if (head[1] & 0x80) for (i = 0; i < len; i++) payload[i] ^= mask[i % 4];
    printf("SERVER frame: fin %d opcode %d masked %d len %d:", head[0] >> 7, head[0] & 0xf, head[1] >> 7, len);
    for (i = 0; i < len; i++)
        if (payload[i] >= 0x20 && payload[i] < 0x7f) printf("%c", payload[i]); else printf("\\x%02x", payload[i]);
    printf("\n");
    return head[0] & 0xf;
}

/* a frame into server_log as [first byte, length:payload hex] */
static int log_frame(SOCKET s)
{
    BYTE head[2], mask[4], payload[256];
    int len, i, n;

    if (!recv_all(s, head, 2)) return -1;
    len = head[1] & 0x7f;
    if (len > 125) return -1;
    if (head[1] & 0x80) { if (!recv_all(s, mask, 4)) return -1; }
    if (!recv_all(s, payload, len)) return -1;
    if (head[1] & 0x80) for (i = 0; i < len; i++) payload[i] ^= mask[i % 4];
    n = strlen(server_log);
    n += sprintf(server_log + n, "[%02x %d:", head[0], len);
    for (i = 0; i < len && n < (int)sizeof(server_log) - 8; i++) n += sprintf(server_log + n, "%02x", payload[i]);
    strcpy(server_log + n, "]");
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

/* upgrade, send a close frame (1000, "bye"), then print what the client sends for a while */
static DWORD WINAPI server_thread(void *arg)
{
    for (;;)
    {
        char buf[4096], head[1024], accept_value[64], *key, *end;
        int len = 0, ret, i;
        DWORD timeout = 1500;
        SOCKET s = accept(listener, NULL, NULL);

        if (s == INVALID_SOCKET) return 0;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
        for (;;)
        {
            ret = recv(s, buf + len, sizeof(buf) - 1 - len, 0);
            if (ret <= 0) break;
            len += ret;
            buf[len] = 0;
            if (strstr(buf, "\r\n\r\n")) break;
        }
        if (!server_mode) print_escaped("SERVER got: ", buf, len);
        key = strstr(buf, "Sec-WebSocket-Key: ");
        if (!key) key = strstr(buf, "sec-websocket-key: ");
        if (!key) { closesocket(s); SetEvent(server_done); continue; }
        key += 19;
        end = strstr(key, "\r\n");
        *end = 0;
        accept_key(key, accept_value);
        *end = '\r';
        sprintf(head, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                "Sec-WebSocket-Accept: %s\r\n%s\r\n", accept_value,
                strstr(buf, "Sec-WebSocket-Protocol: ") ? "Sec-WebSocket-Protocol: chat\r\n" : "");
        send_all(s, head, strlen(head));
        if (server_mode)
        {
            DWORD start = GetTickCount();
            if (server_mode == 2) send_frame(s, 1, 8, NULL, 0);
            if (server_mode == 4) send_all(s, closes[server_close].data, closes[server_close].len);
            if (server_mode == 4 && closes[server_close].reset)
            {
                struct linger linger = {1, 0};
                Sleep(200);
                setsockopt(s, SOL_SOCKET, SO_LINGER, (char *)&linger, sizeof(linger));
                closesocket(s);
                SetEvent(server_done);
                continue;
            }
            for (i = 0; i < server_frames; i++)
            {
                int opcode = log_frame(s);
                if (opcode < 0)
                {
                    int err = WSAGetLastError();
                    sprintf(server_log + strlen(server_log), "[end%s]",
                            err == WSAETIMEDOUT ? " timeout" : GetTickCount() - start < 1000 ? "" : " late");
                    break;
                }
                if (opcode == 8)
                {
                    if (server_mode == 3) send_frame(s, 1, 8, NULL, 0);
                    break;
                }
            }
            closesocket(s);
            SetEvent(server_done);
            continue;
        }
        if (!server_silent)
        {
            BYTE close[5] = {0x03, 0xe8, 'b', 'y', 'e'};
            send_frame(s, 1, 8, close, 5);
        }
        for (i = 0; i < server_frames; i++)
        {
            int opcode = read_frame(s);
            if (opcode < 0) { printf("SERVER read ended %d\n", WSAGetLastError()); break; }
            if (opcode == 8 && server_silent)
            {
                BYTE close[4] = {0x03, 0xe8, 'o', 'k'};
                send_frame(s, 1, 8, close, 4);
                printf("SERVER echoed close\n");
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

static void run(const char *name, const WCHAR *agent, DWORD open_flags, const WCHAR *headers, BOOL after_close)
{
    HINTERNET session, connect, request, socket;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
    BYTE data[256], reason[128];
    DWORD read = 0, err, len = 0;
    USHORT code = 0;
    BOOL ret;

    printf("=== %s\n", name);
    session = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
    connect = WinHttpConnect(session, L"127.0.0.1", port, 0);
    request = WinHttpOpenRequest(connect, L"GET", L"/ws", NULL, NULL, NULL, open_flags);
    if (headers) printf("AddRequestHeaders %d\n", WinHttpAddRequestHeaders(request, headers, -1, WINHTTP_ADDREQ_FLAG_ADD));
    printf("upgrade option %d\n", WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0));
    ret = WinHttpSendRequest(request, NULL, 0, NULL, 0, 0, 0);
    printf("SendRequest %d %lu\n", ret, ret ? 0 : GetLastError());
    ret = WinHttpReceiveResponse(request, NULL);
    printf("ReceiveResponse %d %lu\n", ret, ret ? 0 : GetLastError());
    socket = WinHttpWebSocketCompleteUpgrade(request, 0);
    printf("CompleteUpgrade %p %lu\n", socket, socket ? 0 : GetLastError());
    WinHttpCloseHandle(request);
    if (socket && after_close)
    {
        err = WinHttpWebSocketReceive(socket, data, sizeof(data), &read, &type);
        printf("receive %lu type %d read %lu\n", err, type, read);
        err = WinHttpWebSocketQueryCloseStatus(socket, &code, reason, sizeof(reason), &len);
        printf("close status %lu code %u len %lu\n", err, code, len);
        Sleep(200);
        err = WinHttpWebSocketSend(socket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, (void *)"late", 4);
        printf("send after close frame %lu\n", err);
        err = WinHttpWebSocketSend(socket, WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE, (void *)"ab", 2);
        printf("send fragment after close frame %lu\n", err);
        err = WinHttpWebSocketSend(socket, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE, (void *)"cd", 2);
        printf("send final after close frame %lu\n", err);
        err = WinHttpWebSocketReceive(socket, data, sizeof(data), &read, &type);
        printf("receive again %lu type %d read %lu\n", err, type, read);
        err = WinHttpWebSocketShutdown(socket, 1000, (void *)"done", 4);
        printf("shutdown %lu\n", err);
        err = WinHttpWebSocketSend(socket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, (void *)"after", 5);
        printf("send after shutdown %lu\n", err);
        err = WinHttpWebSocketClose(socket, 1000, (void *)"done", 4);
        printf("close %lu\n", err);
    }
    if (socket) WinHttpCloseHandle(socket);
    WaitForSingleObject(server_done, 5000);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
}

struct receiver
{
    HINTERNET socket;
    DWORD err, read, start, elapsed;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
    HANDLE done;
};

static DWORD WINAPI receiver_thread(void *arg)
{
    struct receiver *r = arg;
    BYTE data[256];
    r->err = WinHttpWebSocketReceive(r->socket, data, sizeof(data), &r->read, &r->type);
    r->elapsed = GetTickCount() - r->start;
    SetEvent(r->done);
    return 0;
}

/* a receive is blocked in another thread when the socket is closed */
static void run_blocked(const char *name, BOOL use_close)
{
    HINTERNET session, connect, request, socket;
    struct receiver r = {0};
    DWORD err, start;
    USHORT code = 0;
    BYTE reason[64];
    DWORD len = 0;

    printf("=== %s\n", name);
    session = WinHttpOpen(NULL, WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
    connect = WinHttpConnect(session, L"127.0.0.1", port, 0);
    request = WinHttpOpenRequest(connect, L"GET", L"/ws", NULL, NULL, NULL, 0);
    WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0);
    WinHttpSendRequest(request, NULL, 0, NULL, 0, 0, 0);
    WinHttpReceiveResponse(request, NULL);
    socket = WinHttpWebSocketCompleteUpgrade(request, 0);
    WinHttpCloseHandle(request);
    printf("CompleteUpgrade %d\n", socket != NULL);
    r.socket = socket;
    r.done = CreateEventW(NULL, TRUE, FALSE, NULL);
    r.start = GetTickCount();
    CreateThread(NULL, 0, receiver_thread, &r, 0, NULL);
    Sleep(500);
    start = GetTickCount();
    if (use_close)
    {
        err = WinHttpWebSocketClose(socket, 1000, (void *)"x", 1);
        printf("WebSocketClose %lu after %lu ms\n", err, GetTickCount() - start);
        err = WinHttpWebSocketQueryCloseStatus(socket, &code, reason, sizeof(reason), &len);
        printf("close status %lu code %u len %lu\n", err, code, len);
    }
    else
    {
        printf("CloseHandle %d after %lu ms\n", WinHttpCloseHandle(socket), GetTickCount() - start);
    }
    printf("receiver wait %lu\n", WaitForSingleObject(r.done, 5000));
    printf("receiver err %lu type %d read %lu after %lu ms\n", r.err, r.type, r.read, r.elapsed);
    if (use_close) WinHttpCloseHandle(socket);
    WaitForSingleObject(server_done, 5000);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
}

static HINTERNET open_socket(HINTERNET *session, HINTERNET *connect)
{
    HINTERNET request, socket;

    server_log[0] = 0;
    *session = WinHttpOpen(NULL, WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
    *connect = WinHttpConnect(*session, L"127.0.0.1", port, 0);
    request = WinHttpOpenRequest(*connect, L"GET", L"/ws", NULL, NULL, NULL, 0);
    WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0);
    WinHttpSendRequest(request, NULL, 0, NULL, 0, 0, 0);
    WinHttpReceiveResponse(request, NULL);
    socket = WinHttpWebSocketCompleteUpgrade(request, 0);
    WinHttpCloseHandle(request);
    if (!socket) printf("CompleteUpgrade failed %lu\n", GetLastError());
    return socket;
}

/* the server is done (it answered a close frame or timed out) before the handle goes */
static void finish(HINTERNET socket, HINTERNET session, HINTERNET connect)
{
    WaitForSingleObject(server_done, 5000);
    if (socket) WinHttpCloseHandle(socket);
    printf("  server saw %s\n", server_log);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
}

static void print_close_status(HINTERNET socket)
{
    BYTE reason[128];
    USHORT code = 0xdead;
    DWORD err, len = 0xdead;

    err = WinHttpWebSocketQueryCloseStatus(socket, &code, reason, sizeof(reason), &len);
    printf("  close status %lu code %u len %lu\n", err, code, len);
}

/* the server's close frame carries no status */
static void run_empty_received(BOOL use_close)
{
    HINTERNET session, connect, socket;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE type = 0xdead;
    BYTE data[64];
    DWORD err, read = 0xdead;

    printf("=== empty close frame received, then %s(1005)\n", use_close ? "close" : "shutdown");
    server_mode = 2;
    server_frames = 3;
    socket = open_socket(&session, &connect);
    err = WinHttpWebSocketReceive(socket, data, sizeof(data), &read, &type);
    printf("  receive %lu type %d read %lu\n", err, type, read);
    print_close_status(socket);
    if (use_close) err = WinHttpWebSocketClose(socket, 1005, NULL, 0);
    else err = WinHttpWebSocketShutdown(socket, 1005, NULL, 0);
    printf("  %s(1005) %lu\n", use_close ? "close" : "shutdown", err);
    print_close_status(socket);
    finish(socket, session, connect);
}

/* this side closes with 1005, the server answers with an empty close frame */
static void run_empty_sent(BOOL use_close)
{
    HINTERNET session, connect, socket;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE type = 0xdead;
    BYTE data[64];
    DWORD err, read = 0xdead;

    printf("=== %s(1005), empty close frame answered\n", use_close ? "close" : "shutdown");
    server_mode = 3;
    server_frames = 3;
    socket = open_socket(&session, &connect);
    if (use_close)
    {
        err = WinHttpWebSocketClose(socket, 1005, NULL, 0);
        printf("  close(1005) %lu\n", err);
    }
    else
    {
        err = WinHttpWebSocketShutdown(socket, 1005, NULL, 0);
        printf("  shutdown(1005) %lu\n", err);
        err = WinHttpWebSocketReceive(socket, data, sizeof(data), &read, &type);
        printf("  receive %lu type %d read %lu\n", err, type, read);
    }
    print_close_status(socket);
    finish(socket, session, connect);
}

/* which close statuses may be sent, and how */
static void run_status(BOOL use_close, USHORT status, const char *reason)
{
    HINTERNET session, connect, socket;
    DWORD err, len = reason ? strlen(reason) : 0;

    server_mode = 3;
    server_frames = 3;
    socket = open_socket(&session, &connect);
    if (use_close) err = WinHttpWebSocketClose(socket, status, (void *)reason, len);
    else err = WinHttpWebSocketShutdown(socket, status, (void *)reason, len);
    printf("=== %s(%u, %lu) %lu\n", use_close ? "close" : "shutdown", status, len, err);
    if (err)
    {
        err = WinHttpWebSocketSend(socket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, (void *)"s", 1);
        printf("  then send %lu\n", err);
        err = WinHttpWebSocketShutdown(socket, 1000, NULL, 0);
        printf("  then shutdown(1000) %lu\n", err);
    }
    finish(socket, session, connect);
}

/* what a close frame from the server is taken to say */
static void run_received(int index)
{
    HINTERNET session, connect, socket;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE type = 0xdead;
    BYTE data[64];
    DWORD err, read = 0xdead;

    printf("=== frame received:");
    for (err = 0; err < closes[index].len; err++) printf(" %02x", closes[index].data[err]);
    printf("\n");
    server_mode = 4;
    server_close = index;
    server_frames = 3;
    socket = open_socket(&session, &connect);
    err = WinHttpWebSocketReceive(socket, data, sizeof(data), &read, &type);
    printf("  receive %lu type %d read %lu\n", err, type, read);
    print_close_status(socket);
    err = WinHttpWebSocketReceive(socket, data, sizeof(data), &read, &type);
    printf("  receive again %lu type %d read %lu\n", err, type, read);
    err = WinHttpWebSocketSend(socket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, (void *)"s", 1);
    printf("  send %lu\n", err);
    err = WinHttpWebSocketShutdown(socket, 1000, NULL, 0);
    printf("  shutdown(1000) %lu\n", err);
    err = WinHttpWebSocketClose(socket, 1000, NULL, 0);
    printf("  close(1000) %lu\n", err);
    print_close_status(socket);
    finish(socket, session, connect);
}

/* which argument is looked at first */
static void run_arguments(void)
{
    HINTERNET session, connect, socket;
    char reason[200];
    DWORD err;

    memset(reason, 'x', sizeof(reason));
    printf("=== arguments\n");
    printf("  shutdown(NULL, 1000) %lu\n", WinHttpWebSocketShutdown(NULL, 1000, NULL, 0));
    printf("  shutdown(NULL, 999) %lu\n", WinHttpWebSocketShutdown(NULL, 999, NULL, 0));
    printf("  shutdown(NULL, 1000, NULL, 1) %lu\n", WinHttpWebSocketShutdown(NULL, 1000, NULL, 1));
    printf("  shutdown(NULL, 1000, 124) %lu\n", WinHttpWebSocketShutdown(NULL, 1000, reason, 124));
    printf("  close(NULL, 1000) %lu\n", WinHttpWebSocketClose(NULL, 1000, NULL, 0));
    printf("  close(NULL, 999) %lu\n", WinHttpWebSocketClose(NULL, 999, NULL, 0));
    printf("  close(NULL, 1000, NULL, 1) %lu\n", WinHttpWebSocketClose(NULL, 1000, NULL, 1));
    printf("  close(NULL, 1000, 124) %lu\n", WinHttpWebSocketClose(NULL, 1000, reason, 124));
    {
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
        USHORT code;
        DWORD read;
        printf("  send(NULL, NULL, 1) %lu\n", WinHttpWebSocketSend(NULL, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, NULL, 1));
        printf("  send(NULL, bad type) %lu\n", WinHttpWebSocketSend(NULL, 99, reason, 1));
        printf("  receive(NULL, NULL, 0) %lu\n", WinHttpWebSocketReceive(NULL, NULL, 0, &read, &type));
        printf("  receive(NULL, buf, 1, NULL) %lu\n", WinHttpWebSocketReceive(NULL, reason, 1, NULL, NULL));
        printf("  query(NULL, NULL) %lu\n", WinHttpWebSocketQueryCloseStatus(NULL, NULL, NULL, 0, NULL));
        printf("  query(NULL, code, NULL, 1) %lu\n", WinHttpWebSocketQueryCloseStatus(NULL, &code, NULL, 1, &read));
        printf("  completeupgrade(NULL) %p %lu\n", WinHttpWebSocketCompleteUpgrade(NULL, 0), GetLastError());
    }

    server_mode = 3;
    server_frames = 3;
    socket = open_socket(&session, &connect);
    printf("  shutdown(1000, 124) %lu\n", WinHttpWebSocketShutdown(socket, 1000, reason, 124));
    printf("  close(1000, 124) %lu\n", WinHttpWebSocketClose(socket, 1000, reason, 124));
    printf("  shutdown(1000, NULL, 1) %lu\n", WinHttpWebSocketShutdown(socket, 1000, NULL, 1));
    printf("  shutdown(1000, 123) %lu\n", (err = WinHttpWebSocketShutdown(socket, 1000, reason, 123)));
    printf("  shutdown again(999) %lu\n", WinHttpWebSocketShutdown(socket, 999, NULL, 0));
    printf("  shutdown again(1000) %lu\n", WinHttpWebSocketShutdown(socket, 1000, NULL, 0));
    finish(socket, session, connect);
}

int main(void)
{
    static const USHORT statuses[] = {0, 1, 999, 1000, 1001, 1002, 1003, 1004, 1006, 1007, 1008, 1009, 1010,
                                      1011, 1012, 1013, 1014, 1015, 1016, 2999, 3000, 4999, 5000, 65535};
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    start_server();
    run("no agent, headers", NULL, 0, L"X-Test: 1\r\nCookie: c=1\r\nSec-WebSocket-Protocol: chat", TRUE);
    server_frames = 1;
    run("agent", L"probe/1.0", 0, NULL, FALSE);
    run("no agent, refresh", NULL, WINHTTP_FLAG_REFRESH, NULL, FALSE);
    run("no agent, protocol only", NULL, 0, L"Sec-WebSocket-Protocol: chat", FALSE);
    server_silent = 1;
    server_frames = 3;
    run_blocked("close handle while receiving", FALSE);
    run_blocked("websocket close while receiving", TRUE);
    server_silent = 0;
    run_empty_received(FALSE);
    run_empty_received(TRUE);
    run_empty_sent(FALSE);
    run_empty_sent(TRUE);
    run_status(FALSE, 1005, "x");
    run_status(TRUE, 1005, "x");
    run_status(FALSE, 1000, NULL);
    for (i = 0; i < ARRAY_SIZE(statuses); i++) run_status(FALSE, statuses[i], "r");
    run_status(TRUE, 999, "r");
    run_status(TRUE, 1006, "r");
    run_status(TRUE, 5000, "r");
    run_arguments();
    for (i = 0; i < ARRAY_SIZE(closes); i++) run_received(i);
    printf("done\n");
    return 0;
}
