/* What WinINet's cache makes of a response's Expires header when it is not a date.
 *
 * Servers send "Expires: -1" to mean "already expired" (ASP.NET does by default), and RFC 7234 5.3
 * has a cache take any invalid date that way.  Wine's wininet only takes "0" so; any other value it
 * cannot parse leaves the entry with its default of ten minutes, and a second request within them
 * is answered from the cache.  A small HTTP server on 127.0.0.1 in this process answers each path
 * with one kind of header; for each the probe fetches the URL twice through InternetOpenUrl with
 * the default caching and prints the cache entry's expiry relative to now, whether it counts as
 * expired, and whether the second fetch reached the server.  Each entry is removed afterwards.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror wininetexpprobe.c -o wininetexpprobe.exe -lwininet -lws2_32
 */
#include <winsock2.h>
#include <windows.h>
#include <wininet.h>
#include <stdio.h>

static const struct { const char *name, *header; } cases[] =
{
    {"minus-one", "Expires: -1\r\n"},
    {"zero", "Expires: 0\r\n"},
    {"garbage", "Expires: soon\r\n"},
    {"empty", "Expires: \r\n"},
    {"past", "Expires: Thu, 01 Jan 1998 00:00:00 GMT\r\n"},
    {"future", "Expires: Fri, 01 Jan 2038 00:00:00 GMT\r\n"},
    {"none", ""},
    {"max-age-0", "Cache-Control: max-age=0\r\n"},
    {"max-age-0-and-future", "Cache-Control: max-age=0\r\nExpires: Fri, 01 Jan 2038 00:00:00 GMT\r\n"},
    {"minus-one-and-max-age", "Cache-Control: max-age=3600\r\nExpires: -1\r\n"},
};

/* exported, but not in the SDK's header or import library */
static BOOL (WINAPI *pIsUrlCacheEntryExpiredA)(const char *, DWORD, FILETIME *);

static SOCKET server;
static LONG requests[ARRAYSIZE(cases)];

static DWORD WINAPI serve(void *arg)
{
    char buf[2048], reply[512];
    SOCKET c;
    int len, i;

    (void)arg;
    while ((c = accept(server, NULL, NULL)) != INVALID_SOCKET)
    {
        len = recv(c, buf, sizeof(buf) - 1, 0);
        buf[len > 0 ? len : 0] = 0;
        for (i = 0; i < (int)ARRAYSIZE(cases); i++)
        {
            char path[64];
            sprintf(path, "/%s ", cases[i].name);
            if (strstr(buf, path)) break;
        }
        if (i < (int)ARRAYSIZE(cases))
        {
            InterlockedIncrement(&requests[i]);
            len = sprintf(reply, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 5\r\n%sConnection: close\r\n\r\nhello",
                          cases[i].header);
        }
        else len = sprintf(reply, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        send(c, reply, len, 0);
        shutdown(c, SD_SEND);
        closesocket(c);
    }
    return 0;
}

static void fetch(HINTERNET session, const char *url)
{
    HINTERNET h = InternetOpenUrlA(session, url, NULL, 0, 0, 0);
    char buf[64];
    DWORD got;

    if (!h)
    {
        printf("    InternetOpenUrl failed %lu\n", GetLastError());
        return;
    }
    while (InternetReadFile(h, buf, sizeof(buf), &got) && got) ;
    InternetCloseHandle(h);
}

static LONGLONG seconds_from_now(FILETIME ft)
{
    FILETIME now;
    ULARGE_INTEGER a, b;

    GetSystemTimeAsFileTime(&now);
    a.LowPart = ft.dwLowDateTime; a.HighPart = ft.dwHighDateTime;
    b.LowPart = now.dwLowDateTime; b.HighPart = now.dwHighDateTime;
    return ((LONGLONG)a.QuadPart - (LONGLONG)b.QuadPart) / 10000000;
}

int main(void)
{
    struct sockaddr_in addr = {0};
    int addrlen = sizeof(addr), i;
    HINTERNET session;
    WSADATA wsa;
    char url[128];

    pIsUrlCacheEntryExpiredA = (void *)GetProcAddress(LoadLibraryA("wininet.dll"), "IsUrlCacheEntryExpiredA");
    WSAStartup(MAKEWORD(2, 2), &wsa);
    server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(server, (struct sockaddr *)&addr, sizeof(addr));
    getsockname(server, (struct sockaddr *)&addr, &addrlen);
    listen(server, 8);
    CloseHandle(CreateThread(NULL, 0, serve, NULL, 0, NULL));

    session = InternetOpenA("wininetexpprobe", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    for (i = 0; i < (int)ARRAYSIZE(cases); i++)
    {
        BYTE info_buf[4096];
        INTERNET_CACHE_ENTRY_INFOA *info = (INTERNET_CACHE_ENTRY_INFOA *)info_buf;
        DWORD size = sizeof(info_buf);

        sprintf(url, "http://127.0.0.1:%u/%s", ntohs(addr.sin_port), cases[i].name);
        DeleteUrlCacheEntryA(url);
        fetch(session, url);
        printf("%s:", cases[i].name);
        if (GetUrlCacheEntryInfoA(url, info, &size))
        {
            if (!info->ExpireTime.dwLowDateTime && !info->ExpireTime.dwHighDateTime)
                printf(" expires 0");
            else
                printf(" expires %+lld s", seconds_from_now(info->ExpireTime));
            printf(", expired %d, type %#lx", pIsUrlCacheEntryExpiredA(url, 0, NULL), info->CacheEntryType);
        }
        else printf(" no cache entry (%lu)", GetLastError());
        fetch(session, url);
        printf(", second fetch %s\n", requests[i] > 1 ? "went to the server" : "came from the cache");
        DeleteUrlCacheEntryA(url);
    }
    InternetCloseHandle(session);
    closesocket(server);
    return 0;
}
