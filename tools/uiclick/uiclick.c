/*
 * uiclick - press something in an Office window that paints nothing useful.
 *
 * The companion to uidump. Office draws its own controls, so there is no child
 * HWND to send a click to and no pixel worth aiming at; MSAA is the only
 * handle on them, and MSAA can also invoke them. Given a substring, this walks
 * the same tree uidump prints, finds the first accessible object whose name
 * contains it, and calls its default action.
 *
 *   uiclick [-click] <name-substring> [class-substring]
 *
 * Prints what it pressed, or every candidate name when it finds nothing, so a
 * failed match is a listing rather than a silence.
 *
 * accDoDefaultAction can also succeed and do nothing. Office's NetUI controls
 * are drawn by Office, and some of them answer S_OK to the default action
 * without acting on it -- measured on the first-run sign-in dialog's own
 * button, where accDoDefaultAction returned S_OK, nothing happened, and a real
 * click at the same accLocation opened the sign-in page five seconds later.
 * A silent no-op that reports success is worse than a failure, because it
 * reads as "the application ignored the click" and sends you looking for a bug
 * in the application. Pass -click to skip the default action and click where
 * the object says it is.
 *
 * accDoDefaultAction is not enough on its own. It works on Office's own
 * NetUI-drawn controls, which is what this was written for, but Wine's oleacc
 * answers E_NOTIMPL for it on the *standard* Win32 controls -- measured on the
 * plain Button in Word's safe-mode #32770 messagebox, where it is the whole
 * reason an automated run could not answer that prompt. Since accLocation does
 * work there, the fallback is to click the object where it says it is. That
 * also makes this usable as one tool for both kinds of dialog instead of
 * needing hand-measured screen coordinates for the Win32 ones.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <oleacc.h>
#include <stdio.h>
#include <string.h>

static int force_click;        /* -click: 跳过默认动作，直接点它自报的位置 */
static const char *want_name, *want_class, *set_value, *type_value;
static int clicked, listed, exact_pass;

/* MSAA can set a value as well as invoke: a rendered sign-in field is a real
 * accessible object, so filling it is the same kind of operation as pressing
 * the button next to it. */
static HRESULT put_value(IAccessible *acc, VARIANT self, const char *utf8)
{
    WCHAR w[512];
    BSTR b;
    HRESULT hr;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, ARRAYSIZE(w));
    if (!(b = SysAllocString(w))) return E_OUTOFMEMORY;
    hr = IAccessible_put_accValue(acc, self, b);
    SysFreeString(b);
    return hr;
}

/* Gecko answers E_NOTIMPL to put_accValue -- MSAA's setter is deprecated and
 * real browsers implement the IAccessible2 text interfaces instead. Focusing
 * the field and sending the characters is what is left, and it is also what a
 * person does. */
static HRESULT type_into(IAccessible *acc, VARIANT self, const char *utf8)
{
    WCHAR w[512];
    INPUT in[2];
    int n, i;
    HWND hwnd = NULL;
    HRESULT hr = IAccessible_accSelect(acc, SELFLAG_TAKEFOCUS, self);

    /* SendInput goes to whatever has the keyboard focus, which is not this
     * field unless its window is in front. Ask the accessible object which
     * window it belongs to and raise that first. */
    if (SUCCEEDED(WindowFromAccessibleObject(acc, &hwnd)) && hwnd) {
        HWND top = GetAncestor(hwnd, GA_ROOT);
        ShowWindow(top ? top : hwnd, SW_SHOW);
        SetForegroundWindow(top ? top : hwnd);
        SetActiveWindow(top ? top : hwnd);
        SetFocus(hwnd);
        Sleep(400);
    }

    n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, ARRAYSIZE(w));
    if (n > 0) n--;                     /* drop the terminator */
    Sleep(300);
    memset(in, 0, sizeof(in));
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.dwFlags = KEYEVENTF_UNICODE;
    in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    for (i = 0; i < n; i++) {
        in[0].ki.wScan = in[1].ki.wScan = w[i];
        SendInput(2, in, sizeof(in[0]));
        Sleep(25);
    }
    return hr;
}

/* Press an object that will not press itself. accDoDefaultAction is the polite
 * request; a real click at the location the object reports is what is left
 * when the polite request is not implemented. Move the pointer there first --
 * this is a synthesised click, and on a display running PointerRoot focus
 * (which the one this project measures on does) the pointer position is what
 * decides where input lands. */
static HRESULT click_at_location(IAccessible *acc, VARIANT self)
{
    LONG x = 0, y = 0, w = 0, h = 0;
    INPUT in[2];
    POINT saved;

    if (FAILED(IAccessible_accLocation(acc, &x, &y, &w, &h, self))) return E_FAIL;
    if (w <= 0 || h <= 0) return E_FAIL;

    GetCursorPos(&saved);
    SetCursorPos(x + w / 2, y + h / 2);
    Sleep(120);

    memset(in, 0, sizeof(in));
    in[0].type = in[1].type = INPUT_MOUSE;
    in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(2, in, sizeof(in[0]));
    Sleep(120);
    /* Leave the pointer where it was: under PointerRoot, parking it on this
     * button would silently redirect every later keystroke to this dialog. */
    SetCursorPos(saved.x, saved.y);
    return S_OK;
}

static void utf8(BSTR s, char *out, int n)
{
    out[0] = 0;
    if (s) WideCharToMultiByte(CP_UTF8, 0, s, -1, out, n, NULL, NULL);
}

