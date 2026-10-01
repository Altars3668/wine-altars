/* dpiprobe - what DPI a process sees, and what DPI awareness Office's windows run with.
 *
 * Office looked very small on a 4K monitor at 200%: under GNOME 50's Xwayland an X11 client gets the
 * monitor's physical pixels and is told its DPI through Xft.dpi, while Wine reports the prefix's fixed
 * LogPixels.  This prints the system DPI, LOGPIXELSX, the screen size and every monitor's effective and
 * raw DPI, for this process as unaware and as per-monitor aware; then, for the top-level windows of
 * Word, Excel, PowerPoint and Outlook that are running, the window's DPI and DPI awareness context, the
 * process's awareness, and the awareness of the child windows by class.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shellscalingapi.h>
#include <stdio.h>

static const char *awareness_name(DPI_AWARENESS awareness)
{
    switch (awareness)
    {
    case DPI_AWARENESS_INVALID: return "invalid";
    case DPI_AWARENESS_UNAWARE: return "unaware";
    case DPI_AWARENESS_SYSTEM_AWARE: return "system aware";
    case DPI_AWARENESS_PER_MONITOR_AWARE: return "per-monitor aware";
    default: return "?";
    }
}

static void print_context(const char *what, DPI_AWARENESS_CONTEXT context)
{
    printf("%s: awareness %s, dpi %u%s%s\n", what, awareness_name(GetAwarenessFromDpiAwarenessContext(context)),
           GetDpiFromDpiAwarenessContext(context),
           AreDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) ? " (v2)" : "",
           AreDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED) ? " (gdi scaled)" : "");
}

static BOOL CALLBACK monitor_proc(HMONITOR monitor, HDC hdc, RECT *rect, LPARAM lparam)
{
    MONITORINFOEXA info = {.cbSize = sizeof(info)};
    UINT ex = 0, ey = 0, rx = 0, ry = 0;

    GetMonitorInfoA(monitor, (MONITORINFO *)&info);
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &ex, &ey);
    GetDpiForMonitor(monitor, MDT_RAW_DPI, &rx, &ry);
    printf("    monitor %s (%ld,%ld)-(%ld,%ld)%s: effective %ux%u, raw %ux%u\n", info.szDevice, info.rcMonitor.left,
           info.rcMonitor.top, info.rcMonitor.right, info.rcMonitor.bottom,
           info.dwFlags & MONITORINFOF_PRIMARY ? " primary" : "", ex, ey, rx, ry);
    return TRUE;
}

static void print_view(const char *what)
{
    HDC hdc = GetDC(NULL);

    print_context(what, GetThreadDpiAwarenessContext());
    printf("    GetDpiForSystem %u, LOGPIXELSX %d, screen %dx%d\n", GetDpiForSystem(), GetDeviceCaps(hdc, LOGPIXELSX),
           GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    ReleaseDC(NULL, hdc);
    EnumDisplayMonitors(NULL, NULL, monitor_proc, 0);
}

struct child_count
{
    char cls[64][64];
    DPI_AWARENESS awareness[64];
    UINT dpi[64];
    unsigned int count[64], n;
};

static BOOL CALLBACK child_proc(HWND hwnd, LPARAM lparam)
{
    struct child_count *c = (struct child_count *)lparam;
    DPI_AWARENESS_CONTEXT context = GetWindowDpiAwarenessContext(hwnd);
    DPI_AWARENESS awareness = GetAwarenessFromDpiAwarenessContext(context);
    UINT dpi = GetDpiForWindow(hwnd);
    char cls[64];
    unsigned int i;

    GetClassNameA(hwnd, cls, sizeof(cls));
    for (i = 0; i < c->n; i++)
        if (!strcmp(c->cls[i], cls) && c->awareness[i] == awareness && c->dpi[i] == dpi) break;
    if (i == c->n && c->n < ARRAYSIZE(c->cls))
    {
        strcpy(c->cls[i], cls);
        c->awareness[i] = awareness;
        c->dpi[i] = dpi;
        c->count[i] = 0;
        c->n++;
    }
    if (i < c->n) c->count[i]++;
    return TRUE;
}

static void print_window(const char *app, const char *cls)
{
    PROCESS_DPI_AWARENESS process_awareness = -1;
    struct child_count children = {0};
    DWORD pid = 0;
    HANDLE process;
    RECT rect;
    HWND hwnd;
    unsigned int i;
    char what[128];

    if (!(hwnd = FindWindowA(cls, NULL)))
    {
        printf("%s (%s): not running\n", app, cls);
        return;
    }
    GetWindowThreadProcessId(hwnd, &pid);
    if ((process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid)))
    {
        GetProcessDpiAwareness(process, &process_awareness);
        CloseHandle(process);
    }
    GetWindowRect(hwnd, &rect);
    snprintf(what, sizeof(what), "%s window %p, process %lu (process awareness %d)", app, hwnd, pid, process_awareness);
    print_context(what, GetWindowDpiAwarenessContext(hwnd));
    printf("    GetDpiForWindow %u, rect (%ld,%ld)-(%ld,%ld)\n", GetDpiForWindow(hwnd), rect.left, rect.top,
           rect.right, rect.bottom);
    EnumChildWindows(hwnd, child_proc, (LPARAM)&children);
    for (i = 0; i < children.n; i++)
        printf("    %3u child %-32s %s, dpi %u\n", children.count[i], children.cls[i],
               awareness_name(children.awareness[i]), children.dpi[i]);
}

int main(void)
{
    print_view("this process, as started");
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    print_view("this thread, per-monitor aware v2");
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
    print_view("this thread, unaware");

    print_window("Word", "OpusApp");
    print_window("Excel", "XLMAIN");
    print_window("PowerPoint", "PPTFrameClass");
    print_window("Outlook", "rctrl_renwnd32");
    return 0;
}
