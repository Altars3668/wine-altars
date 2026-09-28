/*
 * What one process can do with a menu another process made, as OLE's in-place activation has a container and a server
 * do with the shared menu: read it (IsMenu, GetMenuInfo, GetMenuItemCount, GetMenuItemInfo with its text,
 * GetMenuString, GetMenuState, GetMenuItemID, GetSubMenu), change it (SetMenuInfo, InsertMenu, InsertMenuItem with a
 * popup of its own, ModifyMenu, CheckMenuItem, EnableMenuItem, SetMenuDefaultItem, AppendMenu to a popup of it,
 * DeleteMenu, RemoveMenu), put one on a window of its own as its menu bar -- which the window makes room for and
 * lays out -- and destroy that window, track a popup of it with keys posted beforehand, and destroy one; and what
 * the process that made the menus sees of all that, while the other process runs and after it has exited.
 *
 * Both wait as threads with windows do, taking the messages sent to them meanwhile.
 *
 * menuprobe            makes the menus and runs itself as the other process
 * menuprobe child ...  the other process, given the menus
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static HMENU menu, popup, doomed, bar;

static const char *name(HMENU handle)
{
    if (!handle) return "none";
    if (handle == menu) return "M";
    if (handle == popup) return "P";
    if (handle == doomed) return "D";
    if (handle == bar) return "B";
    return IsMenu(handle) ? "another menu" : "an invalid handle";
}

static void dump(const char *what, HMENU handle)
{
    int i, count;

    SetLastError(0xdeadbeef);
    count = GetMenuItemCount(handle);
    printf("  %s: %d items%s", what, count, count < 0 ? "" : "\n");
    if (count < 0) printf(", error %lu\n", GetLastError());
    for (i = 0; i < count; i++)
    {
        MENUITEMINFOW info;
        WCHAR text[64];

        memset(&info, 0, sizeof(info));
        info.cbSize = sizeof(info);
        info.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID | MIIM_SUBMENU | MIIM_DATA | MIIM_STRING;
        info.dwTypeData = text;
        info.cch = ARRAY_SIZE(text);
        text[0] = 0;
        SetLastError(0xdeadbeef);
        if (!GetMenuItemInfoW(handle, i, TRUE, &info))
        {
            printf("    %d: GetMenuItemInfo failed, error %lu\n", i, GetLastError());
            continue;
        }
        if (info.hSubMenu && info.wID == (UINT)(ULONG_PTR)info.hSubMenu)
            printf("    %d: type %#x state %#x id =submenu submenu %s data %#Ix text \"%ls\" (%u)\n", i, info.fType,
                   info.fState, name(info.hSubMenu), info.dwItemData, text, info.cch);
        else
            printf("    %d: type %#x state %#x id %u submenu %s data %#Ix text \"%ls\" (%u)\n", i, info.fType,
                   info.fState, info.wID, name(info.hSubMenu), info.dwItemData, text, info.cch);
        if (info.hSubMenu && info.hSubMenu != popup && info.hSubMenu != menu && IsMenu(info.hSubMenu))
        {
            char sub[64];
            snprintf(sub, sizeof(sub), "its submenu (item %d)", i);
            dump(sub, info.hSubMenu);
        }
    }
}

static void show_info(const char *what, HMENU handle)
{
    MENUINFO info;
    BOOL ret;

    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = MIM_MAXHEIGHT | MIM_HELPID | MIM_MENUDATA | MIM_STYLE;
    SetLastError(0xdeadbeef);
    ret = GetMenuInfo(handle, &info);
    if (ret) printf("  %s: GetMenuInfo style %#lx max height %u help id %lu data %#Ix\n", what, info.dwStyle,
                    info.cyMax, info.dwContextHelpID, info.dwMenuData);
    else printf("  %s: GetMenuInfo failed, error %lu\n", what, GetLastError());
}

/* wait as a thread with windows does, taking the messages sent to it meanwhile */
static DWORD wait_pumping(HANDLE handle, DWORD timeout)
{
    DWORD start = GetTickCount(), elapsed, ret;
    MSG msg;

    for (;;)
    {
        if ((elapsed = GetTickCount() - start) >= timeout) return WAIT_TIMEOUT;
        ret = MsgWaitForMultipleObjects(1, &handle, FALSE, timeout - elapsed, QS_ALLINPUT);
        if (ret != WAIT_OBJECT_0 + 1) return ret;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
    }
}

#define report(what, expr) do { DWORD_PTR r_; SetLastError(0xdeadbeef); r_ = (DWORD_PTR)(expr); \
    printf("  %s: %#Ix, error %lu\n", what, r_, r_ ? 0 : GetLastError()); } while (0)

