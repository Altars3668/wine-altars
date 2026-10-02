/* arr2: where the WER block in the PEB keeps the restart flags, what GetApplicationRecoveryCallback gives after
 * unregistering, and the Restart Manager with a child registered for restart without RESTART_NO_PATCH: whether it is
 * restartable at once and after a minute, and what restarting it starts, with which command line and directory.
 * Prints results only; the child is the probe itself and writes only in the temporary directory.
 *
 *     arr2.exe                       everything
 *     arr2.exe child <tag>           (the child)
 *     arr2.exe /arr2-restarted <tag> (the child as the Restart Manager restarts it) */
#include <windows.h>
#include <restartmanager.h>
#include <stdio.h>
#include <wchar.h>

static BYTE *wer_block(void)
{
    BYTE *peb = (BYTE *)__readgsqword(0x60);
    return *(BYTE **)(peb + 0x358);
}

static DWORD WINAPI recovery(void *param)
{
    return 0;
}

static void flags_place(void)
{
    static const DWORD tries[] = { 0, 1, 2, 4, 8, 0xf, 0x10, 0x12345678 };
    BYTE before[0x968];
    unsigned int i, j;
    BYTE *block;

    RegisterApplicationRestart(L"/x", 0);
    block = wer_block();
    for (i = 0; i < ARRAYSIZE(tries); i++)
    {
        memcpy(before, block, sizeof(before));
        RegisterApplicationRestart(L"/x", tries[i]);
        printf("flags %#lx: dword at 0x24 %#lx, changed:", tries[i], *(DWORD *)(block + 0x24));
        for (j = 0; j < sizeof(before); j += 4)
            if (*(DWORD *)(before + j) != *(DWORD *)(block + j)) printf(" %#x=%#lx", j, *(DWORD *)(block + j));
        printf("\n");
    }
    memcpy(before, block, sizeof(before));
    UnregisterApplicationRestart();
    printf("unregistering changed:");
    for (j = 0; j < sizeof(before); j += 4)
        if (*(DWORD *)(before + j) != *(DWORD *)(block + j)) printf(" %#x=%#lx", j, *(DWORD *)(block + j));
    printf("\n");
    printf("bytes 0x20-0x30:");
    for (j = 0x20; j < 0x30; j++) printf(" %02x", block[j]);
    printf("\n");

    {
        HRESULT (WINAPI *pGet)(HANDLE, APPLICATION_RECOVERY_CALLBACK *, void **, DWORD *, DWORD *) =
                (void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetApplicationRecoveryCallback");
        HRESULT (WINAPI *pUnregister)(void) =
                (void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "UnregisterApplicationRecoveryCallback");
        APPLICATION_RECOVERY_CALLBACK callback = (void *)0xdead;
        void *param = (void *)0xdead;
        DWORD ping = 0xdead, flags = 0xdead;
        HRESULT hr;

        RegisterApplicationRecoveryCallback(recovery, (void *)0x42, 1000, 3);
        memcpy(before, block, sizeof(before));
        hr = pUnregister();
        printf("UnregisterApplicationRecoveryCallback %#lx changed:", hr);
        for (j = 0; j < sizeof(before); j += 4)
            if (*(DWORD *)(before + j) != *(DWORD *)(block + j)) printf(" %#x=%#lx", j, *(DWORD *)(block + j));
        printf("\n");
        hr = pGet(GetCurrentProcess(), &callback, &param, &ping, &flags);
        printf("  then GetApplicationRecoveryCallback %#lx: callback %p param %p ping %lu flags %#lx\n", hr, callback,
               param, ping, flags);
    }
}

static LRESULT CALLBACK child_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    /* an application that goes when the session ends, as Office does */
    if (msg == WM_ENDSESSION && wparam) ExitProcess(5);
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static int child(const WCHAR *tag)
{
    WCHAR path[MAX_PATH], temp[MAX_PATH];
    HANDLE file, ready, done;
    WNDCLASSW cls = { 0 };
    MSG msg;

    GetTempPathW(ARRAYSIZE(temp), temp);
    swprintf(path, ARRAYSIZE(path), L"%sarr2-held-%ls.tmp", temp, tag);
    file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    RegisterApplicationRestart(L"/arr2-restarted second-arg", 0);
    cls.lpfnWndProc = child_proc;
    cls.lpszClassName = L"arr2_child";
    RegisterClassW(&cls);
    CreateWindowExW(0, L"arr2_child", L"arr2 child", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
    swprintf(path, ARRAYSIZE(path), L"arr2-ready-%ls", tag);
    ready = CreateEventW(NULL, TRUE, FALSE, path);
    swprintf(path, ARRAYSIZE(path), L"arr2-done-%ls", tag);
    done = CreateEventW(NULL, TRUE, FALSE, path);
    SetEvent(ready);
    for (;;)
    {
        DWORD ret = MsgWaitForMultipleObjects(1, &done, FALSE, 180000, QS_ALLINPUT);
        if (ret != WAIT_OBJECT_0 + 1) break;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) return 6;
            DispatchMessageW(&msg);
        }
    }
    CloseHandle(file);
    return 0;
}

