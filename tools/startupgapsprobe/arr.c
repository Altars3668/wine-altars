/* arr: Application Restart and Recovery, Windows Error Reporting's registrations, and the Restart Manager.  Which
 * module exports what; what registering, reading back and unregistering a restart command line and a recovery
 * callback return; where they are kept (the block the PEB's WerRegistrationData points to -- its layout found by
 * looking for the values the probe registered); what WER's file, memory block, metadata, runtime exception module
 * and flag registrations return and how many it takes; reading another process's settings, and the Restart Manager
 * listing, shutting down and restarting a child of the probe that holds a file open.  Prints results only; the child
 * is the probe itself, and only it holds the file the Restart Manager is asked about.
 *
 *     arr.exe                       everything
 *     arr.exe child <tag>           (the child)
 *     arr.exe /arr-restarted <tag>  (the child as the Restart Manager restarts it) */
#include <windows.h>
#include <winternl.h>
#include <restartmanager.h>
#include <stdio.h>
#include <wchar.h>

#define RESTART_ARGS L"/arr-restarted"

static HMODULE kernel32, kernelbase;

static void *get(const char *name)
{
    void *proc = GetProcAddress(kernel32, name);
    if (!proc) proc = GetProcAddress(kernelbase, name);
    return proc;
}

static HRESULT (WINAPI *pRegisterApplicationRestart)(const WCHAR *, DWORD);
static HRESULT (WINAPI *pUnregisterApplicationRestart)(void);
static HRESULT (WINAPI *pGetApplicationRestartSettings)(HANDLE, WCHAR *, DWORD *, DWORD *);
static HRESULT (WINAPI *pRegisterApplicationRecoveryCallback)(APPLICATION_RECOVERY_CALLBACK, void *, DWORD, DWORD);
static HRESULT (WINAPI *pUnregisterApplicationRecoveryCallback)(void);
static HRESULT (WINAPI *pGetApplicationRecoveryCallback)(HANDLE, APPLICATION_RECOVERY_CALLBACK *, void **, DWORD *,
                                                          DWORD *);
static HRESULT (WINAPI *pApplicationRecoveryInProgress)(BOOL *);
static void (WINAPI *pApplicationRecoveryFinished)(BOOL);
static HRESULT (WINAPI *pWerRegisterFile)(const WCHAR *, int, DWORD);
static HRESULT (WINAPI *pWerUnregisterFile)(const WCHAR *);
static HRESULT (WINAPI *pWerRegisterMemoryBlock)(void *, DWORD);
static HRESULT (WINAPI *pWerUnregisterMemoryBlock)(void *);
static HRESULT (WINAPI *pWerRegisterExcludedMemoryBlock)(const void *, DWORD);
static HRESULT (WINAPI *pWerUnregisterExcludedMemoryBlock)(const void *);
static HRESULT (WINAPI *pWerRegisterCustomMetadata)(const WCHAR *, const WCHAR *);
static HRESULT (WINAPI *pWerUnregisterCustomMetadata)(const WCHAR *);
static HRESULT (WINAPI *pWerRegisterRuntimeExceptionModule)(const WCHAR *, void *);
static HRESULT (WINAPI *pWerUnregisterRuntimeExceptionModule)(const WCHAR *, void *);
static HRESULT (WINAPI *pWerSetFlags)(DWORD);
static HRESULT (WINAPI *pWerGetFlags)(HANDLE, DWORD *);

static DWORD WINAPI recovery(void *param)
{
    return 0;
}

static BYTE *wer_block(void)
{
    BYTE *peb = (BYTE *)__readgsqword(0x60);
    return *(BYTE **)(peb + 0x358);
}

static void exports(void)
{
    static const char *names[] =
    {
        "RegisterApplicationRestart", "UnregisterApplicationRestart", "GetApplicationRestartSettings",
        "RegisterApplicationRecoveryCallback", "UnregisterApplicationRecoveryCallback",
        "GetApplicationRecoveryCallback", "ApplicationRecoveryInProgress", "ApplicationRecoveryFinished",
        "WerRegisterFile", "WerUnregisterFile", "WerRegisterMemoryBlock", "WerUnregisterMemoryBlock",
        "WerRegisterExcludedMemoryBlock", "WerUnregisterExcludedMemoryBlock", "WerRegisterCustomMetadata",
        "WerUnregisterCustomMetadata", "WerRegisterRuntimeExceptionModule", "WerUnregisterRuntimeExceptionModule",
        "WerSetFlags", "WerGetFlags", "WerRegisterAppLocalDump", "WerUnregisterAppLocalDump",
    };
    unsigned int i;

    for (i = 0; i < ARRAYSIZE(names); i++)
    {
        BYTE *k32 = (BYTE *)GetProcAddress(kernel32, names[i]), *kb = (BYTE *)GetProcAddress(kernelbase, names[i]);
        printf("export %s: kernel32 %s, kernelbase %s\n", names[i], !k32 ? "no" : k32 == kb ? "forwarded" : "own",
               kb ? "yes" : "no");
    }
}

/* the offsets in the block where a value is, as bytes, a wide string or a pointer-sized number */
static void find_in_block(const char *what, const void *value, size_t size)
{
    BYTE *block = wer_block();
    DWORD length, i;

    if (!block)
    {
        printf("    %s: no block\n", what);
        return;
    }
    length = *(DWORD *)block;
    printf("    %s at", what);
    for (i = 0; i + size <= length && i < 0x100000; i += 2)
        if (!memcmp(block + i, value, size)) printf(" %#lx", i);
    printf("\n");
}

static BYTE snapshot[0x10000];
static DWORD snapshot_len;