static HMENU initmenupopup, menuselect;

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITMENUPOPUP: initmenupopup = (HMENU)wparam; break;
    case WM_MENUSELECT: if (lparam) menuselect = (HMENU)lparam; break;
    case WM_TIMER: EndMenu(); break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static int child(char **argv)
{
    MENUITEMINFOW info;
    WCHAR text[64];
    HANDLE done, go;
    HMENU mine;
    HWND hwnd;
    int ret;

    menu = (HMENU)(ULONG_PTR)strtoull(argv[2], NULL, 16);
    popup = (HMENU)(ULONG_PTR)strtoull(argv[3], NULL, 16);
    doomed = (HMENU)(ULONG_PTR)strtoull(argv[4], NULL, 16);
    bar = (HMENU)(ULONG_PTR)strtoull(argv[5], NULL, 16);
    done = OpenEventA(EVENT_MODIFY_STATE, FALSE, "menuprobe-child-done");
    go = OpenEventA(SYNCHRONIZE, FALSE, "menuprobe-child-exit");

    printf("the other process reads:\n");
    printf("  IsMenu: M %d, P %d, D %d, B %d\n", IsMenu(menu), IsMenu(popup), IsMenu(doomed), IsMenu(bar));
    show_info("M", menu);
    dump("M", menu);
    text[0] = 0;
    ret = GetMenuStringW(menu, 0, text, ARRAY_SIZE(text), MF_BYPOSITION);
    printf("  GetMenuString(position 0): %d \"%ls\"\n", ret, text);
    text[0] = 0;
    ret = GetMenuStringW(menu, 101, text, 4, MF_BYCOMMAND);
    printf("  GetMenuString(id 101, room for 4): %d \"%ls\"\n", ret, text);
    printf("  GetMenuState(id 101): %#x, of the popup: %#x\n", GetMenuState(menu, 101, MF_BYCOMMAND),
           GetMenuState(menu, 1, MF_BYPOSITION));
    printf("  GetMenuItemID(0): %u, GetSubMenu(1): %s\n", GetMenuItemID(menu, 0), name(GetSubMenu(menu, 1)));

    printf("the other process changes:\n");
    {
        MENUINFO mi;
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        mi.fMask = MIM_MENUDATA;
        mi.dwMenuData = 0x5678;
        report("SetMenuInfo(M, data 0x5678)", SetMenuInfo(menu, &mi));
    }
    report("InsertMenu(M, position 0, \"Child item\")",
           InsertMenuW(menu, 0, MF_BYPOSITION | MF_STRING, 301, L"Child item"));
    mine = CreatePopupMenu();
    AppendMenuW(mine, MF_STRING, 401, L"Child sub item");
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = MIIM_STRING | MIIM_SUBMENU | MIIM_ID | MIIM_DATA;
    info.wID = 402;
    info.hSubMenu = mine;
    info.dwItemData = 0xc0de;
    info.dwTypeData = (WCHAR *)L"Child popup";
    report("InsertMenuItem(M, at the end, a popup of its own)", InsertMenuItemW(menu, -1, TRUE, &info));
    report("ModifyMenu(M, id 101 -> 102 \"First, modified\")",
           ModifyMenuW(menu, 101, MF_BYCOMMAND | MF_STRING, 102, L"First, modified"));
    printf("  CheckMenuItem(M, id 102): previous %#lx\n", CheckMenuItem(menu, 102, MF_BYCOMMAND | MF_CHECKED));
    printf("  EnableMenuItem(M, id 102, grayed): previous %d\n", EnableMenuItem(menu, 102, MF_BYCOMMAND | MF_GRAYED));
    report("SetMenuDefaultItem(M, id 102)", SetMenuDefaultItem(menu, 102, FALSE));
    printf("  GetMenuDefaultItem(M): %d\n", (int)GetMenuDefaultItem(menu, FALSE, 0));
    report("AppendMenu(P, \"Sub item from the child\")", AppendMenuW(popup, MF_STRING, 202, L"Sub item from the child"));
    report("DeleteMenu(M, id 103)", DeleteMenu(menu, 103, MF_BYCOMMAND));
    show_info("M", menu);
    dump("M", menu);

    printf("the other process puts B on a window of its own:\n");
    {
        WNDCLASSW cls = {0};
        cls.lpfnWndProc = wndproc;
        cls.hInstance = GetModuleHandleW(NULL);
        cls.lpszClassName = L"menuprobe";
        RegisterClassW(&cls);
    }
    hwnd = CreateWindowW(L"menuprobe", L"menuprobe", WS_OVERLAPPEDWINDOW, 0, 0, 200, 200, NULL, NULL,
                         GetModuleHandleW(NULL), NULL);
    {
        RECT before, after, item_rect;
        MENUBARINFO mbi;

        GetClientRect(hwnd, &before);
        report("SetMenu(its window, B)", SetMenu(hwnd, bar));
        GetClientRect(hwnd, &after);
        printf("  GetMenu(its window): %s\n", name(GetMenu(hwnd)));
        printf("  the client area made room for a menu bar: %d\n", after.bottom < before.bottom);
        report("DrawMenuBar", DrawMenuBar(hwnd));
        memset(&mbi, 0, sizeof(mbi));
        mbi.cbSize = sizeof(mbi);
        SetLastError(0xdeadbeef);
        if (GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi))
            printf("  GetMenuBarInfo: menu %s, bar %s, room made less the bar's height: %ld\n", name(mbi.hMenu),
                   mbi.rcBar.bottom > mbi.rcBar.top ? "laid out" : "empty",
                   (before.bottom - after.bottom) - (mbi.rcBar.bottom - mbi.rcBar.top));
        else printf("  GetMenuBarInfo failed, error %lu\n", GetLastError());
        SetLastError(0xdeadbeef);
        if (GetMenuItemRect(hwnd, bar, 0, &item_rect))
            printf("  GetMenuItemRect(B, 0): %s\n", item_rect.right > item_rect.left ? "laid out" : "empty");
        else printf("  GetMenuItemRect(B, 0) failed, error %lu\n", GetLastError());
    }

    printf("the other process tracks P with keys posted beforehand:\n");
    {
        int id;

        PostMessageW(hwnd, WM_KEYDOWN, VK_DOWN, 0);
        PostMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0);
        SetTimer(hwnd, 1, 3000, NULL);
        SetLastError(0xdeadbeef);
        id = TrackPopupMenuEx(popup, TPM_RETURNCMD, 10, 10, hwnd, NULL);
        KillTimer(hwnd, 1);
        printf("  TrackPopupMenuEx: %d, error %lu\n", id, id ? 0 : GetLastError());
        printf("  WM_INITMENUPOPUP named %s, WM_MENUSELECT named %s\n", name(initmenupopup), name(menuselect));
    }
    report("DestroyWindow(its window)", DestroyWindow(hwnd));
    printf("  IsMenu(B): %d\n", IsMenu(bar));

    printf("the other process destroys D:\n");
    report("DestroyMenu(D)", DestroyMenu(doomed));
    printf("  IsMenu(D): %d\n", IsMenu(doomed));

    SetEvent(done);
    wait_pumping(go, 10000);
    return 0;
}

