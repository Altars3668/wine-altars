/*
 * ddshot - capture an Office dropdown the way the screen shows it, on Windows and under Wine
 * alike, so that the two can be put side by side.
 *
 * What a NetUI dropdown looks like -- its corners, border, shadow and scroll bar -- is decided
 * between Office, user32, DWM and the theme, and no single API answers it. The screen does.
 * This opens the font name dropdown on the Home tab of Word (or Excel, whose font list is the same
 * NetUI control) and photographs the screen around it with
 * the pointer in several places, since a modern scroll bar changes as the pointer comes near it:
 *
 *   open    the pointer still on the dropdown arrow it clicked
 *   list    the pointer in the middle of the list
 *   track   the pointer on the scroll bar's track
 *   thumb   the pointer on the scroll bar's thumb
 *   away    the pointer off the dropdown again
 *
 * and once more through PrintWindow(PW_RENDERFULLCONTENT), which has the window's own pixels
 * without anything DWM adds around them. Each picture is a PNG printed as base64 between
 * "PNG-BEGIN <name> <w>x<h>" and "PNG-END". Before encoding, every 8x8 cell whose centre lies on
 * a window of another process is painted grey, so that nothing but Office's own pixels leaves the
 * machine. It also prints what the dropdown window is: class, rectangles, styles, DPI and the
 * DWM attributes that shape it.
 *
 *   ddshot [-attach] [-excel]
 *
 * Without -attach it starts a Word of its own -- /w /q /a: a blank document, no splash screen,
 * no add-ins -- and closes it at the end. It refuses to run when a WINWORD.EXE is already
 * running, so that a Word someone is using is never touched, and when the input desktop is not
 * the default one (a locked session), or when the keyboard or mouse was used in the last two
 * minutes, so that it does not thrust itself into someone's work. With -excel it does the same with Excel, which it takes from
 * its start screen to a new blank workbook with Esc, so that no file is opened and none joins the
 * recent list. With -attach it uses the one that is already running, as on a Wine test display.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <tlhelp32.h>
#include <initguid.h>
#include <oleacc.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <wincrypt.h>

#ifndef CAPTUREBLT
#define CAPTUREBLT 0x40000000
#endif

extern LONG WINAPI RtlGetVersion(OSVERSIONINFOEXW *);

struct app
{
    const WCHAR *exe;           /* the process name */
    const WCHAR *app_path;      /* its App Paths key */
    const WCHAR *args;
    const WCHAR *main_class;
    BOOL leave_start_screen;    /* press Esc on the start screen for a blank document */
};

static const struct app word = { L"WINWORD.EXE", L"Winword.exe", L" /w /q /a", L"OpusApp", FALSE };
static const struct app excel = { L"EXCEL.EXE", L"excel.exe", L"", L"XLMAIN", TRUE };
static const struct app *app = &word;

static DWORD word_pid;          /* the application's, Word's or Excel's */
static HWND word_main;
static HANDLE word_process;     /* only when this program started it */
static UINT dpi = 96;

static int scale(int px) { return MulDiv(px, dpi, 96); }

