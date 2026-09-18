/* winenum -- print the window tree of a running process, with the two facts
 * that decide whether anything can appear on screen: is the window visible,
 * and where is it.
 *
 * Written for the "Word's window is white" hunt: Office renders its whole UI
 * into a DXGI flip-model swapchain on a child NetUIHWND, so whether that
 * child is ever shown is the difference between "Wine drops the pixels" and
 * "Office never asked for them".
 *
 * Later grown a second column for the same reason in a different place: a
 * window that is visible and correctly placed can still be wrong on screen
 * because of *how* it is composited. Office's dialog shadows
 * (MSO_BORDEREFFECT_WINDOW_CLASS) are layered windows, and a layered window
 * whose per-pixel alpha is ignored paints as a solid block of whatever its
 * colour key was meant to erase. So print the extended style, and for a
 * layered window, what GetLayeredWindowAttributes answers -- the difference
 * between LWA_ALPHA/LWA_COLORKEY (set through SetLayeredWindowAttributes,
 * which that call can report) and per-pixel alpha (set through
 * UpdateLayeredWindow, which it cannot: it fails, and that failure is
 * itself the measurement).
 */
#include <windows.h>
#include <stdio.h>

static int match_pid;

static void indent(int d) { while (d--) printf("  "); }

/* The styles that decide how a window reaches the screen, and -- for a
 * layered one -- which of the two layering mechanisms it is using. A window
 * that is WS_EX_LAYERED but whose GetLayeredWindowAttributes fails is on the
 * per-pixel-alpha path: its contents came from UpdateLayeredWindow and carry
 * their own alpha channel, which is exactly the case a compositor is most
 * likely to get wrong. */
static const char *style_note(HWND hwnd)
{
    static char buf[192];
    LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
    COLORREF key = 0;
    BYTE alpha = 0;
    DWORD flags = 0;
    int n = 0;

    buf[0] = 0;
    if (!(ex & (WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_TOPMOST |
                WS_EX_NOACTIVATE | WS_EX_COMPOSITED)))
        return buf;

    n += sprintf(buf + n, " ex=%08lx", ex);
    if (ex & WS_EX_TRANSPARENT) n += sprintf(buf + n, " TRANSPARENT");
    if (ex & WS_EX_TOOLWINDOW)  n += sprintf(buf + n, " TOOLWINDOW");
    if (ex & WS_EX_TOPMOST)     n += sprintf(buf + n, " TOPMOST");
    if (ex & WS_EX_NOACTIVATE)  n += sprintf(buf + n, " NOACTIVATE");
    if (ex & WS_EX_COMPOSITED)  n += sprintf(buf + n, " COMPOSITED");
    if (ex & WS_EX_LAYERED)
    {
        n += sprintf(buf + n, " LAYERED");
        if (GetLayeredWindowAttributes(hwnd, &key, &alpha, &flags))
            n += sprintf(buf + n, "(attrs key=%06lx alpha=%u flags=%lx)",
                         (unsigned long)key, alpha, flags);
        else
            n += sprintf(buf + n, "(per-pixel: GetLayeredWindowAttributes failed %lu)",
                         GetLastError());
    }
    return buf;
}

static void walk(HWND parent, int depth)
{
    HWND child = GetWindow(parent, GW_CHILD);
    while (child)
    {
        WCHAR cls[128] = {0}, txt[128] = {0};
        RECT r = {0};
        DWORD pid = 0;
        GetClassNameW(child, cls, 128);
        GetWindowTextW(child, txt, 128);
        GetWindowRect(child, &r);
        GetWindowThreadProcessId(child, &pid);
        indent(depth);
        printf("%p %-24ls vis=%d (%ld,%ld)-(%ld,%ld)%s %ls\n", child, cls,
               IsWindowVisible(child), r.left, r.top, r.right, r.bottom,
               style_note(child), txt);
        if (depth < 6) walk(child, depth + 1);
        child = GetWindow(child, GW_HWNDNEXT);
    }
}

static BOOL CALLBACK top(HWND hwnd, LPARAM lp)
{
    DWORD pid = 0;
    WCHAR cls[128] = {0}, txt[128] = {0};
    RECT r = {0};
    GetWindowThreadProcessId(hwnd, &pid);
    if (match_pid && pid != (DWORD)match_pid) return TRUE;
    GetClassNameW(hwnd, cls, 128);
    GetWindowTextW(hwnd, txt, 128);
    GetWindowRect(hwnd, &r);
    printf("%p pid=%lu %-24ls vis=%d (%ld,%ld)-(%ld,%ld)%s %ls\n", hwnd, pid, cls,
           IsWindowVisible(hwnd), r.left, r.top, r.right, r.bottom,
           style_note(hwnd), txt);
    walk(hwnd, 1);
    return TRUE;
}

/* Who does Win32 think has the keyboard? On a display running PointerRoot
 * focus -- which is what this project measures on, and what
 * XGetInputFocus answers there -- X itself holds no focus window, so the
 * answer here is entirely Wine's own bookkeeping, driven by whatever
 * FocusIn/EnterNotify events winex11.drv decided to act on. When typing into
 * an application does nothing, this is the first thing to read: a foreground
 * window of NULL means no Win32 window will receive a keystroke at all, and
 * no amount of clicking or XTestFakeKeyEvent will change that. */
static void print_focus(void)
{
    HWND fg = GetForegroundWindow();
    WCHAR cls[128] = {0}, txt[128] = {0};
    DWORD tid;

    printf("== focus ==\n");
    if (!fg) { printf("GetForegroundWindow = NULL  (no window will get keyboard input)\n"); return; }
    GetClassNameW(fg, cls, 128);
    GetWindowTextW(fg, txt, 128);
    printf("GetForegroundWindow = %p %ls \"%ls\"\n", fg, cls, txt);

    /* GetFocus is per-thread-input-queue, so it answers for *this* thread
     * unless we attach to the foreground one first. */
    tid = GetWindowThreadProcessId(fg, NULL);
    if (AttachThreadInput(GetCurrentThreadId(), tid, TRUE))
    {
        HWND f = GetFocus(), a = GetActiveWindow();
        cls[0] = 0;
        if (f) GetClassNameW(f, cls, 128);
        printf("  GetFocus (that thread)  = %p %ls\n", f, cls);
        cls[0] = 0;
        if (a) GetClassNameW(a, cls, 128);
        printf("  GetActiveWindow         = %p %ls\n", a, cls);
        AttachThreadInput(GetCurrentThreadId(), tid, FALSE);
    }
    else printf("  (AttachThreadInput to the foreground thread failed: %lu)\n", GetLastError());
    printf("\n");
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "-focus")) { print_focus(); return 0; }
    if (argc > 1) match_pid = atoi(argv[1]);
    print_focus();
    EnumWindows(top, 0);
    return 0;
}
