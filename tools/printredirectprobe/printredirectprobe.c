/*
 * printredirectprobe - what a window is told, and what IsWindowRedirectedForPrint
 * says, while PrintWindow prints it.
 *
 * Office asks IsWindowRedirectedForPrint hundreds of times as it paints, and
 * Wine's answered FALSE from a stub.  This prints a window and a child of it
 * with each of PrintWindow's flags, from its own thread and from another, and
 * logs each message that arrives with the answer at that moment, for the
 * window and for its child.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <windows.h>
#include <stdio.h>

static BOOL (WINAPI *pIsWindowRedirectedForPrint)(HWND);
static HWND parent, child;
static BOOL logging;

static const char *name(UINT msg)
{
    static char buf[16];
    switch (msg)
    {
    case WM_PAINT: return "WM_PAINT";
    case WM_ERASEBKGND: return "WM_ERASEBKGND";
    case WM_PRINT: return "WM_PRINT";
    case WM_PRINTCLIENT: return "WM_PRINTCLIENT";
    case WM_NCPAINT: return "WM_NCPAINT";
    case WM_SYNCPAINT: return "WM_SYNCPAINT";
    case WM_CTLCOLORSTATIC: return "WM_CTLCOLORSTATIC";
    case WM_GETTEXT: return "WM_GETTEXT";
    case WM_GETTEXTLENGTH: return "WM_GETTEXTLENGTH";
    case WM_GETICON: return "WM_GETICON";
    }
    sprintf(buf, "0x%04x", msg);
    return buf;
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (logging && msg != WM_GETICON && msg != WM_GETTEXT && msg != WM_GETTEXTLENGTH && msg < WM_USER)
        printf("    %-6s %-17s wp %p lp %#lx: redirected: window %d child %d\n", hwnd == parent ? "window" : "child",
               name(msg), (void *)wp, (long)lp, pIsWindowRedirectedForPrint(parent),
               pIsWindowRedirectedForPrint(child));
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void print_it(HWND hwnd, UINT flags, const char *what)
{
    HDC screen = GetDC(NULL), dc = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, 200, 200);
    BOOL ret;

    SelectObject(dc, bmp);
    printf("  %s, flags %#x:\n", what, flags);
    logging = TRUE;
    ret = PrintWindow(hwnd, dc, flags);
    logging = FALSE;
    printf("  -> %d; afterwards window %d child %d\n", ret, pIsWindowRedirectedForPrint(parent),
           pIsWindowRedirectedForPrint(child));
    DeleteObject(bmp);
    DeleteDC(dc);
    ReleaseDC(NULL, screen);
}

static DWORD WINAPI other_thread(void *arg)
{
    print_it(parent, 0, "the window, from another thread");
    return 0;
}

int main(void)
{
    WNDCLASSW cls = {0};
    HANDLE thread;
    MSG msg;
    DWORD end;

    setvbuf(stdout, NULL, _IONBF, 0);
    pIsWindowRedirectedForPrint = (void *)GetProcAddress(GetModuleHandleA("user32.dll"), "IsWindowRedirectedForPrint");
    printf("IsWindowRedirectedForPrint %p\n", pIsWindowRedirectedForPrint);
    if (!pIsWindowRedirectedForPrint) return 1;

    cls.lpfnWndProc = wndproc;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.lpszClassName = L"printredirect";
    cls.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    RegisterClassW(&cls);
    parent = CreateWindowW(L"printredirect", L"print", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 200, 150,
                           NULL, NULL, cls.hInstance, NULL);
    child = CreateWindowW(L"printredirect", L"child", WS_CHILD | WS_VISIBLE, 10, 10, 50, 50, parent, NULL,
                          cls.hInstance, NULL);
    end = GetTickCount() + 300;
    while (GetTickCount() < end)
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);

    printf("outside PrintWindow: window %d child %d, NULL %d, desktop %d\n", pIsWindowRedirectedForPrint(parent),
           pIsWindowRedirectedForPrint(child), pIsWindowRedirectedForPrint(NULL),
           pIsWindowRedirectedForPrint(GetDesktopWindow()));
    print_it(parent, 0, "the window");
    print_it(parent, PW_CLIENTONLY, "the window");
    print_it(parent, 2 /* PW_RENDERFULLCONTENT */, "the window");
    print_it(child, 0, "the child");

    thread = CreateThread(NULL, 0, other_thread, NULL, 0, NULL);
    while (MsgWaitForMultipleObjects(1, &thread, FALSE, 5000, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
    CloseHandle(thread);

    {
        HDC dc = CreateCompatibleDC(NULL);
        BOOL ret;

        SetLastError(0xdeadbeef);
        ret = PrintWindow((HWND)0xdeadbeef, dc, 0);
        printf("a window that is not there: %d, error %lu\n", ret, GetLastError());
        SetLastError(0xdeadbeef);
        ret = PrintWindow(parent, NULL, 0);
        printf("no DC: %d, error %lu\n", ret, GetLastError());
        DeleteDC(dc);
    }
    DestroyWindow(parent);
    printf("done\n");
    return 0;
}