static void take_snapshot(void)
{
    BYTE *block = wer_block();
    snapshot_len = 0;
    if (!block) return;
    snapshot_len = min(*(DWORD *)block, sizeof(snapshot));
    memcpy(snapshot, block, snapshot_len);
}

static void show_changes(const char *what)
{
    BYTE *block = wer_block();
    DWORD i, length;

    if (!block)
    {
        printf("    %s: no block\n", what);
        return;
    }
    length = min(*(DWORD *)block, sizeof(snapshot));
    printf("    %s changed:", what);
    for (i = 0; i + 8 <= length; i += 8)
    {
        ULONG64 old = i + 8 <= snapshot_len ? *(ULONG64 *)(snapshot + i) : 0, now = *(ULONG64 *)(block + i);
        if (old == now) continue;
        if (now > 0x10000 && (now >> 48) == 0 && (now & 7) == 0) printf(" %#lx=ptr", i);
        else printf(" %#lx=%llx", i, now);
    }
    printf("\n");
}

static void restart(void)
{
    WCHAR buffer[2048], *longline;
    DWORD size, flags;
    HRESULT hr;
    BYTE *block;
    unsigned int i;

    block = wer_block();
    printf("WerRegistrationData at start: %s\n", block ? "set" : "NULL");
    size = ARRAYSIZE(buffer);
    hr = pGetApplicationRestartSettings(GetCurrentProcess(), buffer, &size, &flags);
    printf("GetApplicationRestartSettings before registering: %#lx size %lu\n", hr, size);

    hr = pRegisterApplicationRestart(L"/arr-probe-line", 0);
    printf("RegisterApplicationRestart: %#lx\n", hr);
    block = wer_block();
    printf("WerRegistrationData: %s", block ? "set" : "NULL");
    if (block)
    {
        DWORD length = *(DWORD *)block;
        printf(", length %#lx, then", length);
        for (i = 4; i < 4 + 40; i++) printf(" %02x", block[i]);
    }
    printf("\n");
    find_in_block("the command line", L"/arr-probe-line", sizeof(L"/arr-probe-line"));
    size = 0;
    flags = 0xdeadbeef;
    hr = pGetApplicationRestartSettings(GetCurrentProcess(), NULL, &size, &flags);
    printf("GetApplicationRestartSettings NULL buffer: %#lx size %lu flags %#lx\n", hr, size, flags);
    size = 5;
    memset(buffer, 0xcc, sizeof(buffer));
    hr = pGetApplicationRestartSettings(GetCurrentProcess(), buffer, &size, &flags);
    printf("  buffer of 5: %#lx size %lu buffer[0] %#x\n", hr, size, buffer[0]);
    size = 15;
    hr = pGetApplicationRestartSettings(GetCurrentProcess(), buffer, &size, &flags);
    printf("  buffer of 15: %#lx size %lu\n", hr, size);
    size = 16;
    flags = 0xdeadbeef;
    hr = pGetApplicationRestartSettings(GetCurrentProcess(), buffer, &size, &flags);
    printf("  buffer of 16: %#lx size %lu flags %#lx line %ls\n", hr, size, flags, hr == S_OK ? buffer : L"-");
    size = ARRAYSIZE(buffer);
    hr = pGetApplicationRestartSettings(GetCurrentProcess(), buffer, &size, NULL);
    printf("  NULL flags: %#lx size %lu\n", hr, size);
    hr = pGetApplicationRestartSettings(GetCurrentProcess(), buffer, NULL, &flags);
    printf("  NULL size: %#lx\n", hr);

    {
        static const DWORD tries[] = { 1, 2, 4, 8, 0xf, 0x10, 0x20, 0x80000000 };
        for (i = 0; i < ARRAYSIZE(tries); i++)
        {
            hr = pRegisterApplicationRestart(L"/x", tries[i]);
            size = ARRAYSIZE(buffer);
            flags = 0xdeadbeef;
            pGetApplicationRestartSettings(GetCurrentProcess(), buffer, &size, &flags);
            printf("RegisterApplicationRestart flags %#lx: %#lx, reads back %#lx\n", tries[i], hr, flags);
        }
    }
    hr = pRegisterApplicationRestart(NULL, 0);
    size = ARRAYSIZE(buffer);
    buffer[0] = 'X';
    printf("RegisterApplicationRestart NULL: %#lx", hr);
    hr = pGetApplicationRestartSettings(GetCurrentProcess(), buffer, &size, &flags);
    printf(", reads back %#lx size %lu first %#x\n", hr, size, buffer[0]);
    hr = pRegisterApplicationRestart(L"", 0);
    size = ARRAYSIZE(buffer);
    printf("RegisterApplicationRestart empty: %#lx", hr);
    hr = pGetApplicationRestartSettings(GetCurrentProcess(), buffer, &size, &flags);
    printf(", reads back %#lx size %lu\n", hr, size);
    longline = malloc(2048 * sizeof(WCHAR));
    {
        static const unsigned int lengths[] = { 1022, 1023, 1024, 1025 };
        for (i = 0; i < ARRAYSIZE(lengths); i++)
        {
            wmemset(longline, 'a', lengths[i]);
            longline[lengths[i]] = 0;
            hr = pRegisterApplicationRestart(longline, 0);
            size = ARRAYSIZE(buffer);
            printf("RegisterApplicationRestart %u characters: %#lx", lengths[i], hr);
            hr = pGetApplicationRestartSettings(GetCurrentProcess(), buffer, &size, &flags);
            printf(", reads back %#lx size %lu\n", hr, size);
        }
    }
    free(longline);
    hr = pUnregisterApplicationRestart();
    printf("UnregisterApplicationRestart: %#lx, block %s\n", hr, wer_block() ? "set" : "NULL");
    size = ARRAYSIZE(buffer);
    hr = pGetApplicationRestartSettings(GetCurrentProcess(), buffer, &size, &flags);
    printf("  then GetApplicationRestartSettings: %#lx size %lu\n", hr, size);
    hr = pUnregisterApplicationRestart();
    printf("  again: %#lx\n", hr);
}