static DWORD running_word(void)
{
    PROCESSENTRY32W entry = { sizeof(entry) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    DWORD pid = 0;

    if (snap == INVALID_HANDLE_VALUE) return 0;
    if (Process32FirstW(snap, &entry))
        do if (!lstrcmpiW(entry.szExeFile, app->exe)) { pid = entry.th32ProcessID; break; }
        while (Process32NextW(snap, &entry));
    CloseHandle(snap);
    return pid;
}

static BOOL input_desktop_is_default(void)
{
    HDESK desk = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    WCHAR name[64] = L"";
    DWORD len;

    if (!desk) return FALSE;
    GetUserObjectInformationW(desk, UOI_NAME, name, sizeof(name), &len);
    CloseDesktop(desk);
    return !lstrcmpiW(name, L"Default");
}

static BOOL start_word(void)
{
    WCHAR key[MAX_PATH], path[MAX_PATH], cmd[MAX_PATH + 32];
    DWORD len = sizeof(path);
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;

    swprintf(key, ARRAYSIZE(key), L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\%s", app->app_path);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, NULL, RRF_RT_REG_SZ, NULL, path, &len))
    {
        printf("no App Paths entry for %ls\n", app->app_path);
        return FALSE;
    }
    swprintf(cmd, ARRAYSIZE(cmd), L"\"%s\"%s", path, app->args);
    if (!CreateProcessW(path, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        printf("CreateProcess failed: %lu\n", GetLastError());
        return FALSE;
    }
    CloseHandle(pi.hThread);
    word_process = pi.hProcess;
    word_pid = pi.dwProcessId;
    return TRUE;
}

struct find_window { DWORD pid; const WCHAR *class; HWND found; };

static BOOL CALLBACK find_window_proc(HWND hwnd, LPARAM param)
{
    struct find_window *find = (struct find_window *)param;
    WCHAR class[128];
    DWORD pid;

    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != find->pid || !IsWindowVisible(hwnd)) return TRUE;
    GetClassNameW(hwnd, class, ARRAYSIZE(class));
    if (wcscmp(class, find->class)) return TRUE;
    find->found = hwnd;
    return FALSE;
}

/* the Font box: an MSAA combo box named 字体 (or Font), with a place on the screen */
static BOOL box_found;
static RECT box_rect;
static const WCHAR *box_names[] = { L"\x5b57\x4f53", L"Font" };

static void walk(IAccessible *acc, VARIANT self, int depth)
{
    VARIANT *kids, role, state;
    LONG count = 0, got = 0, i, x, y, w, h;
    BSTR name = NULL;

    if (box_found) return;
    if (SUCCEEDED(IAccessible_get_accName(acc, self, &name)) && name)
    {
        for (i = 0; i < ARRAYSIZE(box_names); i++)
        {
            if (wcscmp(name, box_names[i])) continue;
            VariantInit(&role);
            VariantInit(&state);
            if (SUCCEEDED(IAccessible_get_accRole(acc, self, &role)) && V_VT(&role) == VT_I4
                    && V_I4(&role) == ROLE_SYSTEM_COMBOBOX
                    && SUCCEEDED(IAccessible_get_accState(acc, self, &state)) && V_VT(&state) == VT_I4
                    && !(V_I4(&state) & STATE_SYSTEM_UNAVAILABLE)
                    && SUCCEEDED(IAccessible_accLocation(acc, &x, &y, &w, &h, self)) && w > 0 && h > 0)
            {
                SetRect(&box_rect, x, y, x + w, y + h);
                box_found = TRUE;
            }
            VariantClear(&role);
            VariantClear(&state);
        }
        SysFreeString(name);
    }
    if (box_found || V_VT(&self) != VT_I4 || V_I4(&self) != CHILDID_SELF || depth > 14) return;
    if (IAccessible_get_accChildCount(acc, &count) != S_OK || count <= 0) return;
    if (!(kids = calloc(count, sizeof(*kids)))) return;
    if (AccessibleChildren(acc, 0, count, kids, &got) == S_OK)
    {
        for (i = 0; i < got; i++)
        {
            if (V_VT(&kids[i]) == VT_DISPATCH)
            {
                IAccessible *child = NULL;
                if (!box_found && IDispatch_QueryInterface(V_DISPATCH(&kids[i]), &IID_IAccessible, (void **)&child) == S_OK)
                {
                    VARIANT child_self;
                    V_VT(&child_self) = VT_I4;
                    V_I4(&child_self) = CHILDID_SELF;
                    walk(child, child_self, depth + 1);
                    IAccessible_Release(child);
                }
                IDispatch_Release(V_DISPATCH(&kids[i]));
            }
            else if (V_VT(&kids[i]) == VT_I4 && !box_found) walk(acc, kids[i], depth + 1);
        }
    }
    free(kids);
}

static BOOL CALLBACK walk_window(HWND hwnd, LPARAM param)
{
    IAccessible *acc = NULL;
    VARIANT self;

    if (box_found) return FALSE;
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (AccessibleObjectFromWindow(hwnd, OBJID_CLIENT, &IID_IAccessible, (void **)&acc) == S_OK && acc)
    {
        V_VT(&self) = VT_I4;
        V_I4(&self) = CHILDID_SELF;
        walk(acc, self, 0);
        IAccessible_Release(acc);
    }
    return !box_found;
}

static BOOL find_font_box(void)
{
    box_found = FALSE;
    EnumChildWindows(word_main, walk_window, 0);
    return box_found;
}

static BOOL is_word_at(POINT pt)
{
    HWND hwnd = WindowFromPoint(pt);
    DWORD pid = 0;

    if (hwnd) GetWindowThreadProcessId(GetAncestor(hwnd, GA_ROOT), &pid);
    return pid == word_pid;
}

static BOOL bring_to_front(HWND hwnd)
{
    DWORD me = GetCurrentThreadId();
    int i;

    for (i = 0; i < 3 && GetForegroundWindow() != hwnd; i++)
    {
        HWND fg = GetForegroundWindow();
        DWORD them = fg ? GetWindowThreadProcessId(fg, NULL) : 0;

        if (them && them != me) AttachThreadInput(me, them, TRUE);
        if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
        BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
        if (them && them != me) AttachThreadInput(me, them, FALSE);
        Sleep(500);
    }
    return GetForegroundWindow() == hwnd;
}

static void click(POINT pt)
{
    INPUT in[2];

    SetCursorPos(pt.x, pt.y);
    Sleep(150);
    memset(in, 0, sizeof(in));
    in[0].type = in[1].type = INPUT_MOUSE;
    in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(2, in, sizeof(in[0]));
}

static void press_escape(void)
{
    INPUT in[2];

    memset(in, 0, sizeof(in));
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = VK_ESCAPE;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(in[0]));
}

/* the dropdown: the biggest new visible top-level window of Word's */
struct popups { HWND list[64]; int count; };

static BOOL CALLBACK collect_popup(HWND hwnd, LPARAM param)
{
    struct popups *popups = (struct popups *)param;
    DWORD pid;

    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == word_pid && hwnd != word_main && IsWindowVisible(hwnd) && popups->count < ARRAYSIZE(popups->list))
        popups->list[popups->count++] = hwnd;
    return TRUE;
}

