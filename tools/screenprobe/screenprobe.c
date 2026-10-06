/*
 * screenprobe - what a per-monitor DPI aware process is told about the screen, and where a
 * class's top-level windows are, in physical pixels.
 *
 * monprobe answers as a DPI unaware program, which Wine (like Windows) hands scaled numbers:
 * on a 192 dpi screen a 3840x2160 monitor reads as 1920x1080 there, which looks exactly like
 * a session that has the wrong screen.  This asks the same questions after declaring per-monitor
 * awareness, so the numbers are the physical ones, and prints the given class's visible top-level
 * windows: rectangle, client size, DPI, show state, style.  Titles are not printed.
 *
 *   screenprobe [class]          (default class OpusApp, Word's main window)
 *   screenprobe -all             every visible top-level window, with its owner and extended style
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <stdio.h>
#include <string.h>
#include <windows.h>

static const char *want = "OpusApp";
static BOOL all;

static BOOL CALLBACK monitor_proc(HMONITOR monitor, HDC dc, RECT *rc, LPARAM param)
{
    MONITORINFOEXA info = { .cbSize = sizeof(info) };
    UINT x = 0, y = 0;
    HRESULT (WINAPI *get_dpi)(HMONITOR, int, UINT *, UINT *);

    GetMonitorInfoA(monitor, (MONITORINFO *)&info);
    get_dpi = (void *)GetProcAddress(LoadLibraryA("shcore.dll"), "GetDpiForMonitor");
    if (get_dpi) get_dpi(monitor, 0 /* MDT_EFFECTIVE_DPI */, &x, &y);
    printf("monitor %s (%ld,%ld)-(%ld,%ld) work (%ld,%ld)-(%ld,%ld) dpi %u%s\n", info.szDevice,
           info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right, info.rcMonitor.bottom,
           info.rcWork.left, info.rcWork.top, info.rcWork.right, info.rcWork.bottom, x,
           info.dwFlags & MONITORINFOF_PRIMARY ? " primary" : "");
    return TRUE;
}

static BOOL CALLBACK window_proc(HWND hwnd, LPARAM param)
{
    WINDOWPLACEMENT placement = { sizeof(placement) };
    char class[128];
    RECT rc, client;

    GetClassNameA(hwnd, class, sizeof(class));
    if (all)
    {
        DWORD pid = 0;
        if (!IsWindowVisible(hwnd)) return TRUE;
        GetWindowRect(hwnd, &rc);
        GetWindowThreadProcessId(hwnd, &pid);
        printf("%-32s %p pid %04lx (%ld,%ld)-(%ld,%ld) %ldx%ld style %#lx exstyle %#lx owner %p dpi %u\n", class, hwnd, pid,
               rc.left, rc.top, rc.right, rc.bottom, rc.right - rc.left, rc.bottom - rc.top, GetWindowLongA(hwnd, GWL_STYLE),
               GetWindowLongA(hwnd, GWL_EXSTYLE), GetWindow(hwnd, GW_OWNER), GetDpiForWindow(hwnd));
        return TRUE;
    }
    if (strcmp(class, want)) return TRUE;
    GetWindowRect(hwnd, &rc);
    GetClientRect(hwnd, &client);
    GetWindowPlacement(hwnd, &placement);
    printf("%s %p visible %d iconic %d zoomed %d (%ld,%ld)-(%ld,%ld) %ldx%ld client %ldx%ld dpi %u style %#lx\n",
           class, hwnd, IsWindowVisible(hwnd), IsIconic(hwnd), IsZoomed(hwnd), rc.left, rc.top, rc.right, rc.bottom,
           rc.right - rc.left, rc.bottom - rc.top, client.right, client.bottom, GetDpiForWindow(hwnd),
           GetWindowLongA(hwnd, GWL_STYLE));
    printf("  restored to (%ld,%ld)-(%ld,%ld), show %u\n", placement.rcNormalPosition.left,
           placement.rcNormalPosition.top, placement.rcNormalPosition.right, placement.rcNormalPosition.bottom,
           placement.showCmd);
    return TRUE;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "-all")) all = TRUE;
    else if (argc > 1) want = argv[1];
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    printf("system dpi %u, screen %dx%d, virtual %dx%d at (%d,%d)\n", GetDpiForSystem(),
           GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), GetSystemMetrics(SM_CXVIRTUALSCREEN),
           GetSystemMetrics(SM_CYVIRTUALSCREEN), GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN));
    EnumDisplayMonitors(NULL, NULL, monitor_proc, 0);
    EnumWindows(window_proc, 0);
    return 0;
}
