/*
 * toplevels - list the top-level windows of the Wine session this runs in.
 *
 * The quickest answer to "is the application up yet, and what is its main
 * window called": class, owning process, visibility, rectangle and title, and
 * which window has the foreground.  Run it in the same prefix as the
 * application -- a process in another prefix, or one that started a session of
 * its own because nothing else was running, sees nothing, and an empty list
 * says so rather than that the application has no windows.
 *
 *   toplevels [all]
 *
 * With "all", hidden windows that have a title are listed too.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static BOOL CALLBACK list_window(HWND hwnd, LPARAM all)
{
    char cls[128], title[256];
    DWORD pid;
    RECT r;

    GetClassNameA(hwnd, cls, sizeof(cls));
    GetWindowTextA(hwnd, title, sizeof(title));
    GetWindowRect(hwnd, &r);
    GetWindowThreadProcessId(hwnd, &pid);
    if (IsWindowVisible(hwnd) || (all && title[0]))
        printf("%p pid=%04lx vis=%d %-30s (%ld,%ld)-(%ld,%ld) \"%s\"\n", hwnd, pid, IsWindowVisible(hwnd),
               cls, r.left, r.top, r.right, r.bottom, title);
    return TRUE;
}

int main(int argc, char **argv)
{
    EnumWindows(list_window, argc > 1 && !strcmp(argv[1], "all"));
    printf("foreground=%p\n", GetForegroundWindow());
    return 0;
}
