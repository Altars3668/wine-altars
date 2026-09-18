/* LISTEN_TIMEOUT 的原生/Wine 对照，不发送任何网络请求。 */
#include <windows.h>
#include <wininet.h>
#include <stdio.h>

static void probe(HINTERNET handle, const char *kind, DWORD value, DWORD size)
{
    DWORD buffer[2] = {value, 0};
    DWORD result = 0xdeadbeef, result_size = sizeof(result), error;
    BOOL ok;

    SetLastError(12345);
    ok = InternetSetOptionW(handle, INTERNET_OPTION_LISTEN_TIMEOUT, buffer, size);
    error = GetLastError();
    printf("%s set value=%lu size=%lu ok=%d error=%lu\n", kind, value, size, ok, error);
    SetLastError(12345);
    ok = InternetQueryOptionW(handle, INTERNET_OPTION_LISTEN_TIMEOUT, &result, &result_size);
    error = GetLastError();
    printf("%s query ok=%d error=%lu value=%lu size=%lu\n", kind, ok, error, result, result_size);
}

static void probe_null(HINTERNET handle, const char *kind, DWORD size)
{
    BOOL ok;
    DWORD error;

    SetLastError(12345);
    ok = InternetSetOptionW(handle, INTERNET_OPTION_LISTEN_TIMEOUT, NULL, size);
    error = GetLastError();
    printf("%s null size=%lu ok=%d error=%lu\n", kind, size, ok, error);
}

int main(void)
{
    HINTERNET session, connect, request;
    DWORD values[] = {0, 1, 3456, 0xffffffff};
    DWORD sizes[] = {0, 1, 3, 4, 8};
    unsigned int i;

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    setvbuf(stdout, NULL, _IONBF, 0);
    session = InternetOpenW(L"listen-option-probe", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!session) { printf("InternetOpen error=%lu\n", GetLastError()); return 1; }
    connect = InternetConnectW(session, L"127.0.0.1", 80, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!connect) { printf("InternetConnect error=%lu\n", GetLastError()); return 1; }
    request = HttpOpenRequestW(connect, L"GET", L"/", NULL, NULL, NULL, 0, 0);
    if (!request) { printf("HttpOpenRequest error=%lu\n", GetLastError()); return 1; }
    probe(NULL, "global", 3456, sizeof(DWORD));
    probe(session, "session", 3456, sizeof(DWORD));
    probe(connect, "connect", 3456, sizeof(DWORD));
    probe(request, "request", 3456, sizeof(DWORD));
    for (i = 0; i < sizeof(values) / sizeof(values[0]); i++)
        probe(session, "values", values[i], sizeof(DWORD));
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
        probe(session, "sizes", 3456, sizes[i]);
    probe_null(NULL, "global", 0);
    probe_null(NULL, "global", 4);
    probe_null(session, "session", 0);
    probe_null(session, "session", 4);
    probe((HINTERNET)(ULONG_PTR)0xdeadbeef, "invalid", 3456, 4);
    probe_null((HINTERNET)(ULONG_PTR)0xdeadbeef, "invalid", 4);
    InternetCloseHandle(request);
    InternetCloseHandle(connect);
    InternetCloseHandle(session);
    puts("probe-complete");
    return 0;
}