static void recovery_callback(void)
{
    APPLICATION_RECOVERY_CALLBACK callback;
    DWORD ping, flags;
    void *param;
    BOOL cancel;
    HRESULT hr;
    unsigned int i;

    if (!pGetApplicationRecoveryCallback) printf("no GetApplicationRecoveryCallback\n");
    if (pGetApplicationRecoveryCallback)
    {
        hr = pGetApplicationRecoveryCallback(GetCurrentProcess(), &callback, &param, &ping, &flags);
        printf("GetApplicationRecoveryCallback before registering: %#lx\n", hr);
    }
    take_snapshot();
    hr = pRegisterApplicationRecoveryCallback(recovery, (void *)0x12345678, 0, 0);
    printf("RegisterApplicationRecoveryCallback ping 0: %#lx\n", hr);
    {
        void *value = recovery;
        find_in_block("the callback", &value, sizeof(value));
        value = (void *)0x12345678;
        find_in_block("the parameter", &value, sizeof(value));
    }
    show_changes("block");
    if (pGetApplicationRecoveryCallback)
    {
        callback = NULL;
        param = NULL;
        ping = flags = 0xdeadbeef;
        hr = pGetApplicationRecoveryCallback(GetCurrentProcess(), &callback, &param, &ping, &flags);
        printf("  reads back %#lx: callback %s, param %p, ping %lu, flags %#lx\n", hr,
               callback == recovery ? "ours" : "other", param, ping, flags);
        hr = pGetApplicationRecoveryCallback(GetCurrentProcess(), &callback, &param, NULL, NULL);
        printf("  NULL ping and flags: %#lx\n", hr);
    }
    {
        static const struct { DWORD ping, flags; } tries[] = { { 1000, 0 }, { 300000, 0 }, { 300001, 0 },
            { 0xffffffff, 0 }, { 1000, 1 }, { 1000, 0x80000000 } };
        for (i = 0; i < ARRAYSIZE(tries); i++)
        {
            hr = pRegisterApplicationRecoveryCallback(recovery, NULL, tries[i].ping, tries[i].flags);
            ping = flags = 0xdeadbeef;
            if (pGetApplicationRecoveryCallback)
                pGetApplicationRecoveryCallback(GetCurrentProcess(), &callback, &param, &ping, &flags);
            printf("RegisterApplicationRecoveryCallback ping %lu flags %#lx: %#lx, reads back ping %lu flags %#lx\n",
                   tries[i].ping, tries[i].flags, hr, ping, flags);
        }
    }
    hr = pRegisterApplicationRecoveryCallback(NULL, NULL, 1000, 0);
    printf("RegisterApplicationRecoveryCallback NULL: %#lx\n", hr);
    SetLastError(0xdeadbeef);
    cancel = 7;
    hr = pApplicationRecoveryInProgress(&cancel);
    printf("ApplicationRecoveryInProgress outside recovery: %#lx cancel %d error %lu\n", hr, cancel, GetLastError());
    hr = pApplicationRecoveryInProgress(NULL);
    printf("  NULL: %#lx\n", hr);
    SetLastError(0xdeadbeef);
    pApplicationRecoveryFinished(TRUE);
    printf("ApplicationRecoveryFinished outside recovery: error %lu\n", GetLastError());
    if (pUnregisterApplicationRecoveryCallback)
    {
        hr = pUnregisterApplicationRecoveryCallback();
        printf("UnregisterApplicationRecoveryCallback: %#lx\n", hr);
        if (pGetApplicationRecoveryCallback)
        {
            hr = pGetApplicationRecoveryCallback(GetCurrentProcess(), &callback, &param, &ping, &flags);
            printf("  then GetApplicationRecoveryCallback: %#lx\n", hr);
        }
        hr = pUnregisterApplicationRecoveryCallback();
        printf("  again: %#lx\n", hr);
    }
    else printf("no UnregisterApplicationRecoveryCallback\n");
}

