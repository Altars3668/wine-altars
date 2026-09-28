/*
 * What comctl32's subclassing helpers answer for a window another thread or another process owns.  The window is
 * made and subclassed here, and the properties it then has are listed; then another thread, and a second copy of
 * this program given the window's handle, ask GetWindowSubclass about it, read the window property the helpers
 * keep, call DefSubclassProc, subclass it and remove the owner's subclass; the owner asks again after each.  The
 * helpers are looked up by ordinal (410-413), as comctl32 5 exports them by ordinal only; "v6" as the first
 * argument uses the version 6 in an activation context instead.
 */
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>

typedef BOOL (WINAPI *set_func)(HWND, SUBCLASSPROC, UINT_PTR, DWORD_PTR);
typedef BOOL (WINAPI *get_func)(HWND, SUBCLASSPROC, UINT_PTR, DWORD_PTR *);
typedef BOOL (WINAPI *remove_func)(HWND, SUBCLASSPROC, UINT_PTR);
typedef LRESULT (WINAPI *def_func)(HWND, UINT, WPARAM, LPARAM);

static set_func pSetWindowSubclass;
static get_func pGetWindowSubclass;
static remove_func pRemoveWindowSubclass;
static def_func pDefSubclassProc;
static HWND window;
static SUBCLASSPROC owner_proc;
static unsigned int subclass_calls;

static LRESULT CALLBACK subclass_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR ref)
{
    if (msg == WM_USER) subclass_calls++;
    return pDefSubclassProc(hwnd, msg, wparam, lparam);
}

static void load_helpers(BOOL v6)
{
    HMODULE module;

    if (v6)
    {
        ACTCTXW ctx = { sizeof(ctx) };
        ULONG_PTR cookie;
        WCHAR path[MAX_PATH];
        HANDLE handle;
        FILE *file;

        /* a manifest asking for comctl32 6 */
        GetTempPathW(MAX_PATH, path);
        wcscat(path, L"subclass-v6.manifest");
        file = _wfopen(path, L"w");
        fputs("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
              "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">\n"
              "<dependency><dependentAssembly><assemblyIdentity type=\"win32\" name=\"Microsoft.Windows.Common-Controls\""
              " version=\"6.0.0.0\" processorArchitecture=\"*\" publicKeyToken=\"6595b64144ccf1df\" language=\"*\"/>"
              "</dependentAssembly></dependency>\n</assembly>\n", file);
        fclose(file);
        ctx.lpSource = path;
        handle = CreateActCtxW(&ctx);
        ActivateActCtx(handle, &cookie);
    }
    module = LoadLibraryA("comctl32.dll");
    pSetWindowSubclass = (set_func)GetProcAddress(module, MAKEINTRESOURCEA(410));
    pGetWindowSubclass = (get_func)GetProcAddress(module, MAKEINTRESOURCEA(411));
    pRemoveWindowSubclass = (remove_func)GetProcAddress(module, MAKEINTRESOURCEA(412));
    pDefSubclassProc = (def_func)GetProcAddress(module, MAKEINTRESOURCEA(413));
}

static void ask(const char *who)
{
    DWORD_PTR ref = 0xdeadbeef;
    BOOL ret;

    SetLastError(0xdeadbeef);
    ret = pGetWindowSubclass(window, owner_proc, 1, &ref);
    printf("%s: GetWindowSubclass of the owner's subclass %d, reference %#Ix, error %lu\n", who, ret, ref,
           GetLastError());
    ref = 0xdeadbeef;
    ret = pGetWindowSubclass(window, owner_proc, 7, &ref);
    printf("%s: GetWindowSubclass of an id never used %d, reference %#Ix\n", who, ret, ref);
    printf("%s: property CC32SubclassInfo %s\n", who, GetPropW(window, L"CC32SubclassInfo") ? "set" : "not set");
    /* the static control answers the length of its text, when the message gets there */
    printf("%s: DefSubclassProc(WM_GETTEXTLENGTH) %Id\n", who, pDefSubclassProc(window, WM_GETTEXTLENGTH, 0, 0));
}

