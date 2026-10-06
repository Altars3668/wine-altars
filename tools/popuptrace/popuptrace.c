/* popuptrace x y duration_ms [settle_ms]
 *
 * 在 Word 主窗口所在会话里点击 (x, y)，随后每 2 ms 记录：
 *   - 顶层窗口的新建、显示/隐藏、位置变化、销毁（含类名、样式、所属进程）；
 *   - 前台窗口；
 *   - Word 主线程的激活窗口、焦点窗口、捕获窗口和菜单状态。
 * 时间相对点击时刻，单位毫秒。只发送一次左键点击，不发送回车或其它按键。
 * 进程声明为 per-monitor DPI 感知，坐标是物理像素。 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_WIN 4096

struct win_state
{
    HWND hwnd;
    BOOL seen;
    BOOL visible;
    RECT rect;
    DWORD style, exstyle, pid, tid;
    WCHAR cls[64];
};

static struct win_state wins[MAX_WIN];
static int nwins;
static LARGE_INTEGER freq, t0;

static double now_ms(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)(t.QuadPart - t0.QuadPart) * 1000.0 / freq.QuadPart;
}

static struct win_state *find_win(HWND hwnd)
{
    int i;
    for (i = 0; i < nwins; i++) if (wins[i].hwnd == hwnd) return &wins[i];
    return NULL;
}

static void describe(const char *what, struct win_state *w)
{
    printf("%9.1f %-5s hwnd=%p pid=%04lx tid=%04lx vis=%d rect=(%ld,%ld)-(%ld,%ld) style=%08lx ex=%08lx class=%ls\n",
           now_ms(), what, w->hwnd, w->pid, w->tid, w->visible, w->rect.left, w->rect.top,
           w->rect.right, w->rect.bottom, w->style, w->exstyle, w->cls);
}

static BOOL CALLBACK enum_proc(HWND hwnd, LPARAM quiet)
{
    struct win_state *w = find_win(hwnd), cur = {0};
    cur.hwnd = hwnd;
    cur.visible = IsWindowVisible(hwnd);
    GetWindowRect(hwnd, &cur.rect);
    cur.style = GetWindowLongW(hwnd, GWL_STYLE);
    cur.exstyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
    cur.tid = GetWindowThreadProcessId(hwnd, &cur.pid);
    GetClassNameW(hwnd, cur.cls, ARRAYSIZE(cur.cls));
    cur.seen = TRUE;
    if (!w)
    {
        if (nwins >= MAX_WIN) return TRUE;
        w = &wins[nwins++];
        *w = cur;
        if (!quiet) describe("NEW", w);
        return TRUE;
    }
    w->seen = TRUE;
    if (cur.visible != w->visible)
    {
        *w = cur;
        if (!quiet) describe(cur.visible ? "SHOW" : "HIDE", w);
    }
    else if (memcmp(&cur.rect, &w->rect, sizeof(RECT)) || cur.style != w->style || cur.exstyle != w->exstyle)
    {
        *w = cur;
        if (!quiet && w->visible) describe("MOVE", w);
    }
    return TRUE;
}

static void snapshot(BOOL quiet)
{
    int i, j;
    for (i = 0; i < nwins; i++) wins[i].seen = FALSE;
    EnumWindows(enum_proc, quiet);
    for (i = j = 0; i < nwins; i++)
    {
        if (!wins[i].seen)
        {
            if (!quiet && wins[i].visible) describe("GONE", &wins[i]);
            else if (!quiet) printf("%9.1f GONE  hwnd=%p (hidden) class=%ls\n", now_ms(), wins[i].hwnd, wins[i].cls);
            continue;
        }
        wins[j++] = wins[i];
    }
    nwins = j;
}

int main(int argc, char **argv)
{
    typedef BOOL (WINAPI *dpi_fn)(DPI_AWARENESS_CONTEXT);
    dpi_fn set_dpi = (dpi_fn)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext");
    HWND word, fg = (HWND)-1;
    GUITHREADINFO gti = {sizeof(gti)}, last = {0};
    DWORD word_tid, duration, settle;
    INPUT in[3] = {0};
    int x, y;

    if (argc < 4) { fprintf(stderr, "usage: popuptrace x y duration_ms [settle_ms]\n"); return 2; }
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (set_dpi) set_dpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    setvbuf(stdout, NULL, _IOFBF, 1 << 20);
    x = atoi(argv[1]); y = atoi(argv[2]); duration = atoi(argv[3]);
    settle = argc > 4 ? atoi(argv[4]) : 300;
    word = FindWindowW(L"OpusApp", NULL);
    if (!word) { fprintf(stderr, "no OpusApp\n"); return 3; }
    word_tid = GetWindowThreadProcessId(word, NULL);
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    snapshot(TRUE);
    printf("dpi_aware=%d virtual_screen=%dx%d word=%p word_tid=%04lx windows_before=%d\n", !!set_dpi,
           GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN), word, word_tid, nwins);
    Sleep(settle);
    snapshot(TRUE);

    in[0].type = INPUT_MOUSE;
    in[0].mi.dx = MulDiv(x - GetSystemMetrics(SM_XVIRTUALSCREEN), 65535, GetSystemMetrics(SM_CXVIRTUALSCREEN) - 1);
    in[0].mi.dy = MulDiv(y - GetSystemMetrics(SM_YVIRTUALSCREEN), 65535, GetSystemMetrics(SM_CYVIRTUALSCREEN) - 1);
    in[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    in[1].type = INPUT_MOUSE;
    in[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    in[2].type = INPUT_MOUSE;
    in[2].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &in[0], sizeof(INPUT));
    Sleep(60);
    QueryPerformanceCounter(&t0);
    if (SendInput(2, &in[1], sizeof(INPUT)) != 2) { fprintf(stderr, "SendInput failed %lu\n", GetLastError()); return 4; }
    printf("%9.1f CLICK at %d,%d\n", now_ms(), x, y);

    while (now_ms() < duration)
    {
        HWND cur_fg = GetForegroundWindow();
        if (cur_fg != fg)
        {
            WCHAR cls[64] = L"";
            if (cur_fg) GetClassNameW(cur_fg, cls, ARRAYSIZE(cls));
            printf("%9.1f FG    hwnd=%p class=%ls\n", now_ms(), cur_fg, cls);
            fg = cur_fg;
        }
        gti.cbSize = sizeof(gti);
        if (GetGUIThreadInfo(word_tid, &gti) &&
            (gti.hwndActive != last.hwndActive || gti.hwndFocus != last.hwndFocus ||
             gti.hwndCapture != last.hwndCapture || gti.hwndMenuOwner != last.hwndMenuOwner || gti.flags != last.flags))
        {
            printf("%9.1f GTI   active=%p focus=%p capture=%p menuowner=%p flags=%#lx\n", now_ms(),
                   gti.hwndActive, gti.hwndFocus, gti.hwndCapture, gti.hwndMenuOwner, gti.flags);
            last = gti;
        }
        snapshot(FALSE);
        Sleep(2);
    }
    printf("%9.1f END\n", now_ms());
    return 0;
}