static void wer(void)
{
    static BYTE blocks[8][256];
    WCHAR name[64], value[300];
    BYTE *big;
    DWORD flags;
    HRESULT hr, first;
    unsigned int i, n;

    take_snapshot();
    hr = pWerRegisterFile(L"C:\\WineAltarsTest\\arr-none.dat", 2 /* WerRegFileTypeOther */, 3);
    printf("WerRegisterFile a file that does not exist: %#lx\n", hr);
    show_changes("block");
    find_in_block("the file name", L"C:\\WineAltarsTest\\arr-none.dat", sizeof(L"C:\\WineAltarsTest\\arr-none.dat"));
    hr = pWerRegisterFile(L"C:\\WineAltarsTest\\arr-none.dat", 2, 3);
    printf("  again: %#lx\n", hr);
    hr = pWerRegisterFile(L"c:\\winealtarstest\\ARR-NONE.DAT", 2, 3);
    printf("  in other case: %#lx\n", hr);
    hr = pWerUnregisterFile(L"c:\\winealtarstest\\ARR-NONE.DAT");
    printf("  unregister in other case: %#lx\n", hr);
    hr = pWerUnregisterFile(L"C:\\WineAltarsTest\\arr-none.dat");
    printf("  unregister: %#lx\n", hr);
    hr = pWerUnregisterFile(L"C:\\WineAltarsTest\\arr-none.dat");
    printf("  again: %#lx\n", hr);
    hr = pWerRegisterFile(NULL, 2, 0);
    printf("WerRegisterFile NULL: %#lx\n", hr);
    hr = pWerRegisterFile(L"", 2, 0);
    printf("WerRegisterFile empty: %#lx\n", hr);
    hr = pWerRegisterFile(L"arr-relative.dat", 2, 0);
    printf("WerRegisterFile relative: %#lx\n", hr);
    pWerUnregisterFile(L"arr-relative.dat");
    {
        static const int types[] = { 0, 1, 2, 3, 4, 5, 6, 7, 100 };
        for (i = 0; i < ARRAYSIZE(types); i++)
        {
            hr = pWerRegisterFile(L"C:\\WineAltarsTest\\arr-type.dat", types[i], 0);
            printf("WerRegisterFile type %d: %#lx\n", types[i], hr);
            pWerUnregisterFile(L"C:\\WineAltarsTest\\arr-type.dat");
        }
    }
    hr = pWerRegisterFile(L"C:\\WineAltarsTest\\arr-flags.dat", 2, 0x80);
    printf("WerRegisterFile flags 0x80: %#lx\n", hr);
    pWerUnregisterFile(L"C:\\WineAltarsTest\\arr-flags.dat");
    {
        WCHAR longname[400] = L"C:\\WineAltarsTest\\";
        wmemset(longname + 18, 'a', 300);
        longname[318] = 0;
        hr = pWerRegisterFile(longname, 2, 0);
        printf("WerRegisterFile 318 characters: %#lx\n", hr);
        pWerUnregisterFile(longname);
    }
    first = S_OK;
    for (n = 0; n < 1100; n++)
    {
        swprintf(name, ARRAYSIZE(name), L"C:\\WineAltarsTest\\arr-count-%u.dat", n);
        if (FAILED(first = pWerRegisterFile(name, 2, 0))) break;
    }
    printf("WerRegisterFile files taken: %u, then %#lx\n", n, first);
    while (n--)
    {
        swprintf(name, ARRAYSIZE(name), L"C:\\WineAltarsTest\\arr-count-%u.dat", n);
        pWerUnregisterFile(name);
    }

    take_snapshot();
    hr = pWerRegisterMemoryBlock(blocks[0], 128);
    printf("WerRegisterMemoryBlock: %#lx\n", hr);
    show_changes("block");
    hr = pWerRegisterMemoryBlock(blocks[0], 128);
    printf("  again: %#lx\n", hr);
    hr = pWerRegisterMemoryBlock(blocks[0], 64);
    printf("  again, another size: %#lx\n", hr);
    hr = pWerRegisterMemoryBlock(blocks[0] + 8, 64);
    printf("  inside it: %#lx\n", hr);
    pWerUnregisterMemoryBlock(blocks[0] + 8);
    hr = pWerUnregisterMemoryBlock(blocks[0]);
    printf("  unregister: %#lx\n", hr);
    hr = pWerUnregisterMemoryBlock(blocks[0]);
    printf("  again: %#lx\n", hr);
    hr = pWerRegisterMemoryBlock(NULL, 128);
    printf("WerRegisterMemoryBlock NULL: %#lx\n", hr);
    hr = pWerRegisterMemoryBlock(blocks[1], 0);
    printf("WerRegisterMemoryBlock size 0: %#lx\n", hr);
    pWerUnregisterMemoryBlock(blocks[1]);
    big = VirtualAlloc(NULL, 0x40000, MEM_COMMIT, PAGE_READWRITE);
    {
        static const DWORD sizes[] = { 0x10000, 0x10001, 0x20000 };
        for (i = 0; i < ARRAYSIZE(sizes); i++)
        {
            hr = pWerRegisterMemoryBlock(big, sizes[i]);
            printf("WerRegisterMemoryBlock size %#lx: %#lx\n", sizes[i], hr);
            pWerUnregisterMemoryBlock(big);
        }
    }
    hr = pWerRegisterMemoryBlock((void *)0x1000, 128);
    printf("WerRegisterMemoryBlock unmapped: %#lx\n", hr);
    pWerUnregisterMemoryBlock((void *)0x1000);
    first = S_OK;
    for (n = 0; n < 1100; n++)
        if (FAILED(first = pWerRegisterMemoryBlock(big + n * 16, 16))) break;
    printf("WerRegisterMemoryBlock blocks taken: %u, then %#lx\n", n, first);
    while (n--) pWerUnregisterMemoryBlock(big + n * 16);

    if (pWerRegisterExcludedMemoryBlock)
    {
        hr = pWerRegisterExcludedMemoryBlock(blocks[2], 128);
        printf("WerRegisterExcludedMemoryBlock: %#lx", hr);
        hr = pWerRegisterExcludedMemoryBlock(blocks[2], 128);
        printf(", again %#lx", hr);
        hr = pWerUnregisterExcludedMemoryBlock(blocks[2]);
        printf(", unregister %#lx", hr);
        hr = pWerUnregisterExcludedMemoryBlock(blocks[2]);
        printf(", again %#lx\n", hr);
    }

    take_snapshot();
    hr = pWerRegisterCustomMetadata(L"ArrProbeKey", L"value one");
    printf("WerRegisterCustomMetadata: %#lx\n", hr);
    show_changes("block");
    find_in_block("the key", L"ArrProbeKey", sizeof(L"ArrProbeKey"));
    hr = pWerRegisterCustomMetadata(L"ArrProbeKey", L"value two");
    printf("  again, another value: %#lx\n", hr);
    find_in_block("the first value", L"value one", sizeof(L"value one"));
    find_in_block("the second value", L"value two", sizeof(L"value two"));
    hr = pWerRegisterCustomMetadata(L"arrprobekey", L"value three");
    printf("  in other case: %#lx\n", hr);
    pWerUnregisterCustomMetadata(L"arrprobekey");
    hr = pWerUnregisterCustomMetadata(L"ArrProbeKey");
    printf("  unregister: %#lx\n", hr);
    hr = pWerUnregisterCustomMetadata(L"ArrProbeKey");
    printf("  again: %#lx\n", hr);
    {
        static const WCHAR *keys[] = { L"", L"1Key", L"Key 1", L"Key.1", L"Key_1", L"Key-1", L"K\u00e9y", NULL };
        for (i = 0; i < ARRAYSIZE(keys); i++)
        {
            hr = pWerRegisterCustomMetadata(keys[i], L"v");
            printf("WerRegisterCustomMetadata key %ls: %#lx\n", keys[i] ? keys[i] : L"(null)", hr);
            if (SUCCEEDED(hr)) pWerUnregisterCustomMetadata(keys[i]);
        }
        hr = pWerRegisterCustomMetadata(L"Key", NULL);
        printf("WerRegisterCustomMetadata NULL value: %#lx\n", hr);
        if (SUCCEEDED(hr)) pWerUnregisterCustomMetadata(L"Key");
        hr = pWerRegisterCustomMetadata(L"Key", L"");
        printf("WerRegisterCustomMetadata empty value: %#lx\n", hr);
        if (SUCCEEDED(hr)) pWerUnregisterCustomMetadata(L"Key");
    }
    {
        static const unsigned int lengths[] = { 63, 64, 65, 127, 128, 129, 255, 256, 257 };
        for (i = 0; i < ARRAYSIZE(lengths); i++)
        {
            wmemset(name, 'K', 1);
            name[1] = 0;
            if (lengths[i] < ARRAYSIZE(name))
            {
                wmemset(name, 'K', lengths[i]);
                name[lengths[i]] = 0;
                hr = pWerRegisterCustomMetadata(name, L"v");
                printf("WerRegisterCustomMetadata key of %u: %#lx\n", lengths[i], hr);
                if (SUCCEEDED(hr)) pWerUnregisterCustomMetadata(name);
            }
            wmemset(value, 'v', lengths[i]);
            value[lengths[i]] = 0;
            hr = pWerRegisterCustomMetadata(L"Key", value);
            printf("WerRegisterCustomMetadata value of %u: %#lx\n", lengths[i], hr);
            if (SUCCEEDED(hr)) pWerUnregisterCustomMetadata(L"Key");
        }
    }
    first = S_OK;
    for (n = 0; n < 1100; n++)
    {
        swprintf(name, ARRAYSIZE(name), L"ArrKey%u", n);
        if (FAILED(first = pWerRegisterCustomMetadata(name, L"v"))) break;
    }
    printf("WerRegisterCustomMetadata keys taken: %u, then %#lx\n", n, first);
    while (n--)
    {
        swprintf(name, ARRAYSIZE(name), L"ArrKey%u", n);
        pWerUnregisterCustomMetadata(name);
    }

    take_snapshot();
    hr = pWerRegisterRuntimeExceptionModule(L"C:\\WineAltarsTest\\arr-none.dll", (void *)0x1234);
    printf("WerRegisterRuntimeExceptionModule a dll that does not exist: %#lx\n", hr);
    show_changes("block");
    hr = pWerRegisterRuntimeExceptionModule(L"C:\\WineAltarsTest\\arr-none.dll", (void *)0x1234);
    printf("  again: %#lx\n", hr);
    hr = pWerRegisterRuntimeExceptionModule(L"C:\\WineAltarsTest\\arr-none.dll", (void *)0x5678);
    printf("  again, another context: %#lx\n", hr);
    hr = pWerUnregisterRuntimeExceptionModule(L"C:\\WineAltarsTest\\arr-none.dll", (void *)0x9999);
    printf("  unregister with a third context: %#lx\n", hr);
    hr = pWerUnregisterRuntimeExceptionModule(L"C:\\WineAltarsTest\\arr-none.dll", (void *)0x1234);
    printf("  unregister: %#lx\n", hr);
    hr = pWerUnregisterRuntimeExceptionModule(L"C:\\WineAltarsTest\\arr-none.dll", (void *)0x1234);
    printf("  again: %#lx\n", hr);
    pWerUnregisterRuntimeExceptionModule(L"C:\\WineAltarsTest\\arr-none.dll", (void *)0x5678);
    hr = pWerRegisterRuntimeExceptionModule(NULL, NULL);
    printf("WerRegisterRuntimeExceptionModule NULL: %#lx\n", hr);
    hr = pWerRegisterRuntimeExceptionModule(L"arr-relative.dll", NULL);
    printf("WerRegisterRuntimeExceptionModule relative: %#lx\n", hr);
    pWerUnregisterRuntimeExceptionModule(L"arr-relative.dll", NULL);
    first = S_OK;
    for (n = 0; n < 200; n++)
    {
        swprintf(name, ARRAYSIZE(name), L"C:\\WineAltarsTest\\arr-%u.dll", n);
        if (FAILED(first = pWerRegisterRuntimeExceptionModule(name, NULL))) break;
    }
    printf("WerRegisterRuntimeExceptionModule modules taken: %u, then %#lx\n", n, first);
    while (n--)
    {
        swprintf(name, ARRAYSIZE(name), L"C:\\WineAltarsTest\\arr-%u.dll", n);
        pWerUnregisterRuntimeExceptionModule(name, NULL);
    }

    flags = 0xdeadbeef;
    hr = pWerGetFlags(GetCurrentProcess(), &flags);
    printf("WerGetFlags before setting: %#lx flags %#lx\n", hr, flags);
    take_snapshot();
    {
        static const DWORD tries[] = { 0, 1, 0x1f, 0x7ff, 0x800, 0x80000000 };
        for (i = 0; i < ARRAYSIZE(tries); i++)
        {
            hr = pWerSetFlags(tries[i]);
            flags = 0xdeadbeef;
            pWerGetFlags(GetCurrentProcess(), &flags);
            printf("WerSetFlags %#lx: %#lx, reads back %#lx\n", tries[i], hr, flags);
        }
    }
    show_changes("block after the flags");
    hr = pWerGetFlags(GetCurrentProcess(), NULL);
    printf("WerGetFlags NULL: %#lx\n", hr);
    hr = pWerGetFlags(NULL, &flags);
    printf("WerGetFlags NULL process: %#lx\n", hr);
    pWerSetFlags(0);
}