static BOOL listed_before(const struct popups *before, HWND hwnd)
{
    int i;
    for (i = 0; i < before->count; i++) if (before->list[i] == hwnd) return TRUE;
    return FALSE;
}

static HWND find_popup(const struct popups *before)
{
    struct popups now = { { 0 }, 0 };
    LONG best_area = 0;
    HWND best = NULL;
    int i;

    EnumWindows(collect_popup, (LPARAM)&now);
    for (i = 0; i < now.count; i++)
    {
        WCHAR class[128];
        RECT rc;
        LONG area;

        GetClassNameW(now.list[i], class, ARRAYSIZE(class));
        GetWindowRect(now.list[i], &rc);
        area = (rc.right - rc.left) * (rc.bottom - rc.top);
        printf("  window %p %ls (%ld,%ld)-(%ld,%ld)%s\n", now.list[i], class, rc.left, rc.top, rc.right, rc.bottom,
               listed_before(before, now.list[i]) ? "" : " new");
        if (listed_before(before, now.list[i]) || wcsstr(class, L"BORDEREFFECT")) continue;
        if (area > best_area) { best_area = area; best = now.list[i]; }
    }
    return best;
}

static void print_dwm_dword(HWND hwnd, DWORD attr, const char *name)
{
    DWORD value = 0xdeadbeef;
    HRESULT hr = DwmGetWindowAttribute(hwnd, attr, &value, sizeof(value));
    printf("  %s: hr %#lx value %#lx\n", name, hr, value);
}