static void walk(IAccessible *acc, VARIANT self, int depth)
{
    VARIANT *kids; LONG count = 0, got = 0, i;
    BSTR s = NULL; char name[512];

    if (clicked) return;
    if (SUCCEEDED(IAccessible_get_accName(acc, self, &s))) { utf8(s, name, sizeof(name)); if (s) SysFreeString(s); }
    else name[0] = 0;
    s = NULL;

    if (name[0]) {
        /* Exact first. "登录" as a substring also matches the sentence that
         * offers it ("...请重新登录。"), and pressing a static text does
         * nothing while looking like it worked. */
        int hit = exact_pass ? !strcmp(name, want_name) : (strstr(name, want_name) != NULL);
        if (hit) {
            HRESULT hr;
            if (type_value) {
                hr = type_into(acc, self, type_value);
                printf("typed into \"%s\" (focus -> %#lx)\n", name, hr);
            } else if (set_value) {
                hr = put_value(acc, self, set_value);
                printf("set \"%s\" -> %#lx\n", name, hr);
            } else {
                hr = force_click ? E_NOTIMPL : IAccessible_accDoDefaultAction(acc, self);
                if (FAILED(hr)) {
                    HRESULT hr2 = click_at_location(acc, self);
                    printf("pressed \"%s\" -> %#lx (accDoDefaultAction), "
                           "clicked at its accLocation -> %#lx\n", name, hr, hr2);
                } else {
                    printf("pressed \"%s\" -> %#lx\n", name, hr);
                }
            }
            clicked = 1;
            return;
        }
        if (listed) printf("  candidate: \"%s\"\n", name);
    }

    if (V_I4(&self) != CHILDID_SELF) return;
    if (IAccessible_get_accChildCount(acc, &count) != S_OK || count <= 0) return;
    if (depth > 12) return;
    if (!(kids = calloc(count, sizeof(*kids)))) return;
    if (AccessibleChildren(acc, 0, count, kids, &got) == S_OK) {
        for (i = 0; i < got && !clicked; i++) {
            if (V_VT(&kids[i]) == VT_DISPATCH) {
                IAccessible *child = NULL;
                if (IDispatch_QueryInterface(V_DISPATCH(&kids[i]), &IID_IAccessible, (void **)&child) == S_OK) {
                    VARIANT cs; V_VT(&cs) = VT_I4; V_I4(&cs) = CHILDID_SELF;
                    walk(child, cs, depth + 1);
                    IAccessible_Release(child);
                }
                IDispatch_Release(V_DISPATCH(&kids[i]));
            } else if (V_VT(&kids[i]) == VT_I4) {
                walk(acc, kids[i], depth + 1);
            }
        }
    }
    free(kids);
}

static BOOL CALLBACK visit(HWND hwnd, LPARAM param)
{
    char cls[128]; IAccessible *acc = NULL; VARIANT self;
    const DWORD objs[] = { OBJID_WINDOW, OBJID_CLIENT };
    int k;

    if (clicked) return FALSE;
    GetClassNameA(hwnd, cls, sizeof(cls));
    if (want_class && !strstr(cls, want_class) && GetParent(hwnd) == NULL) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;

    for (k = 0; k < 2 && !clicked; k++) {
        if (AccessibleObjectFromWindow(hwnd, objs[k], &IID_IAccessible, (void **)&acc) != S_OK || !acc) continue;
        V_VT(&self) = VT_I4; V_I4(&self) = CHILDID_SELF;
        walk(acc, self, 0);
        IAccessible_Release(acc); acc = NULL;
    }
    EnumChildWindows(hwnd, visit, 0);
    return !clicked;
}

/* The names worth pressing are localised, and an argv[] that came through the
 * ANSI command line has already lost them -- "确定" arrives as "??" and matches
 * nothing. Take the wide command line and convert it ourselves. */
static char *arg_utf8(const WCHAR *w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *s = malloc(n > 0 ? n : 1);
    if (s) WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    return s;
}

int main(int argc, char **argv)
{
    int wargc = 0;
    WCHAR **wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);

    if (wargc > 2 && !wcscmp(wargv[1], L"-type")) {
        if (wargc < 4) { fprintf(stderr, "usage: uiclick -type <text> <name-substring>\n"); return 2; }
        type_value = arg_utf8(wargv[2]);
        want_name = arg_utf8(wargv[3]);
        want_class = wargc > 4 ? arg_utf8(wargv[4]) : NULL;
    } else if (wargc > 2 && !wcscmp(wargv[1], L"-set")) {
        if (wargc < 4) { fprintf(stderr, "usage: uiclick -set <value> <name-substring>\n"); return 2; }
        set_value = arg_utf8(wargv[2]);
        want_name = arg_utf8(wargv[3]);
        want_class = wargc > 4 ? arg_utf8(wargv[4]) : NULL;
    } else if (wargc > 2 && !wcscmp(wargv[1], L"-click")) {
        /* Some Office controls answer S_OK to accDoDefaultAction and do
         * nothing; this skips straight to a real click. */
        force_click = 1;
        want_name = arg_utf8(wargv[2]);
        want_class = wargc > 3 ? arg_utf8(wargv[3]) : NULL;
    } else {
        if (wargc < 2) { fprintf(stderr, "usage: uiclick [-click|-set <value>|-type <text>] <name-substring> [class-substring]\n"); return 2; }
        want_name = arg_utf8(wargv[1]);
        want_class = wargc > 2 ? arg_utf8(wargv[2]) : NULL;
    }
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    exact_pass = 1;
    EnumWindows(visit, 0);
    if (!clicked) { exact_pass = 0; EnumWindows(visit, 0); }
    if (!clicked) {           /* third pass: show what was there instead */
        printf("no match for \"%s\"; candidates:\n", want_name);
        listed = 1;
        EnumWindows(visit, 0);
    }
    CoUninitialize();
    return clicked ? 0 : 1;
}