/* the child: register, hold a file, log the messages a hidden window gets, wait to be told to go */
static FILE *child_log;

static LRESULT CALLBACK child_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_QUERYENDSESSION:
    case WM_ENDSESSION:
    case WM_CLOSE:
    case WM_DESTROY:
    case WM_QUIT:
        fprintf(child_log, "message %#x wparam %#llx lparam %#llx\n", msg, (ULONG64)wparam, (ULONG64)lparam);
        fflush(child_log);
        if (msg == WM_ENDSESSION && wparam) ExitProcess(5);
        if (msg == WM_QUERYENDSESSION) return TRUE;
        break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static BOOL WINAPI child_ctrl(DWORD type)
{
    fprintf(child_log, "console control %lu\n", type);
    fflush(child_log);
    return FALSE;
}

static int child(const WCHAR *tag)
{
    WNDCLASSW cls = { 0 };
    WCHAR path[64];
    HANDLE file, ready, done;
    HRESULT hr;
    MSG msg;

    swprintf(path, ARRAYSIZE(path), L"arr-child-%ls.log", tag);
    child_log = _wfopen(path, L"w");
    swprintf(path, ARRAYSIZE(path), L"arr-held-%ls.tmp", tag);
    file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    hr = pRegisterApplicationRestart(RESTART_ARGS L" child-tag", RESTART_NO_PATCH);
    fprintf(child_log, "child: file %s, restart %#lx", file != INVALID_HANDLE_VALUE ? "held" : "not opened", hr);
    hr = pRegisterApplicationRecoveryCallback(recovery, (void *)0x777, 2000, 0);
    fprintf(child_log, ", recovery %#lx", hr);
    hr = pWerSetFlags(3 /* NOHEAP | QUEUE */);
    fprintf(child_log, ", wer flags %#lx\n", hr);
    fflush(child_log);
    SetConsoleCtrlHandler(child_ctrl, TRUE);
    cls.lpfnWndProc = child_proc;
    cls.lpszClassName = L"arr_child";
    RegisterClassW(&cls);
    CreateWindowExW(0, L"arr_child", L"arr child", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
    swprintf(path, ARRAYSIZE(path), L"arr-ready-%ls", tag);
    ready = CreateEventW(NULL, TRUE, FALSE, path);
    swprintf(path, ARRAYSIZE(path), L"arr-done-%ls", tag);
    done = CreateEventW(NULL, TRUE, FALSE, path);
    SetEvent(ready);
    for (;;)
    {
        DWORD ret = MsgWaitForMultipleObjects(1, &done, FALSE, 60000, QS_ALLINPUT);
        if (ret != WAIT_OBJECT_0 + 1) break;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                fprintf(child_log, "WM_QUIT\n");
                fflush(child_log);
                return 6;
            }
            DispatchMessageW(&msg);
        }
    }
    fprintf(child_log, "child: told to go\n");
    fclose(child_log);
    CloseHandle(file);
    return 0;
}

