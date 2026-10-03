/* misc2: what misc left -- the input scope TF_GetInputScope gives for a window after SetInputScope, SetInputScopes
 * and SetInputScopeXML (the scopes, phrases, regular expression, SRGS and XML), for a window with none, one of another
 * thread and one destroyed; and LoadLibraryEx with LOAD_LIBRARY_REQUIRE_SIGNED_TARGET for DLLs with an embedded
 * Microsoft signature (the C runtime's), by name and by path, with the search flags Office adds.  Prints results
 * only. */
#define COBJMACROS
#include <windows.h>
#include <msctf.h>
#include <inputscope.h>
#include <stdio.h>

static void *proc(const WCHAR *dll, const char *name)
{
    HMODULE module = LoadLibraryW(dll);
    return module ? (void *)GetProcAddress(module, name) : NULL;
}

static HRESULT (WINAPI *pTF_GetInputScope)(HWND, ITfInputScope **);

static void show_scope(const char *what, HWND hwnd)
{
    ITfInputScope *scope = NULL;
    InputScope *scopes = NULL;
    BSTR *phrases = NULL, str;
    UINT count = 0xdead, i;
    HRESULT hr;

    hr = pTF_GetInputScope(hwnd, &scope);
    printf("%s: TF_GetInputScope %#lx scope %s", what, hr, scope ? "set" : "NULL");
    if (!scope)
    {
        printf("\n");
        return;
    }
    hr = ITfInputScope_GetInputScopes(scope, &scopes, &count);
    printf(", scopes %#lx", hr);
    if (SUCCEEDED(hr))
    {
        printf(" [");
        for (i = 0; i < count; i++) printf("%s%d", i ? " " : "", scopes[i]);
        printf("]");
        CoTaskMemFree(scopes);
    }
    count = 0xdead;
    hr = ITfInputScope_GetPhrase(scope, &phrases, &count);
    printf(", phrases %#lx", hr);
    if (SUCCEEDED(hr))
    {
        printf(" [");
        for (i = 0; i < count; i++)
        {
            printf("%s%ls", i ? "|" : "", phrases[i]);
            SysFreeString(phrases[i]);
        }
        printf("]");
        CoTaskMemFree(phrases);
    }
    str = (BSTR)0xdeadbeef;
    hr = ITfInputScope_GetRegularExpression(scope, &str);
    printf(", regexp %#lx %s", hr, str == (BSTR)0xdeadbeef ? "untouched" : str ? "" : "NULL");
    if (str && str != (BSTR)0xdeadbeef)
    {
        printf("[%ls]", str);
        SysFreeString(str);
    }
    str = (BSTR)0xdeadbeef;
    hr = ITfInputScope_GetSRGS(scope, &str);
    printf(", srgs %#lx %s", hr, str == (BSTR)0xdeadbeef ? "untouched" : str ? "" : "NULL");
    if (str && str != (BSTR)0xdeadbeef)
    {
        printf("[%ls]", str);
        SysFreeString(str);
    }
    str = (BSTR)0xdeadbeef;
    hr = ITfInputScope_GetXML(scope, &str);
    printf(", xml %#lx %s", hr, str == (BSTR)0xdeadbeef ? "untouched" : str ? "" : "NULL");
    if (str && str != (BSTR)0xdeadbeef)
    {
        printf("[%ls]", str);
        SysFreeString(str);
    }
    printf("\n");
    ITfInputScope_Release(scope);
}

static HWND other_thread_window;