static void describe(HWND hwnd)
{
    WCHAR class[128];
    RECT window, client, frame;
    HRGN region = CreateRectRgn(0, 0, 0, 0);
    int region_type;
    HRESULT hr;

    GetClassNameW(hwnd, class, ARRAYSIZE(class));
    GetWindowRect(hwnd, &window);
    GetClientRect(hwnd, &client);
    printf("dropdown %p class %ls\n", hwnd, class);
    printf("  window (%ld,%ld)-(%ld,%ld) %ldx%ld, client %ldx%ld\n", window.left, window.top, window.right, window.bottom,
           window.right - window.left, window.bottom - window.top, client.right, client.bottom);
    printf("  style %#lx exstyle %#lx class style %#lx dpi %u\n", GetWindowLongW(hwnd, GWL_STYLE),
           GetWindowLongW(hwnd, GWL_EXSTYLE), (DWORD)GetClassLongPtrW(hwnd, GCL_STYLE), GetDpiForWindow(hwnd));
    hr = DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame));
    printf("  DWMWA_EXTENDED_FRAME_BOUNDS: hr %#lx (%ld,%ld)-(%ld,%ld)\n", hr, frame.left, frame.top, frame.right, frame.bottom);
    print_dwm_dword(hwnd, 2 /* DWMWA_NCRENDERING_POLICY */, "DWMWA_NCRENDERING_POLICY");
    print_dwm_dword(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, "DWMWA_WINDOW_CORNER_PREFERENCE");
    print_dwm_dword(hwnd, 34 /* DWMWA_BORDER_COLOR */, "DWMWA_BORDER_COLOR");
    print_dwm_dword(hwnd, 38 /* DWMWA_SYSTEMBACKDROP_TYPE */, "DWMWA_SYSTEMBACKDROP_TYPE");
    region_type = GetWindowRgn(hwnd, region);
    if (region_type != ERROR)
    {
        RECT box;
        GetRgnBox(region, &box);
        printf("  window region type %d box (%ld,%ld)-(%ld,%ld)\n", region_type, box.left, box.top, box.right, box.bottom);
    }
    else printf("  no window region\n");
    DeleteObject(region);
    if (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED)
    {
        COLORREF key = 0;
        BYTE alpha = 0;
        DWORD flags = 0;
        BOOL ret = GetLayeredWindowAttributes(hwnd, &key, &alpha, &flags);
        printf("  layered: ret %d key %#lx alpha %u flags %#lx\n", ret, key, alpha, flags);
    }
}

static void print_png(const char *name, const BYTE *bgrx, int width, int height)
{
    IWICImagingFactory *factory = NULL;
    IWICBitmapEncoder *encoder = NULL;
    IWICBitmapFrameEncode *frame = NULL;
    IStream *stream = NULL;
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    UINT stride = (width * 3 + 3) & ~3;
    BYTE *bgr = malloc(stride * height), *png;
    STATSTG stat;
    HGLOBAL global;
    DWORD len = 0;
    char *text;
    HRESULT hr;
    int x, y;

    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            memcpy(bgr + y * stride + x * 3, bgrx + (y * width + x) * 4, 3);
    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&factory);
    if (SUCCEEDED(hr)) hr = CreateStreamOnHGlobal(NULL, TRUE, &stream);
    if (SUCCEEDED(hr)) hr = IWICImagingFactory_CreateEncoder(factory, &GUID_ContainerFormatPng, NULL, &encoder);
    if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_Initialize(encoder, stream, WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_CreateNewFrame(encoder, &frame, NULL);
    if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_Initialize(frame, NULL);
    if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_SetSize(frame, width, height);
    if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_SetPixelFormat(frame, &format);
    if (SUCCEEDED(hr) && !IsEqualGUID(&format, &GUID_WICPixelFormat24bppBGR)) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_WritePixels(frame, height, stride, stride * height, bgr);
    if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_Commit(frame);
    if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_Commit(encoder);
    if (SUCCEEDED(hr)) hr = IStream_Stat(stream, &stat, STATFLAG_NONAME);
    if (SUCCEEDED(hr)) hr = GetHGlobalFromStream(stream, &global);
    if (SUCCEEDED(hr) && (png = GlobalLock(global)))
    {
        CryptBinaryToStringA(png, stat.cbSize.LowPart, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCR, NULL, &len);
        if ((text = malloc(len)) && CryptBinaryToStringA(png, stat.cbSize.LowPart, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCR, text, &len))
        {
            printf("PNG-BEGIN %s %dx%d\n", name, width, height);
            fwrite(text, 1, len, stdout);
            printf("PNG-END\n");
        }
        free(text);
        GlobalUnlock(global);
    }
    else printf("PNG %s: encoding failed, hr %#lx\n", name, hr);
    fflush(stdout);
    if (frame) IWICBitmapFrameEncode_Release(frame);
    if (encoder) IWICBitmapEncoder_Release(encoder);
    if (stream) IStream_Release(stream);
    if (factory) IWICImagingFactory_Release(factory);
    free(bgr);
}

/* grey out every 8x8 cell whose centre is on someone else's window */
static int mask_others(BYTE *bgrx, int width, int height, POINT origin)
{
    int cx, cy, x, y, masked = 0;

    for (cy = 0; cy < height; cy += 8)
        for (cx = 0; cx < width; cx += 8)
        {
            POINT pt = { origin.x + cx + 4, origin.y + cy + 4 };
            if (is_word_at(pt)) continue;
            masked++;
            for (y = cy; y < cy + 8 && y < height; y++)
                for (x = cx; x < cx + 8 && x < width; x++)
                    *(DWORD *)(bgrx + (y * width + x) * 4) = 0x808080;
        }
    return masked;
}

