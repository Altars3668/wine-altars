/*
 * uidump - print what an Office window says, without needing it to paint.
 *
 * Office draws its dialogs itself: a NUIDialog has one NetUIHWND child and no
 * controls, so a screenshot is the only way to read it -- and when the drawing
 * is what is broken, that is exactly what is unavailable. MSAA goes the other
 * way round, asking the application for the text, so it works whether or not a
 * single pixel reached the screen.
 *
 *   uidump [class-substring]
 *
 * With no argument every top-level window is dumped.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <oleacc.h>
#include <stdio.h>
#include <string.h>

static const char *want;

static void put(const char *label, BSTR s)
{
    char a[1024];
    if (!s) return;
    WideCharToMultiByte(CP_UTF8, 0, s, -1, a, sizeof(a), NULL, NULL);
    if (a[0]) printf("%s=\"%s\" ", label, a);
    SysFreeString(s);
}

static const char *role_name(LONG role)
{
    static char buf[64];
    WCHAR w[64];
    UINT n = GetRoleTextW(role, w, ARRAYSIZE(w));
    if (!n) { sprintf(buf, "role%ld", role); return buf; }
    WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, sizeof(buf), NULL, NULL);
    return buf;
}

static void dump_acc(IAccessible *acc, VARIANT self, int depth)
{
    VARIANT *kids;
    LONG count = 0, got = 0, i;
    BSTR s = NULL;
    VARIANT vrole;

    printf("%*s", depth * 2, "");

    /* SUCCEEDED, not == S_OK: MSAA servers routinely answer S_FALSE and still
     * hand back the string, and Office's licensing dialog is one of them --
     * every line of its text was being dropped on the floor here, which made
     * the dialog look like it had nothing to say. */
    if (SUCCEEDED(IAccessible_get_accName(acc, self, &s))) put("name", s);
    s = NULL;
    if (SUCCEEDED(IAccessible_get_accValue(acc, self, &s))) put("value", s);
    s = NULL;
    if (SUCCEEDED(IAccessible_get_accDescription(acc, self, &s))) put("desc", s);
    s = NULL;
    if (SUCCEEDED(IAccessible_get_accHelp(acc, self, &s))) put("help", s);
    VariantInit(&vrole);
    if (IAccessible_get_accRole(acc, self, &vrole) == S_OK && V_VT(&vrole) == VT_I4)
        printf("[%s]", role_name(V_I4(&vrole)));
    {
        /* Screen coordinates, not window-relative: what a synthetic click or
         * XTest event needs is where the pixel actually is, and Office's own
         * controls have no HWND to ask GetWindowRect of. */
        long x = 0, y = 0, w = 0, h = 0;
        if (IAccessible_accLocation(acc, &x, &y, &w, &h, self) == S_OK && (w || h))
            printf(" rect=(%ld,%ld,%ld,%ld)", x, y, w, h);
    }
    printf("\n");

    if (V_I4(&self) != CHILDID_SELF) return;      /* a simple element has no children */
    if (IAccessible_get_accChildCount(acc, &count) != S_OK || count <= 0) return;
    if (depth > 12) return;

    kids = calloc(count, sizeof(*kids));
    if (!kids) return;
    if (AccessibleChildren(acc, 0, count, kids, &got) == S_OK)
    {
        for (i = 0; i < got; i++)
        {
            if (V_VT(&kids[i]) == VT_DISPATCH)
            {
                IAccessible *child = NULL;
                if (IDispatch_QueryInterface(V_DISPATCH(&kids[i]), &IID_IAccessible,
                                             (void **)&child) == S_OK)
                {
                    VARIANT cs;
                    V_VT(&cs) = VT_I4; V_I4(&cs) = CHILDID_SELF;
                    dump_acc(child, cs, depth + 1);
                    IAccessible_Release(child);
                }
                IDispatch_Release(V_DISPATCH(&kids[i]));
            }
            else if (V_VT(&kids[i]) == VT_I4)
            {
                dump_acc(acc, kids[i], depth + 1);
            }
        }
    }
    free(kids);
}

static BOOL CALLBACK enum_top(HWND hwnd, LPARAM param);

static BOOL CALLBACK enum_top(HWND hwnd, LPARAM param)
{
    char cls[128], text[512];
    IAccessible *acc = NULL;
    VARIANT self;
    RECT r;

    GetClassNameA(hwnd, cls, sizeof(cls));
    if (want && !strstr(cls, want) && GetParent(hwnd) == NULL) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;

    GetWindowTextA(hwnd, text, sizeof(text));
    GetWindowRect(hwnd, &r);
    printf("== hwnd %p class %s title \"%s\" %ldx%ld\n", hwnd, cls, text,
           r.right - r.left, r.bottom - r.top);

    /* Office answers WM_GETOBJECT on OBJID_CLIENT for the content and on
     * OBJID_WINDOW only for the frame, so ask for both rather than guess. */
    {
        static const struct { DWORD id; const char *name; } objs[] = {
            { OBJID_WINDOW, "OBJID_WINDOW" }, { OBJID_CLIENT, "OBJID_CLIENT" },
        };
        int k;
        for (k = 0; k < 2; k++)
        {
            HRESULT hr = AccessibleObjectFromWindow(hwnd, objs[k].id, &IID_IAccessible, (void **)&acc);
            printf("  %s -> %#lx\n", objs[k].name, hr);
            if (hr != S_OK || !acc) continue;
            V_VT(&self) = VT_I4; V_I4(&self) = CHILDID_SELF;
            dump_acc(acc, self, 2);
            IAccessible_Release(acc);
            acc = NULL;
        }
    }

    /* and walk the child HWNDs, because the content of a NUIDialog lives in a
     * NetUIHWND that EnumWindows never visits */
    EnumChildWindows(hwnd, enum_top, 0);
    return TRUE;
}

int main(int argc, char **argv)
{
    if (argc > 1) want = argv[1];
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    EnumWindows(enum_top, 0);
    CoUninitialize();
    return 0;
}
