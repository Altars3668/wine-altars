/* sessionprobe -- ask the system directly whether this looks like an
 * interactive desktop session or a "headless" one, with no debugging and no
 * Office involved at all.
 *
 * Written to test a specific hypothesis from the side, cheaply: Office's own
 * log strings call a licensing code path "Headless LVUX" (LVUX = Licensing
 * Validation UX). The obvious reading is a debugger-detection check, and
 * that reading led an entire session down a live-debugging investigation
 * that hit two separate Wine walls before ever confirming what the check
 * actually looks at. "Headless" in ordinary Windows usage more often means
 * "no interactive session" (a service, Session 0, a remote/non-console
 * session, no window station) than "being debugged" -- this asks the
 * questions an install would ask to tell those apart, so that hypothesis can
 * be checked without touching a debugger at all.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <windows.h>
#include <wtsapi32.h>
#include <stdio.h>

int main(void)
{
    DWORD session_id = 0, pid = GetCurrentProcessId();
    HWINSTA winsta;
    HDESK desk;
    BOOL remote_session = GetSystemMetrics(SM_REMOTESESSION);
    BOOL remote_control = GetSystemMetrics(SM_REMOTECONTROL);
    DWORD console_session = WTSGetActiveConsoleSessionId();
    HWND desktop_hwnd = GetDesktopWindow();
    BOOL desktop_visible = desktop_hwnd ? IsWindowVisible(desktop_hwnd) : FALSE;
    WCHAR winsta_name[256] = {0}, desk_name[256] = {0};
    DWORD needed;

    ProcessIdToSessionId(pid, &session_id);
    winsta = GetProcessWindowStation();
    desk = GetThreadDesktop(GetCurrentThreadId());

    if (winsta) GetUserObjectInformationW(winsta, UOI_NAME, winsta_name, sizeof(winsta_name), &needed);
    if (desk) GetUserObjectInformationW(desk, UOI_NAME, desk_name, sizeof(desk_name), &needed);

    printf("pid                    = %lu\n", pid);
    printf("session id             = %lu\n", session_id);
    printf("active console session = %lu\n", console_session);
    printf("SM_REMOTESESSION        = %d\n", remote_session);
    printf("SM_REMOTECONTROL        = %d\n", remote_control);
    printf("window station handle   = %p\n", (void *)winsta);
    printf("window station name     = %ls\n", winsta_name);
    printf("desktop handle          = %p\n", (void *)desk);
    printf("desktop name            = %ls\n", desk_name);
    printf("GetDesktopWindow()      = %p\n", (void *)desktop_hwnd);
    printf("desktop window visible  = %d\n", desktop_visible);

    /* A process with no interactive window station (a real Windows service
     * running as SYSTEM, for instance) typically gets a window station named
     * something like "Service-0x0-3e7$", not "WinSta0". */
    if (winsta_name[0])
        printf("interactive winsta guess = %ls\n",
                lstrcmpiW(winsta_name, L"WinSta0") == 0 ? L"yes (WinSta0)" : L"NO -- not WinSta0");

    return 0;
}