static void capture_screen(const char *name, RECT rc)
{
    int width = rc.right - rc.left, height = rc.bottom - rc.top, masked;
    BITMAPINFO info = { { sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB } };
    HDC screen = GetDC(NULL), mem = CreateCompatibleDC(screen);
    void *bits;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ old = SelectObject(mem, bitmap);
    POINT origin = { rc.left, rc.top }, cursor;

    GetCursorPos(&cursor);
    BitBlt(mem, 0, 0, width, height, screen, rc.left, rc.top, SRCCOPY | CAPTUREBLT);
    GdiFlush();
    masked = mask_others(bits, width, height, origin);
    printf("capture %s: screen (%ld,%ld)-(%ld,%ld), pointer (%ld,%ld), %d cells of other windows greyed\n",
           name, rc.left, rc.top, rc.right, rc.bottom, cursor.x, cursor.y, masked);
    print_png(name, bits, width, height);
    SelectObject(mem, old);
    DeleteObject(bitmap);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
}

static void capture_window(const char *name, HWND hwnd)
{
    RECT rc;
    int width, height;
    BITMAPINFO info = { { sizeof(BITMAPINFOHEADER) } };
    HDC screen = GetDC(NULL), mem = CreateCompatibleDC(screen);
    HBITMAP bitmap;
    HGDIOBJ old;
    void *bits;
    BOOL ret;

    GetWindowRect(hwnd, &rc);
    width = rc.right - rc.left;
    height = rc.bottom - rc.top;
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, NULL, 0);
    old = SelectObject(mem, bitmap);
    ret = PrintWindow(hwnd, mem, 2 /* PW_RENDERFULLCONTENT */);
    GdiFlush();
    printf("capture %s: PrintWindow -> %d\n", name, ret);
    if (ret) print_png(name, bits, width, height);
    SelectObject(mem, old);
    DeleteObject(bitmap);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
}

static void point_and_capture(const char *name, POINT pt, RECT area, DWORD settle)
{
    SetCursorPos(pt.x, pt.y);
    Sleep(settle);
    capture_screen(name, area);
}

static void print_setting(const WCHAR *subkey, const WCHAR *value)
{
    DWORD data = 0, len = sizeof(data);
    LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, subkey, value, RRF_RT_REG_DWORD, NULL, &data, &len);

    if (status) printf("setting %ls: absent (%ld)\n", value, status);
    else printf("setting %ls: %lu\n", value, data);
}

static DWORD WINAPI watchdog(void *arg)
{
    Sleep(300000);
    printf("watchdog: giving up after five minutes\n");
    fflush(stdout);
    if (word_process) TerminateProcess(word_process, 1);
    ExitProcess(5);
}

static void close_word(void)
{
    struct popups left = { { 0 }, 0 };
    int i;

    if (!word_process) return;
    PostMessageW(word_main, WM_CLOSE, 0, 0);
    if (WaitForSingleObject(word_process, 60000) == WAIT_OBJECT_0)
    {
        printf("the %ls this started has exited\n", app->exe);
        return;
    }
    EnumWindows(collect_popup, (LPARAM)&left);
    for (i = 0; i < left.count; i++)
    {
        WCHAR class[128];
        GetClassNameW(left.list[i], class, ARRAYSIZE(class));
        printf("  still open: %p %ls\n", left.list[i], class);
    }
    TerminateProcess(word_process, 1);
    printf("the %ls this started did not exit in a minute; terminated it\n", app->exe);
}

