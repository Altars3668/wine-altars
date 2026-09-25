/*
 * dpihostprobe - the last errors sysprobe could not see.
 *
 * sysprobe read GetLastError() in the same argument list as the call it
 * reported on, and x64 evaluates arguments right to left, so it printed the
 * error from before the call.  This asks again, one statement at a time:
 * what SetThreadDpiHostingBehavior leaves as the last error for values it
 * refuses, and what EnableNonClientDpiScaling says for a system aware thread;
 * and the DPI awareness contexts of windows, and of their thread in
 * WM_NCCREATE, for each awareness.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define _WIN32_WINNT 0x0a00
#include <windows.h>
#include <stdio.h>

static BOOL (WINAPI *pEnableNonClientDpiScaling)(HWND);
static DPI_AWARENESS_CONTEXT (WINAPI *pGetThreadDpiAwarenessContext)(void);
static BOOL nc_ret;
static DWORD nc_error;
static DPI_AWARENESS_CONTEXT nc_context;

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE)
    {
        SetLastError(0xdeadbeef);
        nc_ret = pEnableNonClientDpiScaling(hwnd);
        nc_error = GetLastError();
        nc_context = pGetThreadDpiAwarenessContext();
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int main(void)
{
    HMODULE user32 = GetModuleHandleA("user32.dll");
    int (WINAPI *pSetThreadDpiHostingBehavior)(int) = (void *)GetProcAddress(user32, "SetThreadDpiHostingBehavior");
    int (WINAPI *pGetThreadDpiHostingBehavior)(void) = (void *)GetProcAddress(user32, "GetThreadDpiHostingBehavior");
    DPI_AWARENESS_CONTEXT (WINAPI *pSetThreadDpiAwarenessContext)(DPI_AWARENESS_CONTEXT) =
        (void *)GetProcAddress(user32, "SetThreadDpiAwarenessContext");
    static const DPI_AWARENESS_CONTEXT contexts[] = {DPI_AWARENESS_CONTEXT_UNAWARE, DPI_AWARENESS_CONTEXT_SYSTEM_AWARE,
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2,
        DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED};
    static const char *names[] = {"unaware", "system aware", "per monitor aware", "per monitor aware v2",
        "unaware, GDI scaled"};
    WNDCLASSW cls = {0};
    DWORD error;
    HWND hwnd;
    int i, ret, now;
    BOOL b;

    setvbuf(stdout, NULL, _IONBF, 0);
    pEnableNonClientDpiScaling = (void *)GetProcAddress(user32, "EnableNonClientDpiScaling");
    pGetThreadDpiAwarenessContext = (void *)GetProcAddress(user32, "GetThreadDpiAwarenessContext");
    for (i = -1; i <= 3; i++)
    {
        SetLastError(0xdeadbeef);
        ret = pSetThreadDpiHostingBehavior(i);
        error = GetLastError();
        now = pGetThreadDpiHostingBehavior();
        printf("SetThreadDpiHostingBehavior(%d) %d error %lu, now %d\n", i, ret, error, now);
    }
    pSetThreadDpiHostingBehavior(0);

    cls.lpfnWndProc = wndproc;
    cls.lpszClassName = L"dpihostprobe";
    RegisterClassW(&cls);
    for (i = 0; i < ARRAYSIZE(contexts); i++)
    {
        DPI_AWARENESS_CONTEXT old = pSetThreadDpiAwarenessContext(contexts[i]);
        nc_ret = 2;
        hwnd = CreateWindowW(L"dpihostprobe", NULL, WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
        printf("%s: EnableNonClientDpiScaling in WM_NCCREATE %d error %lu", names[i], nc_ret, nc_error);
        SetLastError(0xdeadbeef);
        b = pEnableNonClientDpiScaling(hwnd);
        error = GetLastError();
        printf(", after %d error %lu\n", b, error);
        {
            DPI_AWARENESS_CONTEXT (WINAPI *pGetWindowDpiAwarenessContext)(HWND) =
                (void *)GetProcAddress(user32, "GetWindowDpiAwarenessContext");
            BOOL (WINAPI *pAreDpiAwarenessContextsEqual)(DPI_AWARENESS_CONTEXT, DPI_AWARENESS_CONTEXT) =
                (void *)GetProcAddress(user32, "AreDpiAwarenessContextsEqual");
            DPI_AWARENESS_CONTEXT window = pGetWindowDpiAwarenessContext(hwnd);
            HWND child = CreateWindowW(L"static", NULL, WS_CHILD, 0, 0, 10, 10, hwnd, NULL, NULL, NULL);

            printf("  thread context in WM_NCCREATE %p, window context %p, equal to the thread's %d, "
                   "a child's %p\n", nc_context, window, pAreDpiAwarenessContextsEqual(window, contexts[i]),
                   pGetWindowDpiAwarenessContext(child));
        }
        DestroyWindow(hwnd);
        pSetThreadDpiAwarenessContext(old);
    }
    SetLastError(0xdeadbeef);
    b = pEnableNonClientDpiScaling(NULL);
    error = GetLastError();
    printf("EnableNonClientDpiScaling(NULL) %d error %lu\n", b, error);
    printf("done\n");
    return 0;
}
