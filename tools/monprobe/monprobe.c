/*
 * monprobe -- what a process is told about the screen.
 *
 * A window that will not resize past some old size is usually not the window's
 * fault: the numbers behind SM_CXMAXTRACK and the monitor rectangle are read
 * once and kept.  This prints what this process is told now, and, given a
 * window title substring, what that window's own limits are -- so a stale view
 * shows up as a disagreement between the two.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -municode \
 *       -o monprobe.exe monprobe.c -luser32
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <windows.h>
#include <stdio.h>

static BOOL CALLBACK mon_proc(HMONITOR mon, HDC dc, LPRECT rc, LPARAM lp)
{
    MONITORINFOEXW mi = { .cbSize = sizeof(mi) };
    (void)dc; (void)rc;
    if (GetMonitorInfoW(mon, (MONITORINFO *)&mi))
    {
        char name[CCHDEVICENAME * 2];
        WideCharToMultiByte(CP_UTF8, 0, mi.szDevice, -1, name, sizeof(name), NULL, NULL);
        printf("  %-14s 显示器 (%ld,%ld)-(%ld,%ld)  工作区 (%ld,%ld)-(%ld,%ld)%s\n", name,
               mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right, mi.rcMonitor.bottom,
               mi.rcWork.left, mi.rcWork.top, mi.rcWork.right, mi.rcWork.bottom,
               (mi.dwFlags & MONITORINFOF_PRIMARY) ? "  [主]" : "");
    }
    (*(int *)lp)++;
    return TRUE;
}

static WCHAR want[128];
static HWND found;

static BOOL CALLBACK find_proc(HWND hwnd, LPARAM lp)
{
    WCHAR t[256] = {0};
    (void)lp;
    GetWindowTextW(hwnd, t, 255);
    if (want[0] && wcsstr(t, want) && IsWindowVisible(hwnd)) { found = hwnd; return FALSE; }
    return TRUE;
}

int wmain(int argc, WCHAR **argv)
{
    int n = 0;

    printf("SM_CXSCREEN        = %d x %d\n", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    printf("SM_CX/CYVIRTUAL    = %d x %d at (%d,%d)\n",
           GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN),
           GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN));
    printf("SM_CX/CYMAXTRACK   = %d x %d   (窗口能被拖到的最大尺寸)\n",
           GetSystemMetrics(SM_CXMAXTRACK), GetSystemMetrics(SM_CYMAXTRACK));
    printf("SM_CX/CYMAXIMIZED  = %d x %d\n",
           GetSystemMetrics(SM_CXMAXIMIZED), GetSystemMetrics(SM_CYMAXIMIZED));
    printf("SM_CMONITORS       = %d\n", GetSystemMetrics(SM_CMONITORS));

    printf("EnumDisplayMonitors:\n");
    EnumDisplayMonitors(NULL, NULL, mon_proc, (LPARAM)&n);
    printf("  共 %d 台\n", n);

    if (argc > 1)
    {
        lstrcpynW(want, argv[1], 127);
        EnumWindows(find_proc, 0);
        if (!found) { printf("\n没找到标题含 %ls 的可见窗口\n", want); return 1; }
        else
        {
            MINMAXINFO mmi = { 0 };
            RECT r;
            HMONITOR mon = MonitorFromWindow(found, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = { .cbSize = sizeof(mi) };

            GetWindowRect(found, &r);
            printf("\n目标窗口 %p\n", found);
            printf("  当前矩形       (%ld,%ld)-(%ld,%ld)  %ldx%ld\n",
                   r.left, r.top, r.right, r.bottom, r.right - r.left, r.bottom - r.top);
            printf("  MonitorFromWindow = %p%s\n", mon, mon ? "" : "  <== NULL，这就是问题");
            if (mon && GetMonitorInfoW(mon, &mi))
                printf("  它所在显示器   (%ld,%ld)-(%ld,%ld)\n",
                       mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right, mi.rcMonitor.bottom);
            /* Ask the window itself what it will allow. */
            SendMessageTimeoutW(found, WM_GETMINMAXINFO, 0, (LPARAM)&mmi, SMTO_ABORTIFHUNG, 3000, NULL);
            printf("  它自己报的上限 最大 %ldx%ld 于 (%ld,%ld)，拖动上限 %ldx%ld\n",
                   mmi.ptMaxSize.x, mmi.ptMaxSize.y, mmi.ptMaxPosition.x, mmi.ptMaxPosition.y,
                   mmi.ptMaxTrackSize.x, mmi.ptMaxTrackSize.y);
        }
    }
    return 0;
}