static int restarted(int argc, WCHAR **argv)
{
    FILE *f = _wfopen(L"arr-restarted.txt", L"w");
    int i;

    if (!f) return 1;
    fprintf(f, "restarted with %d arguments:", argc - 1);
    for (i = 1; i < argc; i++) fprintf(f, " [%ls]", argv[i]);
    fprintf(f, "; command line %s the image path\n",
            wcsstr(GetCommandLineW(), L"arr.exe") ? "has" : "lacks");
    fclose(f);
    return 0;
}

static void restart_manager(void)
{
    DWORD (WINAPI *pRmStartSession)(DWORD *, DWORD, WCHAR *);
    DWORD (WINAPI *pRmEndSession)(DWORD);
    DWORD (WINAPI *pRmJoinSession)(DWORD *, const WCHAR *);
    DWORD (WINAPI *pRmRegisterResources)(DWORD, UINT, const WCHAR **, UINT, RM_UNIQUE_PROCESS *, UINT, const WCHAR **);
    DWORD (WINAPI *pRmGetList)(DWORD, UINT *, UINT *, RM_PROCESS_INFO *, DWORD *);
    DWORD (WINAPI *pRmShutdown)(DWORD, ULONG, RM_WRITE_STATUS_CALLBACK);
    DWORD (WINAPI *pRmRestart)(DWORD, DWORD, RM_WRITE_STATUS_CALLBACK);
    HMODULE rm = LoadLibraryW(L"rstrtmgr.dll");
    WCHAR cmdline[MAX_PATH + 64], exe[MAX_PATH], tag[16], path[64], key[CCH_RM_SESSION_KEY + 1];
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    HANDLE ready, done, process;
    RM_PROCESS_INFO info[8];
    RM_UNIQUE_PROCESS unique;
    FILETIME created, dummy;
    DWORD session, session2, ret, reasons, size, flags, i, exit_code;
    UINT needed, count;
    const WCHAR *files[1];
    WCHAR buffer[2048];
    HRESULT hr;
    char line[256];
    FILE *f;

    swprintf(tag, ARRAYSIZE(tag), L"%lu", GetCurrentProcessId());
    GetModuleFileNameW(NULL, exe, ARRAYSIZE(exe));
    swprintf(cmdline, ARRAYSIZE(cmdline), L"\"%ls\" child %ls", exe, tag);
    swprintf(path, ARRAYSIZE(path), L"arr-ready-%ls", tag);
    ready = CreateEventW(NULL, TRUE, FALSE, path);
    swprintf(path, ARRAYSIZE(path), L"arr-done-%ls", tag);
    done = CreateEventW(NULL, TRUE, FALSE, path);
    DeleteFileW(L"arr-restarted.txt");
    if (!CreateProcessW(exe, cmdline, NULL, NULL, FALSE, DETACHED_PROCESS, NULL, NULL, &si, &pi))
    {
        printf("CreateProcess failed %lu\n", GetLastError());
        return;
    }
    if (WaitForSingleObject(ready, 10000))
    {
        printf("the child did not get ready\n");
        TerminateProcess(pi.hProcess, 1);
        return;
    }

    {
        static const struct { DWORD access; const char *what; } accesses[] =
        {
            { PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, "query+read" },
            { PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, "limited+read" },
            { PROCESS_QUERY_LIMITED_INFORMATION, "limited" },
            { PROCESS_VM_READ, "read" },
        };
        for (i = 0; i < ARRAYSIZE(accesses); i++)
        {
            process = OpenProcess(accesses[i].access, FALSE, pi.dwProcessId);
            size = ARRAYSIZE(buffer);
            flags = 0xdeadbeef;
            hr = pGetApplicationRestartSettings(process, buffer, &size, &flags);
            printf("another process's restart settings, %s: %#lx size %lu flags %#lx line %ls\n", accesses[i].what,
                   hr, size, flags, hr == S_OK ? buffer : L"-");
            if (pGetApplicationRecoveryCallback)
            {
                APPLICATION_RECOVERY_CALLBACK callback = NULL;
                void *param = NULL;
                DWORD ping = 0;
                hr = pGetApplicationRecoveryCallback(process, &callback, &param, &ping, &flags);
                printf("  recovery callback: %#lx callback %s param %p ping %lu flags %#lx\n", hr,
                       callback == recovery ? "at the same address" : callback ? "other" : "NULL", param, ping, flags);
            }
            flags = 0xdeadbeef;
            hr = pWerGetFlags(process, &flags);
            printf("  WerGetFlags: %#lx flags %#lx\n", hr, flags);
            CloseHandle(process);
        }
    }

    if (!rm)
    {
        printf("no rstrtmgr.dll\n");
        SetEvent(done);
        return;
    }
    pRmStartSession = (void *)GetProcAddress(rm, "RmStartSession");
    pRmEndSession = (void *)GetProcAddress(rm, "RmEndSession");
    pRmJoinSession = (void *)GetProcAddress(rm, "RmJoinSession");
    pRmRegisterResources = (void *)GetProcAddress(rm, "RmRegisterResources");
    pRmGetList = (void *)GetProcAddress(rm, "RmGetList");
    pRmShutdown = (void *)GetProcAddress(rm, "RmShutdown");
    pRmRestart = (void *)GetProcAddress(rm, "RmRestart");
    memset(key, 0, sizeof(key));
    ret = pRmStartSession(&session, 0, key);
    printf("RmStartSession: %lu, key of %u characters\n", ret, (unsigned int)wcslen(key));
    ret = pRmJoinSession(&session2, key);
    printf("RmJoinSession from the same process: %lu\n", ret);
    if (!ret) pRmEndSession(session2);
    swprintf(path, ARRAYSIZE(path), L"arr-held-%ls.tmp", tag);
    GetFullPathNameW(path, ARRAYSIZE(buffer), buffer, NULL);
    files[0] = buffer;
    needed = count = 0;
    ret = pRmGetList(session, &needed, &count, NULL, &reasons);
    printf("RmGetList with nothing registered: %lu needed %u reasons %#lx\n", ret, needed, reasons);
    ret = pRmRegisterResources(session, 1, files, 0, NULL, 0, NULL);
    printf("RmRegisterResources the held file: %lu\n", ret);
    needed = count = 0;
    reasons = 0xdeadbeef;
    ret = pRmGetList(session, &needed, &count, NULL, &reasons);
    printf("RmGetList no buffer: %lu needed %u count %u reasons %#lx\n", ret, needed, count, reasons);
    count = ARRAYSIZE(info);
    memset(info, 0, sizeof(info));
    ret = pRmGetList(session, &needed, &count, info, &reasons);
    printf("RmGetList: %lu needed %u count %u reasons %#lx\n", ret, needed, count, reasons);
    GetProcessTimes(pi.hProcess, &created, &dummy, &dummy, &dummy);
    for (i = 0; !ret && i < count; i++)
    {
        DWORD own_session = 0;
        ProcessIdToSessionId(GetCurrentProcessId(), &own_session);
        printf("  process: %s, start time %s, app name %ls, service [%ls], type %d, status %#lx, session %s,"
               " restartable %d\n", info[i].Process.dwProcessId == pi.dwProcessId ? "the child" : "another",
               !memcmp(&info[i].Process.ProcessStartTime, &created, sizeof(created)) ? "the child's" : "other",
               info[i].strAppName, info[i].strServiceShortName, info[i].ApplicationType, info[i].AppStatus,
               info[i].TSSessionId == own_session ? "ours" : "other", info[i].bRestartable);
    }
    unique.dwProcessId = pi.dwProcessId;
    unique.ProcessStartTime = created;
    {
        DWORD session3;
        ret = pRmStartSession(&session3, 0, key);
        ret = pRmRegisterResources(session3, 0, NULL, 1, &unique, 0, NULL);
        count = ARRAYSIZE(info);
        ret = pRmGetList(session3, &needed, &count, info, &reasons);
        printf("RmGetList for the child registered as a process: %lu count %u, %s, type %d, restartable %d\n", ret,
               count, count && info[0].Process.dwProcessId == pi.dwProcessId ? "the child" : "-",
               count ? info[0].ApplicationType : -1, count ? info[0].bRestartable : -1);
        pRmEndSession(session3);
    }

    ret = pRmShutdown(session, 0, NULL);
    printf("RmShutdown: %lu\n", ret);
    ret = WaitForSingleObject(pi.hProcess, 10000);
    GetExitCodeProcess(pi.hProcess, &exit_code);
    printf("  the child %s, exit code %lu\n", ret ? "is still there" : "ended", exit_code);
    count = ARRAYSIZE(info);
    ret = pRmGetList(session, &needed, &count, info, &reasons);
    printf("RmGetList after the shutdown: %lu count %u status %#lx\n", ret, count, count ? info[0].AppStatus : 0);
    if (exit_code != STILL_ACTIVE)
    {
        ret = pRmRestart(session, 0, NULL);
        printf("RmRestart: %lu\n", ret);
        for (i = 0; i < 100; i++)
        {
            if (GetFileAttributesW(L"arr-restarted.txt") != INVALID_FILE_ATTRIBUTES) break;
            Sleep(100);
        }
        Sleep(200);
        if ((f = _wfopen(L"arr-restarted.txt", L"r")))
        {
            while (fgets(line, sizeof(line), f)) printf("  %s", line);
            fclose(f);
        }
        else printf("  nothing restarted\n");
        count = ARRAYSIZE(info);
        ret = pRmGetList(session, &needed, &count, info, &reasons);
        printf("RmGetList after the restart: %lu count %u status %#lx\n", ret, count, count ? info[0].AppStatus : 0);
    }
    ret = pRmEndSession(session);
    printf("RmEndSession: %lu, again %lu\n", ret, pRmEndSession(session));

    SetEvent(done);
    WaitForSingleObject(pi.hProcess, 10000);
    TerminateProcess(pi.hProcess, 1);
    swprintf(path, ARRAYSIZE(path), L"arr-child-%ls.log", tag);
    if ((f = _wfopen(path, L"r")))
    {
        while (fgets(line, sizeof(line), f)) printf("  child log: %s", line);
        fclose(f);
    }
    DeleteFileW(path);
    swprintf(path, ARRAYSIZE(path), L"arr-held-%ls.tmp", tag);
    DeleteFileW(path);
    DeleteFileW(L"arr-restarted.txt");
}