int main(int argc, char **argv)
{
    char cmdline[MAX_PATH + 128], path[MAX_PATH];
    PROCESS_INFORMATION pi;
    STARTUPINFOA si;
    HANDLE done, go;
    MENUINFO mi;
    MSG msg;

    setvbuf(stdout, NULL, _IONBF, 0);
    PeekMessageA(&msg, NULL, 0, 0, PM_NOREMOVE);  /* a message queue from the start */
    if (argc > 5 && !strcmp(argv[1], "child")) return child(argv);

    menu = CreateMenu();
    AppendMenuW(menu, MF_STRING, 101, L"&First");
    popup = CreatePopupMenu();
    AppendMenuW(popup, MF_STRING, 201, L"Sub item");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)popup, L"&Popup");
    AppendMenuW(menu, MF_STRING, 103, L"&Third");
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    mi.fMask = MIM_MENUDATA | MIM_STYLE | MIM_HELPID;
    mi.dwMenuData = 0x1234;
    mi.dwStyle = MNS_CHECKORBMP;
    mi.dwContextHelpID = 77;
    SetMenuInfo(menu, &mi);
    doomed = CreatePopupMenu();
    AppendMenuW(doomed, MF_STRING, 501, L"To destroy");
    bar = CreateMenu();
    AppendMenuW(bar, MF_STRING, 601, L"Bar item");

    done = CreateEventA(NULL, TRUE, FALSE, "menuprobe-child-done");
    go = CreateEventA(NULL, TRUE, FALSE, "menuprobe-child-exit");
    GetModuleFileNameA(NULL, path, sizeof(path));
    snprintf(cmdline, sizeof(cmdline), "\"%s\" child %Ix %Ix %Ix %Ix", path, (ULONG_PTR)menu, (ULONG_PTR)popup,
             (ULONG_PTR)doomed, (ULONG_PTR)bar);
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        printf("CreateProcess failed, error %lu\n", GetLastError());
        return 1;
    }
    if (wait_pumping(done, 20000)) printf("the other process never finished its steps\n");

    printf("the process that made the menus, while the other still runs:\n");
    show_info("M", menu);
    dump("M", menu);
    dump("P", popup);
    printf("  IsMenu: D %d, B %d\n", IsMenu(doomed), IsMenu(bar));

    SetEvent(go);
    wait_pumping(pi.hProcess, 20000);
    printf("the process that made the menus, after the other exited:\n");
    dump("M", menu);
    DestroyMenu(menu);
    DestroyMenu(bar);
    printf("done\n");
    return 0;
}