int main(int argc, char **argv)
{
    BOOL attach = FALSE;
    struct find_window find = { 0, NULL, NULL };
    struct popups before = { { 0 }, 0 };
    HWND previous, popup;
    POINT saved, arrow, pt;
    RECT rc, area, main_rc;
    OSVERSIONINFOEXW version = { sizeof(version) };
    ULONGLONG deadline;
    int i;

    for (i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-attach")) attach = TRUE;
        else if (!strcmp(argv[i], "-excel")) app = &excel;
        else { printf("usage: ddshot [-attach] [-excel]\n"); return 2; }
    }
    find.class = app->main_class;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    setvbuf(stdout, NULL, _IOFBF, 1 << 16);
    RtlGetVersion(&version);
    printf("Windows %lu.%lu.%lu\n", version.dwMajorVersion, version.dwMinorVersion, version.dwBuildNumber);
    print_setting(L"Control Panel\\Accessibility", L"DynamicScrollbars");
    print_setting(L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"EnableTransparency");
    print_setting(L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme");

    if (!input_desktop_is_default())
    {
        printf("the input desktop is not the default one (locked?); not starting\n");
        return 4;
    }
    if (!attach)
    {
        LASTINPUTINFO last = { sizeof(last) };

        /* someone at the machine would have the application thrust into their work */
        if (GetLastInputInfo(&last) && GetTickCount() - last.dwTime < 120000)
        {
            printf("someone used this machine %lu s ago; not starting\n", (GetTickCount() - last.dwTime) / 1000);
            return 6;
        }
    }
    if (attach)
    {
        if (!(word_pid = running_word())) { printf("no %ls is running\n", app->exe); return 3; }
    }
    else
    {
        if (running_word()) { printf("a %ls is already running; not touching it\n", app->exe); return 3; }
        if (!start_word()) return 2;
        CloseHandle(CreateThread(NULL, 0, watchdog, NULL, 0, NULL));
    }
    GetCursorPos(&saved);
    previous = GetForegroundWindow();

    find.pid = word_pid;
    for (i = 0; i < 180 && !find.found; i++)
    {
        EnumWindows(find_window_proc, (LPARAM)&find);
        if (!find.found) Sleep(500);
    }
    if (!(word_main = find.found)) { printf("no %ls window\n", app->main_class); goto done; }
    dpi = GetDpiForWindow(word_main);
    GetWindowRect(word_main, &main_rc);
    printf("%ls window %p (%ld,%ld)-(%ld,%ld), dpi %u\n", app->main_class, word_main, main_rc.left, main_rc.top,
           main_rc.right, main_rc.bottom, dpi);
    if (!attach && app->leave_start_screen)
    {
        Sleep(5000);
        if (!bring_to_front(word_main)) { printf("could not bring the window to the front\n"); goto done; }
        press_escape();
        printf("pressed Esc on the start screen\n");
    }

    /* walking the whole tree takes a while on Windows: bound it by time */
    for (deadline = GetTickCount64() + 90000; !find_font_box() && GetTickCount64() < deadline;) Sleep(500);
    if (!box_found) { printf("no Font box\n"); goto done; }
    printf("Font box (%ld,%ld)-(%ld,%ld)\n", box_rect.left, box_rect.top, box_rect.right, box_rect.bottom);
    Sleep(2000);    /* let the start-up finish drawing */

    if (!bring_to_front(word_main)) { printf("could not bring the window to the front\n"); goto done; }
    arrow.x = box_rect.right - scale(8);
    arrow.y = (box_rect.top + box_rect.bottom) / 2;
    if (!is_word_at(arrow)) { printf("the Font box is covered\n"); goto done; }
    EnumWindows(collect_popup, (LPARAM)&before);
    click(arrow);
    Sleep(1500);
    if (!(popup = find_popup(&before))) { printf("no dropdown appeared\n"); goto done; }
    describe(popup);

    GetWindowRect(popup, &rc);
    area = rc;
    InflateRect(&area, scale(24), scale(24));
    capture_screen("open", area);
    pt.x = (rc.left + rc.right) / 2; pt.y = (rc.top + rc.bottom) / 2;
    point_and_capture("list", pt, area, 900);
    pt.x = rc.right - scale(10); pt.y = rc.top + (rc.bottom - rc.top) * 2 / 3;
    point_and_capture("track", pt, area, 900);
    pt.y = rc.top + scale(30);
    point_and_capture("thumb", pt, area, 900);
    pt.x = rc.right + scale(160); pt.y = (rc.top + rc.bottom) / 2;
    if (pt.x < main_rc.right && is_word_at(pt)) point_and_capture("away", pt, area, 1500);
    capture_window("printwindow", popup);

    if (GetForegroundWindow() == word_main || IsWindowVisible(popup)) press_escape();
    Sleep(500);

done:
    close_word();
    SetCursorPos(saved.x, saved.y);
    if (!attach && previous && IsWindow(previous)) bring_to_front(previous);
    fflush(stdout);
    CoUninitialize();
    return 0;
}
