/* 不连接网络，只测试选项日志有无改变返回值或泄露缓冲区。 */
#include <windows.h>
#include <stdio.h>

typedef HANDLE (WINAPI *open_fn)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
typedef BOOL (WINAPI *set_fn)(HANDLE, DWORD, void *, DWORD);
typedef BOOL (WINAPI *query_fn)(HANDLE, DWORD, void *, DWORD *);
typedef BOOL (WINAPI *close_fn)(HANDLE);

static unsigned int failures;

static void exercise(const char *dll_name, const char *open_name,
                     const char *set_name, const char *query_name,
                     const char *close_name, DWORD timeout_option)
{
    HMODULE dll = LoadLibraryA(dll_name);
    open_fn open_session;
    set_fn set;
    query_fn query;
    close_fn close;
    HANDLE session;
    DWORD value = 3456, size, error;
    BOOL ok;
    char sentinel[] = "SENSITIVE_OPTION_BUFFER_MUST_NOT_BE_LOGGED";

    if (!dll) { failures++; return; }
    open_session = (open_fn)(void *)GetProcAddress(dll, open_name);
    set = (set_fn)(void *)GetProcAddress(dll, set_name);
    query = (query_fn)(void *)GetProcAddress(dll, query_name);
    close = (close_fn)(void *)GetProcAddress(dll, close_name);
    if (!open_session || !set || !query || !close) { failures++; FreeLibrary(dll); return; }
    session = open_session(L"http-option-selftest", 1, NULL, NULL, 0);
    if (!session) { failures++; FreeLibrary(dll); return; }

    SetLastError(12345);
    ok = set(session, timeout_option, &value, sizeof(value));
    error = GetLastError();
    printf("%s set-valid ok=%d error=%lu\n", dll_name, ok, error);
    if (!ok) failures++;
    value = 0; size = sizeof(value);
    SetLastError(12345);
    ok = query(session, timeout_option, &value, &size);
    error = GetLastError();
    printf("%s query-valid ok=%d error=%lu value=%lu\n", dll_name, ok, error, value);
    if (!ok || value != 3456) failures++;

    SetLastError(12345);
    ok = set(session, 0x7ffffffe, sentinel, sizeof(sentinel));
    error = GetLastError();
    printf("%s set-invalid ok=%d error=%lu\n", dll_name, ok, error);
    if (ok || error != 12009) failures++;
    size = sizeof(sentinel);
    SetLastError(12345);
    ok = query(session, 0x7ffffffe, sentinel, &size);
    error = GetLastError();
    printf("%s query-invalid ok=%d error=%lu\n", dll_name, ok, error);
    /* 此处验证 Wine 诊断开关不改变原行为，不把两套 API 的错误码等同。 */
    if (ok || error != (timeout_option == 2 ? 12018 : 87)) failures++;
    close(session);
    FreeLibrary(dll);
}

int main(void)
{
    exercise("wininet.dll", "InternetOpenW", "InternetSetOptionW",
             "InternetQueryOptionW", "InternetCloseHandle", 2);
    exercise("winhttp.dll", "WinHttpOpen", "WinHttpSetOption",
             "WinHttpQueryOption", "WinHttpCloseHandle", 3);
    printf("failures=%u\n", failures);
    return failures ? 1 : 0;
}