static int restarted(int argc, WCHAR **argv)
{
    WCHAR temp[MAX_PATH], path[MAX_PATH], cwd[MAX_PATH];
    FILE *f;
    int i;

    GetTempPathW(ARRAYSIZE(temp), temp);
    swprintf(path, ARRAYSIZE(path), L"%sarr2-restarted.txt", temp);
    if (!(f = _wfopen(path, L"w"))) return 1;
    GetCurrentDirectoryW(ARRAYSIZE(cwd), cwd);
    fprintf(f, "restarted with %d arguments:", argc - 1);
    for (i = 1; i < argc; i++) fprintf(f, " [%ls]", argv[i]);
    fprintf(f, "; the command line %s with the quoted image path; directory %s\n",
            GetCommandLineW()[0] == '"' ? "starts" : "does not start",
            !wcsnicmp(cwd, temp, wcslen(cwd)) ? "the temporary one" : "another");
    fclose(f);
    return 0;
}

static void restart_manager(void)
{
    WCHAR cmdline[MAX_PATH + 64], exe[MAX_PATH], tag[16], path[MAX_PATH], temp[MAX_PATH], key[CCH_RM_SESSION_KEY + 1];
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    HANDLE ready, done;
    RM_PROCESS_INFO info[4];
    const WCHAR *files[1];
    DWORD session, ret, reasons, exit_code, start;
    UINT needed, count;
    char line[256];
    FILE *f;

    GetTempPathW(ARRAYSIZE(temp), temp);
    swprintf(tag, ARRAYSIZE(tag), L"%lu", GetCurrentProcessId());
    GetModuleFileNameW(NULL, exe, ARRAYSIZE(exe));
    swprintf(cmdline, ARRAYSIZE(cmdline), L"\"%ls\" child %ls", exe, tag);
    swprintf(path, ARRAYSIZE(path), L"arr2-ready-%ls", tag);
    ready = CreateEventW(NULL, TRUE, FALSE, path);
    swprintf(path, ARRAYSIZE(path), L"arr2-done-%ls", tag);
    done = CreateEventW(NULL, TRUE, FALSE, path);
    swprintf(path, ARRAYSIZE(path), L"%sarr2-restarted.txt", temp);
    DeleteFileW(path);
    if (!CreateProcessW(exe, cmdline, NULL, NULL, FALSE, DETACHED_PROCESS, NULL, NULL, &si, &pi)) return;
    WaitForSingleObject(ready, 10000);
    start = GetTickCount();

    ret = RmStartSession(&session, 0, key);
    swprintf(path, ARRAYSIZE(path), L"%sarr2-held-%ls.tmp", temp, tag);
    files[0] = path;
    ret = RmRegisterResources(session, 1, files, 0, NULL, 0, NULL);
    count = ARRAYSIZE(info);
    ret = RmGetList(session, &needed, &count, info, &reasons);
    printf("RmGetList at once: %lu count %u, type %d, restartable %d, app name %ls\n", ret, count,
           count ? info[0].ApplicationType : -1, count ? info[0].bRestartable : -1, count ? info[0].strAppName : L"-");
    while (GetTickCount() - start < 2000) Sleep(1000);
    count = ARRAYSIZE(info);
    ret = RmGetList(session, &needed, &count, info, &reasons);
    printf("RmGetList after two seconds: %lu count %u, restartable %d\n", ret, count, count ? info[0].bRestartable : -1);
    ret = RmShutdown(session, 0, NULL);
    WaitForSingleObject(pi.hProcess, 15000);
    GetExitCodeProcess(pi.hProcess, &exit_code);
    printf("RmShutdown: %lu, the child's exit code %#lx\n", ret, exit_code);
    count = ARRAYSIZE(info);
    ret = RmGetList(session, &needed, &count, info, &reasons);
    printf("RmGetList after the shutdown: %lu count %u status %#lx restartable %d\n", ret, count,
           count ? info[0].AppStatus : 0, count ? info[0].bRestartable : -1);
    ret = RmRestart(session, 0, NULL);
    printf("RmRestart: %lu\n", ret);
    swprintf(path, ARRAYSIZE(path), L"%sarr2-restarted.txt", temp);
    for (start = GetTickCount(); GetTickCount() - start < 10000; Sleep(100))
        if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) break;
    Sleep(300);
    if ((f = _wfopen(path, L"r")))
    {
        while (fgets(line, sizeof(line), f)) printf("  %s", line);
        fclose(f);
        DeleteFileW(path);
    }
    else printf("  nothing restarted\n");
    count = ARRAYSIZE(info);
    ret = RmGetList(session, &needed, &count, info, &reasons);
    printf("RmGetList after the restart: %lu count %u status %#lx\n", ret, count, count ? info[0].AppStatus : 0);
    RmEndSession(session);
    SetEvent(done);
    WaitForSingleObject(pi.hProcess, 5000);
    swprintf(path, ARRAYSIZE(path), L"%sarr2-held-%ls.tmp", temp, tag);
    DeleteFileW(path);
}

int wmain(int argc, WCHAR **argv)
{
    if (argc >= 3 && !wcscmp(argv[1], L"child")) return child(argv[2]);
    if (argc >= 2 && !wcscmp(argv[1], L"/arr2-restarted")) return restarted(argc, argv);
    setvbuf(stdout, NULL, _IONBF, 0);
    flags_place();
    restart_manager();
    return 0;
}
