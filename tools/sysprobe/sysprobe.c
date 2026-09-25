/*
 * sysprobe - what Windows answers to the small system calls Word makes as it
 * starts, which Wine answered from stubs: the DPI hosting behavior of threads
 * and windows, touch hit testing, message filters, a process's cycle time, a
 * thread's pending I/O, a token's UI access, the platform's power role, and
 * the session and power setting notifications.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define _WIN32_WINNT 0x0a00
#include <windows.h>
#include <winternl.h>
#include <powrprof.h>
#include <wtsapi32.h>
#include <stdio.h>

#ifndef NOTIFY_FOR_THIS_SESSION
#define NOTIFY_FOR_THIS_SESSION 0
#endif

#define GET(mod, name) ((void *)GetProcAddress(LoadLibraryA(mod), name))

static BOOL (WINAPI *pEnableNonClientDpiScaling)(HWND);

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE && GetPropW(hwnd, L"probe") == NULL && GetWindowTextLengthW(hwnd) == 0)
    {
        BOOL ret;
        SetLastError(0xdeadbeef);
        ret = pEnableNonClientDpiScaling(hwnd);
        printf("EnableNonClientDpiScaling in WM_NCCREATE %d error %lu\n", ret, ret ? 0 : GetLastError());
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int main(void)
{
    int (WINAPI *pSetThreadDpiHostingBehavior)(int) = GET("user32.dll", "SetThreadDpiHostingBehavior");
    int (WINAPI *pGetThreadDpiHostingBehavior)(void) = GET("user32.dll", "GetThreadDpiHostingBehavior");
    int (WINAPI *pGetWindowDpiHostingBehavior)(HWND) = GET("user32.dll", "GetWindowDpiHostingBehavior");
    BOOL (WINAPI *pRegisterTouchHitTestingWindow)(HWND, ULONG) = GET("user32.dll", "RegisterTouchHitTestingWindow");
    BOOL (WINAPI *pChangeWindowMessageFilterEx)(HWND, UINT, DWORD, void *) = GET("user32.dll", "ChangeWindowMessageFilterEx");
    BOOL (WINAPI *pQueryProcessCycleTime)(HANDLE, ULONG64 *) = GET("kernel32.dll", "QueryProcessCycleTime");
    BOOL (WINAPI *pQueryThreadCycleTime)(HANDLE, ULONG64 *) = GET("kernel32.dll", "QueryThreadCycleTime");
    NTSTATUS (WINAPI *pNtQueryInformationProcess)(HANDLE, int, void *, ULONG, ULONG *) = GET("ntdll.dll", "NtQueryInformationProcess");
    NTSTATUS (WINAPI *pNtQueryInformationThread)(HANDLE, int, void *, ULONG, ULONG *) = GET("ntdll.dll", "NtQueryInformationThread");
    ULONG (WINAPI *pPowerDeterminePlatformRoleEx)(ULONG) = GET("powrprof.dll", "PowerDeterminePlatformRoleEx");
    BOOL (WINAPI *pWTSRegisterSessionNotification)(HWND, DWORD) = GET("wtsapi32.dll", "WTSRegisterSessionNotification");
    BOOL (WINAPI *pWTSUnRegisterSessionNotification)(HWND) = GET("wtsapi32.dll", "WTSUnRegisterSessionNotification");
    struct { DWORD cbSize; DWORD ExtStatus; } filter;
    WNDCLASSW cls = {0};
    HWND hwnd, child;
    ULONG64 cycles, cycles2;
    ULONG len, value;
    NTSTATUS status;
    HANDLE token;
    DWORD size;
    BOOL ret;
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    pEnableNonClientDpiScaling = GET("user32.dll", "EnableNonClientDpiScaling");
    cls.lpfnWndProc = wndproc;
    cls.lpszClassName = L"sysprobe";
    RegisterClassW(&cls);

    printf("GetThreadDpiHostingBehavior %d\n", pGetThreadDpiHostingBehavior());
    hwnd = CreateWindowW(L"sysprobe", L"default", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
    /* each call a statement of its own: x64 evaluates arguments right to left, so a GetLastError() beside the call
     * in one argument list reads the error from before it */
    ret = pSetThreadDpiHostingBehavior(1);
    printf("SetThreadDpiHostingBehavior(1) %d, now %d\n", ret, pGetThreadDpiHostingBehavior());
    child = CreateWindowW(L"sysprobe", L"mixed", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
    ret = pSetThreadDpiHostingBehavior(0);
    printf("SetThreadDpiHostingBehavior(0) %d, now %d\n", ret, pGetThreadDpiHostingBehavior());
    for (i = -1; i <= 2; i += 3)
    {
        DWORD error;

        SetLastError(0xdeadbeef);
        ret = pSetThreadDpiHostingBehavior(i);
        error = GetLastError();
        printf("SetThreadDpiHostingBehavior(%d) %d error %lu, now %d\n", i, ret, error, pGetThreadDpiHostingBehavior());
    }
    printf("GetWindowDpiHostingBehavior: default window %d, mixed window %d, NULL %d, desktop %d\n",
           pGetWindowDpiHostingBehavior(hwnd), pGetWindowDpiHostingBehavior(child), pGetWindowDpiHostingBehavior(NULL),
           pGetWindowDpiHostingBehavior(GetDesktopWindow()));

    for (i = 0; i <= 3; i++)
    {
        SetLastError(0xdeadbeef);
        ret = pRegisterTouchHitTestingWindow(hwnd, i);
        printf("RegisterTouchHitTestingWindow(window, %d) %d error %lu\n", i, ret, ret ? 0 : GetLastError());
    }
    SetLastError(0xdeadbeef);
    ret = pRegisterTouchHitTestingWindow(NULL, 1);
    printf("RegisterTouchHitTestingWindow(NULL, 1) %d error %lu\n", ret, ret ? 0 : GetLastError());
    SetLastError(0xdeadbeef);
    ret = pRegisterTouchHitTestingWindow(GetDesktopWindow(), 1);
    printf("RegisterTouchHitTestingWindow(desktop, 1) %d error %lu\n", ret, ret ? 0 : GetLastError());

    for (i = 0; i <= 4; i++)
    {
        filter.cbSize = sizeof(filter);
        filter.ExtStatus = 0xdead;
        SetLastError(0xdeadbeef);
        ret = pChangeWindowMessageFilterEx(hwnd, 0xc078, i, &filter);
        printf("ChangeWindowMessageFilterEx(window, 0xc078, action %d) %d error %lu ext %lu\n", i, ret,
               ret ? 0 : GetLastError(), filter.ExtStatus);
    }
    SetLastError(0xdeadbeef);
    ret = pChangeWindowMessageFilterEx(hwnd, WM_COPYDATA, 1, NULL);
    printf("ChangeWindowMessageFilterEx(window, WM_COPYDATA, allow, NULL) %d error %lu\n", ret, ret ? 0 : GetLastError());
    filter.cbSize = 4;
    SetLastError(0xdeadbeef);
    ret = pChangeWindowMessageFilterEx(hwnd, WM_COPYDATA, 1, &filter);
    printf("ChangeWindowMessageFilterEx(window, WM_COPYDATA, allow, small) %d error %lu\n", ret, ret ? 0 : GetLastError());
    filter.cbSize = sizeof(filter);
    SetLastError(0xdeadbeef);
    ret = pChangeWindowMessageFilterEx(NULL, WM_COPYDATA, 1, &filter);
    printf("ChangeWindowMessageFilterEx(NULL, WM_COPYDATA, allow) %d error %lu\n", ret, ret ? 0 : GetLastError());

    SetLastError(0xdeadbeef);
    ret = pEnableNonClientDpiScaling(hwnd);
    printf("EnableNonClientDpiScaling(window) %d error %lu\n", ret, ret ? 0 : GetLastError());
    DestroyWindow(CreateWindowW(L"sysprobe", NULL, WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, NULL, NULL));
    {
        DPI_AWARENESS_CONTEXT (WINAPI *pSetThreadDpiAwarenessContext)(DPI_AWARENESS_CONTEXT) =
            GET("user32.dll", "SetThreadDpiAwarenessContext");
        DPI_AWARENESS_CONTEXT old = pSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
        HWND pm;

        printf("per monitor aware:\n");
        pm = CreateWindowW(L"sysprobe", NULL, WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
        SetLastError(0xdeadbeef);
        ret = pEnableNonClientDpiScaling(pm);
        printf("EnableNonClientDpiScaling after creation %d error %lu\n", ret, ret ? 0 : GetLastError());
        DestroyWindow(pm);
        pSetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        printf("per monitor aware v2:\n");
        DestroyWindow(CreateWindowW(L"sysprobe", NULL, WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, NULL, NULL));
        pSetThreadDpiAwarenessContext(old);
    }
    filter.cbSize = sizeof(filter);
    filter.ExtStatus = 0xdead;
    SetLastError(0xdeadbeef);
    ret = pChangeWindowMessageFilterEx(hwnd, 0, 0, &filter);
    printf("ChangeWindowMessageFilterEx(window, 0, reset) %d error %lu ext %lu\n", ret, ret ? 0 : GetLastError(),
           filter.ExtStatus);
    SetLastError(0xdeadbeef);
    ret = pChangeWindowMessageFilterEx(hwnd, WM_COPYDATA, 2, &filter);
    printf("ChangeWindowMessageFilterEx(window, WM_COPYDATA, disallow) %d error %lu ext %lu\n", ret,
           ret ? 0 : GetLastError(), filter.ExtStatus);
    SetLastError(0xdeadbeef);
    ret = pChangeWindowMessageFilterEx(GetDesktopWindow(), WM_COPYDATA, 1, &filter);
    printf("ChangeWindowMessageFilterEx(desktop, WM_COPYDATA, allow) %d error %lu ext %lu\n", ret,
           ret ? 0 : GetLastError(), filter.ExtStatus);

    ret = pQueryProcessCycleTime(GetCurrentProcess(), &cycles);
    for (i = 0; i < 50000000; i++) cycles2 = i;
    pQueryProcessCycleTime(GetCurrentProcess(), &cycles2);
    printf("QueryProcessCycleTime %d, grows %d\n", ret, cycles2 > cycles);
    {
        ULONG64 info[2] = {0xdead, 0xdead};
        LARGE_INTEGER freq;
        FILETIME a, b, kernel, user;

        status = pNtQueryInformationProcess(GetCurrentProcess(), 38 /* ProcessCycleTime */, info, 16, &len);
        printf("ProcessCycleTime status %#lx len %lu, accumulated %s, current %s\n", status, len,
               info[0] ? "nonzero" : "0", info[1] ? "nonzero" : "0");
        status = pNtQueryInformationProcess(GetCurrentProcess(), 38, info, 8, &len);
        printf("ProcessCycleTime, 8 bytes: status %#lx\n", status);
        status = pNtQueryInformationThread(GetCurrentThread(), 23 /* ThreadCycleTime */, info, 16, &len);
        printf("ThreadCycleTime status %#lx len %lu, accumulated %s, current %s\n", status, len,
               info[0] ? "nonzero" : "0", info[1] ? "nonzero" : "0");
        ret = pQueryThreadCycleTime(GetCurrentThread(), &cycles);
        GetThreadTimes(GetCurrentThread(), &a, &b, &kernel, &user);
        QueryPerformanceFrequency(&freq);
        printf("QueryThreadCycleTime %d; cycles per 100ns of thread time %.1f\n", ret,
               (double)cycles / (((ULONGLONG)kernel.dwHighDateTime << 32 | kernel.dwLowDateTime) +
                                 ((ULONGLONG)user.dwHighDateTime << 32 | user.dwLowDateTime)));
    }

    value = 0xdead;
    status = pNtQueryInformationThread(GetCurrentThread(), 16 /* ThreadIsIoPending */, &value, sizeof(value), &len);
    printf("ThreadIsIoPending status %#lx value %lu len %lu\n", status, value, len);
    status = pNtQueryInformationThread(GetCurrentThread(), 16, &value, 1, &len);
    printf("ThreadIsIoPending, 1 byte: status %#lx\n", status);

    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token);
    value = 0xdead;
    ret = GetTokenInformation(token, TokenUIAccess, &value, sizeof(value), &size);
    printf("TokenUIAccess %d value %lu size %lu\n", ret, value, size);
    CloseHandle(token);

    for (i = 1; i <= 2; i++)
        printf("PowerDeterminePlatformRoleEx(%d) %lu\n", i, pPowerDeterminePlatformRoleEx(i));
    printf("PowerDeterminePlatformRoleEx(3) %lu\n", pPowerDeterminePlatformRoleEx(3));
    printf("PowerDeterminePlatformRole %d\n", PowerDeterminePlatformRole());

    SetLastError(0xdeadbeef);
    ret = pWTSRegisterSessionNotification(hwnd, NOTIFY_FOR_THIS_SESSION);
    printf("WTSRegisterSessionNotification %d error %lu\n", ret, ret ? 0 : GetLastError());
    SetLastError(0xdeadbeef);
    ret = pWTSRegisterSessionNotification(hwnd, 7);
    printf("WTSRegisterSessionNotification(flags 7) %d error %lu\n", ret, ret ? 0 : GetLastError());
    SetLastError(0xdeadbeef);
    ret = pWTSRegisterSessionNotification(NULL, NOTIFY_FOR_THIS_SESSION);
    printf("WTSRegisterSessionNotification(NULL) %d error %lu\n", ret, ret ? 0 : GetLastError());
    ret = pWTSUnRegisterSessionNotification(hwnd);
    printf("WTSUnRegisterSessionNotification %d\n", ret);

    {
        static const GUID GUID_ACDC_POWER_SOURCE_ = {0x5d3e9a59, 0xe9d5, 0x4b00, {0xa6, 0xbd, 0xff, 0x34, 0xff, 0x51, 0x65, 0x48}};
        HPOWERNOTIFY notify;

        SetLastError(0xdeadbeef);
        notify = RegisterPowerSettingNotification(hwnd, &GUID_ACDC_POWER_SOURCE_, DEVICE_NOTIFY_WINDOW_HANDLE);
        printf("RegisterPowerSettingNotification %p error %lu\n", notify, notify ? 0 : GetLastError());
        if (notify) printf("UnregisterPowerSettingNotification %d\n", UnregisterPowerSettingNotification(notify));
    }

    DestroyWindow(child);
    DestroyWindow(hwnd);
    printf("done\n");
    return 0;
}