static void modify(const char *who)
{
    BOOL ret;

    SetLastError(0xdeadbeef);
    ret = pSetWindowSubclass(window, subclass_proc, 3, 0x333);
    printf("%s: SetWindowSubclass %d, error %lu\n", who, ret, GetLastError());
    SetLastError(0xdeadbeef);
    ret = pRemoveWindowSubclass(window, owner_proc, 1);
    printf("%s: RemoveWindowSubclass of the owner's %d, error %lu\n", who, ret, GetLastError());
    SetLastError(0xdeadbeef);
    ret = SetPropW(window, L"subclass probe", (HANDLE)1);
    printf("%s: SetPropW %d, error %lu\n", who, ret, GetLastError());
}

static BOOL CALLBACK print_property(HWND hwnd, WCHAR *name, HANDLE data, ULONG_PTR param)
{
    WCHAR atom_name[64];

    if (IS_INTRESOURCE(name))
    {
        if (!GlobalGetAtomNameW(LOWORD(name), atom_name, ARRAY_SIZE(atom_name))) wcscpy(atom_name, L"?");
        printf("  property atom %#x \"%ls\"\n", LOWORD(name), atom_name);
    }
    /* Wine keeps properties of its own, named __wine_*, on every window */
    else if (wcsncmp(name, L"__wine_", 7)) printf("  property \"%ls\"\n", name);
    return TRUE;
}

static void owner_check(const char *after)
{
    DWORD_PTR ref = 0xdeadbeef;
    BOOL ret;

    ret = pGetWindowSubclass(window, subclass_proc, 1, &ref);
    subclass_calls = 0;
    SendMessageW(window, WM_USER, 0, 0);
    printf("owner, after %s: its subclass %d, reference %#Ix, called %u times; another's (id 3) %d; property %s\n",
           after, ret, ref, subclass_calls, pGetWindowSubclass(window, subclass_proc, 3, NULL),
           GetPropW(window, L"subclass probe") ? "set" : "not set");
    RemovePropW(window, L"subclass probe");
}

static DWORD WINAPI thread_proc(void *arg)
{
    ask("another thread");
    modify("another thread");
    return 0;
}

static void wait_pumping(HANDLE handle)
{
    MSG msg;

    while (MsgWaitForMultipleObjects(1, &handle, FALSE, 30000, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
}

int main(int argc, char **argv)
{
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    char cmdline[MAX_PATH + 100], exe[MAX_PATH];
    BOOL v6 = argc > 1 && !strcmp(argv[1], "v6");
    HANDLE thread;
    BOOL ret;

    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc > 4 && !strcmp(argv[2], "child"))
    {
        load_helpers(v6);
        window = (HWND)(ULONG_PTR)strtoull(argv[3], NULL, 16);
        owner_proc = (SUBCLASSPROC)(ULONG_PTR)strtoull(argv[4], NULL, 16);
        ask("another process");
        modify("another process");
        return 0;
    }

    load_helpers(v6);
    printf("comctl32 %s, helpers %s\n", v6 ? "6" : "5", pSetWindowSubclass && pGetWindowSubclass
           && pRemoveWindowSubclass && pDefSubclassProc ? "found" : "missing");
    if (!pSetWindowSubclass) return 1;

    window = CreateWindowW(L"static", L"subclass probe", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
    owner_proc = subclass_proc;
    ret = pSetWindowSubclass(window, subclass_proc, 1, 0x1234);
    printf("owner: SetWindowSubclass %d\n", ret);
    EnumPropsExW(window, print_property, 0);
    ask("owner");

    thread = CreateThread(NULL, 0, thread_proc, NULL, 0, NULL);
    wait_pumping(thread);
    CloseHandle(thread);
    owner_check("another thread");

    /* the other thread may have removed the owner's subclass; put it back */
    pSetWindowSubclass(window, subclass_proc, 1, 0x1234);
    pRemoveWindowSubclass(window, subclass_proc, 3);

    GetModuleFileNameA(NULL, exe, sizeof(exe));
    snprintf(cmdline, sizeof(cmdline), "\"%s\" %s child %Ix %Ix", exe, v6 ? "v6" : "v5", (ULONG_PTR)window,
             (ULONG_PTR)subclass_proc);
    if (CreateProcessA(exe, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        wait_pumping(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    owner_check("another process");

    DestroyWindow(window);
    printf("done\n");
    return 0;
}