static DWORD WINAPI window_thread(void *arg)
{
    MSG msg;

    other_thread_window = CreateWindowExW(0, L"static", L"other", WS_POPUP, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
    SetEvent(arg);
    while (GetMessageW(&msg, NULL, 0, 0)) DispatchMessageW(&msg);
    DestroyWindow(other_thread_window);
    return 0;
}

static void input_scopes(void)
{
    HRESULT (WINAPI *pSetInputScope)(HWND, InputScope) = proc(L"msctf.dll", "SetInputScope");
    HRESULT (WINAPI *pSetInputScopes)(HWND, const InputScope *, UINT, WCHAR **, UINT, WCHAR *, WCHAR *) =
            proc(L"msctf.dll", "SetInputScopes");
    HRESULT (WINAPI *pSetInputScopeXML)(HWND, WCHAR *) = proc(L"msctf.dll", "SetInputScopeXML");
    static const InputScope two[] = { IS_URL, IS_DIGITS };
    WCHAR *phrases[] = { (WCHAR *)L"one", (WCHAR *)L"two" };
    HWND hwnd = CreateWindowExW(0, L"static", L"scopes", WS_POPUP, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
    HANDLE event = CreateEventW(NULL, FALSE, FALSE, NULL), thread;
    HRESULT hr;

    pTF_GetInputScope = proc(L"msctf.dll", "TF_GetInputScope");
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    show_scope("a window with none", hwnd);
    hr = pSetInputScope(hwnd, IS_URL);
    printf("SetInputScope IS_URL %#lx\n", hr);
    show_scope("  then", hwnd);
    hr = pSetInputScope(hwnd, IS_DEFAULT);
    printf("SetInputScope IS_DEFAULT %#lx\n", hr);
    show_scope("  then", hwnd);
    hr = pSetInputScopes(hwnd, two, 2, phrases, 2, (WCHAR *)L"[0-9]+", (WCHAR *)L"<grammar/>");
    printf("SetInputScopes two, two phrases, a regular expression and SRGS %#lx\n", hr);
    show_scope("  then", hwnd);
    hr = pSetInputScopes(hwnd, NULL, 0, NULL, 0, NULL, NULL);
    printf("SetInputScopes none %#lx\n", hr);
    show_scope("  then", hwnd);
    hr = pSetInputScopeXML(hwnd, (WCHAR *)L"<input-scope/>");
    printf("SetInputScopeXML %#lx\n", hr);
    show_scope("  then", hwnd);
    hr = pSetInputScopeXML(hwnd, NULL);
    printf("SetInputScopeXML NULL %#lx\n", hr);
    show_scope("  then", hwnd);
    hr = pSetInputScopeXML(NULL, (WCHAR *)L"<input-scope/>");
    printf("SetInputScopeXML no window %#lx\n", hr);
    hr = pSetInputScopes(NULL, two, 2, NULL, 0, NULL, NULL);
    printf("SetInputScopes no window %#lx\n", hr);
    hr = pSetInputScopes(hwnd, NULL, 2, NULL, 0, NULL, NULL);
    printf("SetInputScopes two, no array %#lx\n", hr);
    show_scope("  then", hwnd);

    thread = CreateThread(NULL, 0, window_thread, event, 0, NULL);
    WaitForSingleObject(event, 5000);
    hr = pSetInputScope(other_thread_window, IS_NUMBER);
    printf("SetInputScope, a window of another thread %#lx\n", hr);
    show_scope("  then", other_thread_window);
    PostThreadMessageW(GetThreadId(thread), WM_QUIT, 0, 0);
    WaitForSingleObject(thread, 5000);
    CloseHandle(thread);
    show_scope("that window destroyed", other_thread_window);
    show_scope("no window", NULL);
    show_scope("the desktop", GetDesktopWindow());
    {
        HRESULT hr2 = pTF_GetInputScope(hwnd, NULL);
        printf("TF_GetInputScope NULL: %#lx\n", hr2);
    }
    DestroyWindow(hwnd);
    show_scope("our window destroyed", hwnd);
    CoUninitialize();
    CloseHandle(event);
}

static void signed_loads(void)
{
    static const struct { const WCHAR *name; DWORD flags; } tries[] =
    {
        { L"vcruntime140.dll", 0x80 },
        { L"msvcp140.dll", 0x1f80 },
        { L"C:\\Windows\\System32\\concrt140.dll", 0x80 },
        { L"C:\\Windows\\System32\\vcruntime140_1.dll", 0x1f80 },
        { L"ucrtbase.dll", 0x1f80 },
        { L"C:\\Windows\\System32\\winhttp.dll", 0x80 },
    };
    unsigned int i;

    for (i = 0; i < ARRAYSIZE(tries); i++)
    {
        HMODULE before = GetModuleHandleW(tries[i].name), module;

        SetLastError(0xdeadbeef);
        module = LoadLibraryExW(tries[i].name, NULL, tries[i].flags);
        printf("LoadLibraryEx %ls flags %#lx (%s before): %s error %lu\n", tries[i].name, tries[i].flags,
               before ? "loaded" : "not loaded", module ? "loaded" : "failed", module ? 0 : GetLastError());
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    input_scopes();
    signed_loads();
    return 0;
}