int wmain(int argc, WCHAR **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    kernel32 = GetModuleHandleW(L"kernel32.dll");
    kernelbase = GetModuleHandleW(L"kernelbase.dll");
    pRegisterApplicationRestart = get("RegisterApplicationRestart");
    pUnregisterApplicationRestart = get("UnregisterApplicationRestart");
    pGetApplicationRestartSettings = get("GetApplicationRestartSettings");
    pRegisterApplicationRecoveryCallback = get("RegisterApplicationRecoveryCallback");
    pUnregisterApplicationRecoveryCallback = get("UnregisterApplicationRecoveryCallback");
    pGetApplicationRecoveryCallback = get("GetApplicationRecoveryCallback");
    pApplicationRecoveryInProgress = get("ApplicationRecoveryInProgress");
    pApplicationRecoveryFinished = get("ApplicationRecoveryFinished");
    pWerRegisterFile = get("WerRegisterFile");
    pWerUnregisterFile = get("WerUnregisterFile");
    pWerRegisterMemoryBlock = get("WerRegisterMemoryBlock");
    pWerUnregisterMemoryBlock = get("WerUnregisterMemoryBlock");
    pWerRegisterExcludedMemoryBlock = get("WerRegisterExcludedMemoryBlock");
    pWerUnregisterExcludedMemoryBlock = get("WerUnregisterExcludedMemoryBlock");
    pWerRegisterCustomMetadata = get("WerRegisterCustomMetadata");
    pWerUnregisterCustomMetadata = get("WerUnregisterCustomMetadata");
    pWerRegisterRuntimeExceptionModule = get("WerRegisterRuntimeExceptionModule");
    pWerUnregisterRuntimeExceptionModule = get("WerUnregisterRuntimeExceptionModule");
    pWerSetFlags = get("WerSetFlags");
    pWerGetFlags = get("WerGetFlags");

    if (argc >= 3 && !wcscmp(argv[1], L"child")) return child(argv[2]);
    if (argc >= 2 && !wcscmp(argv[1], RESTART_ARGS)) return restarted(argc, argv);

    exports();
    restart();
    recovery_callback();
    wer();
    restart_manager();
    return 0;
}
