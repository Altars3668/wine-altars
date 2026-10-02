/* misc: the small functions Word calls at startup that Wine has as stubs, each asked what Word asks and a little
 * around it -- converting byte arrays to strings, version information with the MUI flags, the precise unbiased
 * interrupt time, waitable timers with a tolerable delay, RPC idle cleanup, the join information, license values (and
 * the whole product policy, after "== policy"), heap information classes, LOAD_LIBRARY_REQUIRE_SIGNED_TARGET,
 * mitigation policies given at process creation, package ids, input scopes, WinHTTP's IPv6 fast fallback, a WMI
 * proxy's blanket, interfaces Office asks of system objects, CLSCTX_REMOTE_SERVER, private object security, short
 * names, performance and power information, OaBuildVersion, DllDebugObjectRPCHook, xmllite's properties and persistent
 * thread pool threads.  Prints results only; a user's SIDs come out as <machine>-RID and logon SIDs as <logon>.
 *
 *     misc.exe                  everything
 *     misc.exe child <event>    (the child of the mitigation policy tests) */
#define COBJMACROS
#include <winsock2.h>
#include <windows.h>
#include <winternl.h>
#include <initguid.h>
#include <propvarutil.h>
#include <lm.h>
#include <winhttp.h>
#include <wbemcli.h>
#include <xmllite.h>
#include <sddl.h>
#include <aclapi.h>
#include <powrprof.h>
#include <rpc.h>
#include <stdio.h>
#include <wchar.h>

DEFINE_GUID(IID_office_private, 0xe19c7100, 0x9709, 0x4db7, 0x93, 0x73, 0xe7, 0xb5, 0x18, 0xb4, 0x70, 0x86);
DEFINE_GUID(IID_office_sax, 0xc970c32d, 0x9ffd, 0x45e5, 0xbf, 0x20, 0xc3, 0xcb, 0xaa, 0xb2, 0x62, 0x22);
DEFINE_GUID(CLSID_SAXXMLReader60_, 0x88d96a0c, 0xf192, 0x11d4, 0xa6, 0x5f, 0x00, 0x40, 0x96, 0x32, 0x51, 0xe5);
DEFINE_GUID(CLSID_SAXXMLReader30_, 0x3124c396, 0xfb13, 0x4836, 0xa6, 0xad, 0x13, 0x34, 0xa4, 0x77, 0x02, 0xf1);
DEFINE_GUID(CLSID_DOMDocument60_, 0x88d96a05, 0xf192, 0x11d4, 0xa6, 0x5f, 0x00, 0x40, 0x96, 0x32, 0x51, 0xe5);
DEFINE_GUID(CLSID_MXXMLWriter60_, 0x88d96a0f, 0xf192, 0x11d4, 0xa6, 0x5f, 0x00, 0x40, 0x96, 0x32, 0x51, 0xe5);
DEFINE_GUID(CLSID_StdGlobalInterfaceTable_, 0x00000323, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
DEFINE_GUID(IID_IClientSecurity_, 0x0000013d, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

/* a wide string with what is not printable ASCII as \uXXXX, so that no code page changes it */
static void print_w(const WCHAR *s, int len)
{
    int i;

    for (i = 0; len < 0 ? s[i] != 0 : i < len; i++)
        if (s[i] >= 0x20 && s[i] < 0x7f) putchar(s[i]);
        else printf("\\u%04x", s[i]);
}

static void *proc(const WCHAR *dll, const char *name)
{
    HMODULE module = LoadLibraryW(dll);
    return module ? (void *)GetProcAddress(module, name) : NULL;
}

/* SIDs in a string with the machine's part and logon ids taken out */
static void scrub_sids(WCHAR *s)
{
    WCHAR *p, *q, *end;
    unsigned int dashes;

    while ((p = wcsstr(s, L"S-1-5-21-")))
    {
        for (q = p + 9, dashes = 0, end = q; *end && (iswdigit(*end) || *end == '-'); end++)
            if (*end == '-') dashes++;
        /* S-1-5-21-a-b-c-rid: keep the rid */
        if (dashes >= 3)
        {
            WCHAR *rid = end;
            while (rid > q && rid[-1] != '-') rid--;
            memmove(p + 9, rid, (wcslen(rid) + 1) * sizeof(WCHAR));
            memcpy(p, L"<machine>", 9 * sizeof(WCHAR));
            memmove(p + 9 + 1, p + 9, (wcslen(p + 9) + 1) * sizeof(WCHAR));
            p[9] = '-';
        }
        else break;
    }
    while ((p = wcsstr(s, L"S-1-5-5-")))
    {
        for (end = p + 8; *end && (iswdigit(*end) || *end == '-'); end++);
        memcpy(p, L"<logon>", 7 * sizeof(WCHAR));
        memmove(p + 7, end, (wcslen(end) + 1) * sizeof(WCHAR));
    }
}

static void variants(void)
{
    HRESULT (WINAPI *pVariantToString)(REFVARIANT, WCHAR *, UINT) = proc(L"propsys.dll", "VariantToString");
    HRESULT (WINAPI *pPropVariantToString)(REFPROPVARIANT, WCHAR *, UINT) = proc(L"propsys.dll", "PropVariantToString");
    SAFEARRAY *array;
    VARIANT v;
    PROPVARIANT pv;
    WCHAR buffer[64];
    HRESULT hr;
    LONG i;

    array = SafeArrayCreateVector(VT_UI1, 0, 3);
    for (i = 0; i < 3; i++)
    {
        BYTE b = 0x41 + i;
        SafeArrayPutElement(array, &i, &b);
    }
    V_VT(&v) = VT_ARRAY | VT_UI1;
    V_ARRAY(&v) = array;
    wcscpy(buffer, L"untouched");
    hr = pVariantToString(&v, buffer, ARRAYSIZE(buffer));
    printf("VariantToString VT_ARRAY|VT_UI1 {41 42 43}: %#lx [%ls]\n", hr, buffer);
    wcscpy(buffer, L"untouched");
    hr = pVariantToString(&v, buffer, 3);
    printf("  into 3: %#lx [%ls]\n", hr, buffer);
    SafeArrayDestroy(array);
    array = SafeArrayCreateVector(VT_I4, 0, 3);
    for (i = 0; i < 3; i++)
    {
        LONG n = i + 1;
        SafeArrayPutElement(array, &i, &n);
    }
    V_VT(&v) = VT_ARRAY | VT_I4;
    V_ARRAY(&v) = array;
    wcscpy(buffer, L"untouched");
    hr = pVariantToString(&v, buffer, ARRAYSIZE(buffer));
    printf("VariantToString VT_ARRAY|VT_I4 {1 2 3}: %#lx [%ls]\n", hr, buffer);
    SafeArrayDestroy(array);
    array = SafeArrayCreateVector(VT_UI1, 0, 0);
    V_VT(&v) = VT_ARRAY | VT_UI1;
    V_ARRAY(&v) = array;
    wcscpy(buffer, L"untouched");
    hr = pVariantToString(&v, buffer, ARRAYSIZE(buffer));
    printf("VariantToString empty VT_ARRAY|VT_UI1: %#lx [%ls]\n", hr, buffer);
    SafeArrayDestroy(array);

    {
        BYTE bytes[3] = { 0x41, 0x42, 0x43 };
        PropVariantInit(&pv);
        pv.vt = VT_VECTOR | VT_UI1;
        pv.caub.cElems = 3;
        pv.caub.pElems = bytes;
        wcscpy(buffer, L"untouched");
        hr = pPropVariantToString(&pv, buffer, ARRAYSIZE(buffer));
        printf("PropVariantToString VT_VECTOR|VT_UI1: %#lx [%ls]\n", hr, buffer);
        pv.vt = VT_BLOB;
        pv.blob.cbSize = 3;
        pv.blob.pBlobData = bytes;
        wcscpy(buffer, L"untouched");
        hr = pPropVariantToString(&pv, buffer, ARRAYSIZE(buffer));
        printf("PropVariantToString VT_BLOB: %#lx [%ls]\n", hr, buffer);
    }
}

static void version_info(void)
{
    static const WCHAR *files[] = { L"kernel32.dll", L"notepad.exe", L"C:\\Windows\\System32\\shell32.dll" };
    static const DWORD flags[] = { 0, 1, 2, 3, 4, 6 };
    unsigned int f, i;

    for (f = 0; f < ARRAYSIZE(files); f++)
        for (i = 0; i < ARRAYSIZE(flags); i++)
        {
            DWORD handle = 0xdeadbeef, size;
            WORD *translation;
            WCHAR *description, path[64];
            UINT len;
            void *data;
            BOOL ret;

            SetLastError(0xdeadbeef);
            size = GetFileVersionInfoSizeExW(flags[i], files[f], &handle);
            printf("%ls flags %#lx: size %lu handle %#lx error %lu", files[f], flags[i], size, handle,
                   size ? 0 : GetLastError());
            if (!size)
            {
                printf("\n");
                continue;
            }
            data = malloc(size);
            ret = GetFileVersionInfoExW(flags[i], files[f], 0, size, data);
            printf(", info %d", ret);
            if (ret && VerQueryValueW(data, L"\\VarFileInfo\\Translation", (void **)&translation, &len) && len >= 4)
            {
                printf(", language %04x codepage %04x (of %u)", translation[0], translation[1], len / 4);
                swprintf(path, ARRAYSIZE(path), L"\\StringFileInfo\\%04x%04x\\FileDescription", translation[0],
                         translation[1]);
                if (VerQueryValueW(data, path, (void **)&description, &len))
                {
                    printf(", description [");
                    print_w(description, -1);
                    printf("]");
                }
            }
            printf("\n");
            free(data);
        }
}

static void interrupt_time(void)
{
    void (WINAPI *pQueryUnbiasedInterruptTimePrecise)(ULONGLONG *) =
            proc(L"kernelbase.dll", "QueryUnbiasedInterruptTimePrecise");
    void (WINAPI *pQueryInterruptTimePrecise)(ULONGLONG *) = proc(L"kernelbase.dll", "QueryInterruptTimePrecise");
    void (WINAPI *pQueryInterruptTime)(ULONGLONG *) = proc(L"kernelbase.dll", "QueryInterruptTime");
    ULONGLONG a, b, c, d, min_delta = ~0ull, prev, now;
    unsigned int i;

    if (!pQueryUnbiasedInterruptTimePrecise)
    {
        printf("no QueryUnbiasedInterruptTimePrecise in kernelbase\n");
        pQueryUnbiasedInterruptTimePrecise = proc(L"kernel32.dll", "QueryUnbiasedInterruptTimePrecise");
    }
    pQueryUnbiasedInterruptTimePrecise(&a);
    QueryUnbiasedInterruptTime(&b);
    pQueryInterruptTimePrecise(&c);
    pQueryInterruptTime(&d);
    printf("QueryUnbiasedInterruptTimePrecise: unbiased %+lld, precise %+lld, plain %+lld (100 ns)\n",
           (long long)(b - a), (long long)(c - a), (long long)(d - a));
    pQueryUnbiasedInterruptTimePrecise(&prev);
    for (i = 0; i < 100000; i++)
    {
        pQueryUnbiasedInterruptTimePrecise(&now);
        if (now > prev && now - prev < min_delta) min_delta = now - prev;
        if (now < prev) printf("  went back %llu\n", prev - now);
        prev = now;
    }
    printf("  smallest step %llu\n", min_delta);
    min_delta = ~0ull;
    QueryUnbiasedInterruptTime(&prev);
    for (i = 0; i < 2000000; i++)
    {
        QueryUnbiasedInterruptTime(&now);
        if (now > prev && now - prev < min_delta) min_delta = now - prev;
        prev = now;
    }
    printf("QueryUnbiasedInterruptTime smallest step %llu\n", min_delta);
    min_delta = ~0ull;
    pQueryInterruptTimePrecise(&prev);
    for (i = 0; i < 100000; i++)
    {
        pQueryInterruptTimePrecise(&now);
        if (now > prev && now - prev < min_delta) min_delta = now - prev;
        prev = now;
    }
    printf("QueryInterruptTimePrecise smallest step %llu\n", min_delta);
}

static void timers(void)
{
    HANDLE timer = CreateWaitableTimerExW(NULL, NULL, 0, TIMER_ALL_ACCESS);
    LARGE_INTEGER due;
    REASON_CONTEXT reason = { POWER_REQUEST_CONTEXT_VERSION, POWER_REQUEST_CONTEXT_SIMPLE_STRING };
    DWORD start, ret;
    BOOL ok;

    due.QuadPart = -1000000; /* 100 ms */
    SetLastError(0xdeadbeef);
    ok = SetWaitableTimerEx(timer, &due, 0, NULL, NULL, NULL, 10000);
    start = GetTickCount();
    ret = WaitForSingleObject(timer, 15000);
    printf("SetWaitableTimerEx 100 ms, delay 10 s: %d error %lu, wait %lu after %lu ms\n", ok, ok ? 0 : GetLastError(),
           ret, GetTickCount() - start);
    reason.Reason.SimpleReasonString = (WCHAR *)L"probe";
    SetLastError(0xdeadbeef);
    ok = SetWaitableTimerEx(timer, &due, 0, NULL, NULL, &reason, 0);
    printf("SetWaitableTimerEx with a wake context: %d error %lu", ok, ok ? 0 : GetLastError());
    if (ok)
    {
        start = GetTickCount();
        ret = WaitForSingleObject(timer, 5000);
        printf(", wait %lu after %lu ms", ret, GetTickCount() - start);
    }
    printf("\n");
    reason.Version = 7;
    SetLastError(0xdeadbeef);
    ok = SetWaitableTimerEx(timer, &due, 0, NULL, NULL, &reason, 0);
    printf("SetWaitableTimerEx with a wake context of version 7: %d error %lu\n", ok, ok ? 0 : GetLastError());
    due.QuadPart = -1000000;
    SetLastError(0xdeadbeef);
    ok = SetWaitableTimerEx(timer, &due, 50, NULL, NULL, NULL, 0xffffffff);
    printf("SetWaitableTimerEx periodic, delay 0xffffffff: %d error %lu\n", ok, ok ? 0 : GetLastError());
    CancelWaitableTimer(timer);
    CloseHandle(timer);
}

static void rpc_and_join(void)
{
    NET_API_STATUS (WINAPI *pNetGetJoinInformation)(const WCHAR *, WCHAR **, NETSETUP_JOIN_STATUS *) =
            proc(L"netapi32.dll", "NetGetJoinInformation");
    HRESULT (WINAPI *pNetGetAadJoinInformation)(const WCHAR *, void **) = proc(L"netapi32.dll", "NetGetAadJoinInformation");
    void (WINAPI *pNetFreeAadJoinInformation)(void *) = proc(L"netapi32.dll", "NetFreeAadJoinInformation");
    NETSETUP_JOIN_STATUS status = 0xdead;
    WCHAR *name = NULL;
    void *info = (void *)0xdeadbeef;
    NET_API_STATUS ret;
    HRESULT hr;

    printf("RpcMgmtEnableIdleCleanup: %ld\n", RpcMgmtEnableIdleCleanup());
    ret = pNetGetJoinInformation(NULL, &name, &status);
    printf("NetGetJoinInformation: %lu status %d name ", ret, status);
    if (!ret && status == NetSetupWorkgroupName) printf("[%ls]\n", name);
    else printf("of %u characters\n", name ? (unsigned int)wcslen(name) : 0);
    if (name) NetApiBufferFree(name);
    ret = pNetGetJoinInformation(L"", &name, &status);
    printf("NetGetJoinInformation empty server: %lu\n", ret);
    if (!ret) NetApiBufferFree(name);
    ret = pNetGetJoinInformation(NULL, NULL, &status);
    printf("NetGetJoinInformation NULL name: %lu\n", ret);
    if (pNetGetAadJoinInformation)
    {
        hr = pNetGetAadJoinInformation(NULL, &info);
        printf("NetGetAadJoinInformation: %#lx info %s", hr, info == (void *)0xdeadbeef ? "untouched" : info ? "set" : "NULL");
        if (hr == S_OK && info && info != (void *)0xdeadbeef)
        {
            printf(", join type %d", *(int *)info);
            pNetFreeAadJoinInformation(info);
        }
        printf("\n");
        hr = pNetGetAadJoinInformation(NULL, NULL);
        printf("NetGetAadJoinInformation NULL: %#lx\n", hr);
    }
    else printf("no NetGetAadJoinInformation\n");
}

static void licenses(void)
{
    NTSTATUS (WINAPI *pNtQueryLicenseValue)(const UNICODE_STRING *, ULONG *, void *, ULONG, ULONG *) =
            proc(L"ntdll.dll", "NtQueryLicenseValue");
    static const WCHAR *names[] =
    {
        L"Microsoft-Windows-Container-License-Mode", L"Kernel-MUI-Language-Allowed", L"Kernel-MUI-Number-Allowed",
        L"Shell-InBoxGames-Solitaire-EnableGame", L"Kernel-ProductInfo", L"Kernel-Edition", L"Kernel-RegisteredProcessors",
        L"Kernel-VMDetection-Private", L"Security-SPP-GenuineLocalStatus", L"WineAltars-No-Such-Value",
    };
    UNICODE_STRING name;
    BYTE data[512];
    ULONG type, size;
    unsigned int i;
    NTSTATUS status;
    HKEY key;

    for (i = 0; i < ARRAYSIZE(names); i++)
    {
        RtlInitUnicodeString(&name, names[i]);
        type = 0xdead;
        size = 0xdead;
        memset(data, 0, sizeof(data));
        status = pNtQueryLicenseValue(&name, &type, data, sizeof(data), &size);
        printf("NtQueryLicenseValue %ls: %#lx type %lu size %lu", names[i], status, type, size);
        if (!status && type == REG_DWORD) printf(" value %lu", *(DWORD *)data);
        if (!status && type == REG_SZ) printf(" value [%ls]", (WCHAR *)data);
        printf("\n");
    }
    RtlInitUnicodeString(&name, names[0]);
    status = pNtQueryLicenseValue(&name, &type, NULL, 0, &size);
    printf("NtQueryLicenseValue no buffer: %#lx size %lu\n", status, size);
    status = pNtQueryLicenseValue(&name, NULL, data, sizeof(data), &size);
    printf("NtQueryLicenseValue NULL type: %#lx\n", status);

    if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\ProductOptions", 0, KEY_READ, &key))
    {
        BYTE *blob;
        DWORD len = 0, offset, count = 0;

        RegQueryValueExW(key, L"ProductPolicy", NULL, NULL, NULL, &len);
        blob = malloc(len);
        if (!RegQueryValueExW(key, L"ProductPolicy", NULL, NULL, blob, &len))
        {
            DWORD data_size = *(DWORD *)(blob + 4);
            printf("ProductPolicy: %lu bytes, data %lu\n== policy\n", len, data_size);
            for (offset = 20; offset + 16 <= 20 + data_size && offset + 16 <= len; )
            {
                USHORT entry = *(USHORT *)(blob + offset), name_len = *(USHORT *)(blob + offset + 2);
                USHORT type = *(USHORT *)(blob + offset + 4), value_len = *(USHORT *)(blob + offset + 6);
                WCHAR *ename = (WCHAR *)(blob + offset + 16);
                BYTE *value = blob + offset + 16 + name_len;

                if (!entry) break;
                printf("%.*ls type %u size %u", name_len / 2, ename, type, value_len);
                /* strings and binary values hold the licence's own state, a part of the product key among it */
                if (type == 4 && value_len == 4) printf(" dword %lu", *(DWORD *)value);
                printf("\n");
                offset += entry;
                count++;
            }
            printf("== end of policy, %lu values\n", count);
        }
        free(blob);
        RegCloseKey(key);
    }
}

static void heap_info(void)
{
    NTSTATUS (WINAPI *pRtlQueryHeapInformation)(HANDLE, HEAP_INFORMATION_CLASS, void *, SIZE_T, SIZE_T *) =
            proc(L"ntdll.dll", "RtlQueryHeapInformation");
    static const ULONG classes[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 0x80000001, 0x80000002 };
    static const SIZE_T sizes[] = { 0, 4, 8, 16, 24, 32, 48, 64, 128, 256, 1024 };
    BYTE buffer[1024];
    unsigned int i, j;

    for (i = 0; i < ARRAYSIZE(classes); i++)
    {
        printf("RtlQueryHeapInformation class %#lx:", classes[i]);
        for (j = 0; j < ARRAYSIZE(sizes); j++)
        {
            SIZE_T ret = 0xdead;
            NTSTATUS status;

            memset(buffer, 0, sizeof(buffer));
            status = pRtlQueryHeapInformation(GetProcessHeap(), classes[i], buffer, sizes[j], &ret);
            printf(" %llu:%#lx/%llu", (ULONG64)sizes[j], status, (ULONG64)ret);
            if (!status && sizes[j] >= 4) printf("=%lx", *(DWORD *)buffer);
        }
        printf("\n");
    }
    {
        SIZE_T ret = 0xdead;
        NTSTATUS status = pRtlQueryHeapInformation(GetProcessHeap(), 2, NULL, 0, &ret);
        printf("RtlQueryHeapInformation class 2 NULL: %#lx ret %llu\n", status, (ULONG64)ret);
        /* HEAP_EXTENDED_INFORMATION: process, heap, level, callback, context */
        memset(buffer, 0, sizeof(buffer));
        *(HANDLE *)buffer = GetCurrentProcess();
        *(ULONG_PTR *)(buffer + 8) = (ULONG_PTR)GetProcessHeap();
        *(ULONG *)(buffer + 16) = 1;
        ret = 0xdead;
        status = pRtlQueryHeapInformation(GetProcessHeap(), 2, buffer, sizeof(buffer), &ret);
        printf("RtlQueryHeapInformation class 2 extended, level 1: %#lx ret %llu\n", status, (ULONG64)ret);
    }
}

static void signed_load(void)
{
    static const struct { const WCHAR *name; DWORD flags; } tries[] =
    {
        { L"kernel32.dll", 0x1f80 },
        { L"imagehlp.dll", 0x1f80 },
        { L"imagehlp.dll", 0x80 },
        { L"unsigned.dll", 0x80 },
        { L"unsigned.dll", 0x1f80 },
        { L"unsigned.dll", 0x80 | LOAD_LIBRARY_AS_DATAFILE },
        { L"unsigned.dll", 0x80 | LOAD_LIBRARY_AS_IMAGE_RESOURCE },
        { L"unsigned.dll", 0x10 },
        { L"unsigned.dll", 0 },
        { L"unsigned.dll", 0x80 },
    };
    WCHAR path[MAX_PATH];
    unsigned int i;

    for (i = 0; i < ARRAYSIZE(tries); i++)
    {
        HMODULE module;
        const WCHAR *name = tries[i].name;

        if (!wcscmp(name, L"unsigned.dll"))
        {
            GetFullPathNameW(name, ARRAYSIZE(path), path, NULL);
            name = path;
        }
        SetLastError(0xdeadbeef);
        module = LoadLibraryExW(name, NULL, tries[i].flags);
        printf("LoadLibraryEx %ls flags %#lx: %s error %lu\n", tries[i].name, tries[i].flags, module ? "loaded" : "failed",
               module ? 0 : GetLastError());
        if (module && (tries[i].flags & (LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE))) FreeLibrary(module);
    }
}

static void print_policies(const char *who, HANDLE process)
{
    BOOL (WINAPI *pGetProcessMitigationPolicy)(HANDLE, PROCESS_MITIGATION_POLICY, void *, SIZE_T) =
            proc(L"kernel32.dll", "GetProcessMitigationPolicy");
    unsigned int policy;

    printf("%s:", who);
    for (policy = 0; policy < 20; policy++)
    {
        DWORD64 flags[2] = { 0xdeadbeef, 0 };
        SIZE_T size = policy == 0 ? sizeof(PROCESS_MITIGATION_DEP_POLICY) : sizeof(DWORD);
        if (pGetProcessMitigationPolicy(process, policy, flags, size))
            printf(" %u=%llx", policy, flags[0] & (policy == 0 ? ~0ull : 0xffffffffull));
        else printf(" %u:err%lu", policy, GetLastError());
    }
    printf("\n");
}

static void mitigation(void)
{
    static const struct { DWORD64 value[2]; SIZE_T size; const char *what; } tries[] =
    {
        { { (1ull << 52) | (1ull << 56) }, 8, "no remote images, no low label images" },
        { { 0x1 | 0x4 | (1ull << 12) | (1ull << 16) | (1ull << 20) | (1ull << 24) | (1ull << 32) | (1ull << 48) |
            (1ull << 60) }, 8, "dep, sehop, heap terminate, aslr, strict handles, extension points, fonts, system32" },
        { { (1ull << 36) | (1ull << 44) }, 8, "dynamic code, non-Microsoft binaries" },
        { { (1ull << 52), (1ull << 16) | (1ull << 24) }, 16, "no remote images; indirect branch prediction, store bypass" },
        { { (2ull << 52) }, 8, "remote images always off" },
        { { (1ull << 52) }, 12, "size 12" },
        { { (1ull << 52) }, 4, "size 4" },
    };
    WCHAR exe[MAX_PATH], cmdline[MAX_PATH + 64];
    unsigned int i;

    GetModuleFileNameW(NULL, exe, ARRAYSIZE(exe));
    print_policies("own policies", GetCurrentProcess());
    for (i = 0; i < ARRAYSIZE(tries); i++)
    {
        STARTUPINFOEXW si = { { sizeof(si) } };
        PROCESS_INFORMATION pi;
        SIZE_T size = 0;
        HANDLE event;
        BOOL ret;

        printf("mitigation %s:", tries[i].what);
        event = CreateEventW(NULL, TRUE, FALSE, L"misc-mitigation-child");
        InitializeProcThreadAttributeList(NULL, 1, 0, &size);
        si.lpAttributeList = malloc(size);
        InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &size);
        SetLastError(0xdeadbeef);
        ret = UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY,
                                        (void *)tries[i].value, tries[i].size, NULL, NULL);
        printf(" update %d error %lu", ret, ret ? 0 : GetLastError());
        if (ret)
        {
            swprintf(cmdline, ARRAYSIZE(cmdline), L"\"%ls\" child misc-mitigation-child", exe);
            SetLastError(0xdeadbeef);
            ret = CreateProcessW(exe, cmdline, NULL, NULL, FALSE, EXTENDED_STARTUPINFO_PRESENT, NULL, NULL,
                                 &si.StartupInfo, &pi);
            printf(", create %d error %lu\n", ret, ret ? 0 : GetLastError());
            if (ret)
            {
                print_policies("  the child's, asked from outside", pi.hProcess);
                SetEvent(event);
                WaitForSingleObject(pi.hProcess, 10000);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }
        }
        else printf("\n");
        DeleteProcThreadAttributeList(si.lpAttributeList);
        free(si.lpAttributeList);
        CloseHandle(event);
    }
}

static int child(const WCHAR *event_name)
{
    HANDLE event = OpenEventW(SYNCHRONIZE, FALSE, event_name);

    setvbuf(stdout, NULL, _IONBF, 0);
    print_policies("  the child's own", GetCurrentProcess());
    WaitForSingleObject(event, 10000);
    return 0;
}

static void packages(void)
{
    LONG (WINAPI *pGetCurrentPackageId)(UINT32 *, BYTE *) = proc(L"kernel32.dll", "GetCurrentPackageId");
    LONG (WINAPI *pGetCurrentPackageFullName)(UINT32 *, WCHAR *) = proc(L"kernel32.dll", "GetCurrentPackageFullName");
    LONG (WINAPI *pGetCurrentPackageFamilyName)(UINT32 *, WCHAR *) = proc(L"kernel32.dll", "GetCurrentPackageFamilyName");
    LONG (WINAPI *pGetCurrentPackagePath)(UINT32 *, WCHAR *) = proc(L"kernel32.dll", "GetCurrentPackagePath");
    LONG (WINAPI *pGetCurrentApplicationUserModelId)(UINT32 *, WCHAR *) =
            proc(L"kernel32.dll", "GetCurrentApplicationUserModelId");
    LONG (WINAPI *pGetCurrentPackageInfo)(UINT32, UINT32 *, BYTE *, UINT32 *) = proc(L"kernel32.dll", "GetCurrentPackageInfo");
    BYTE buffer[512];
    UINT32 len, count;
    LONG ret;

    len = 0;
    ret = pGetCurrentPackageId(&len, NULL);
    printf("GetCurrentPackageId 0 NULL: %ld len %u\n", ret, len);
    len = sizeof(buffer);
    ret = pGetCurrentPackageId(&len, buffer);
    printf("GetCurrentPackageId %u: %ld len %u\n", (unsigned int)sizeof(buffer), ret, len);
    len = 100;
    ret = pGetCurrentPackageId(&len, NULL);
    printf("GetCurrentPackageId 100 NULL: %ld len %u\n", ret, len);
    len = 0;
    ret = pGetCurrentPackageFullName(&len, NULL);
    printf("GetCurrentPackageFullName: %ld len %u\n", ret, len);
    len = 0;
    ret = pGetCurrentPackageFamilyName(&len, NULL);
    printf("GetCurrentPackageFamilyName: %ld len %u\n", ret, len);
    len = 0;
    ret = pGetCurrentPackagePath(&len, NULL);
    printf("GetCurrentPackagePath: %ld len %u\n", ret, len);
    len = 0;
    ret = pGetCurrentApplicationUserModelId(&len, NULL);
    printf("GetCurrentApplicationUserModelId: %ld len %u\n", ret, len);
    if (pGetCurrentPackageInfo)
    {
        len = 0;
        count = 0xdead;
        ret = pGetCurrentPackageInfo(0x10 /* PACKAGE_FILTER_HEAD */, &len, NULL, &count);
        printf("GetCurrentPackageInfo: %ld len %u count %u\n", ret, len, count);
    }
}

static BOOL CALLBACK show_prop(HWND hwnd, WCHAR *name, HANDLE data, ULONG_PTR param)
{
    WCHAR atom[64];

    if (IS_INTRESOURCE(name))
    {
        if (!GlobalGetAtomNameW(LOWORD(name), atom, ARRAYSIZE(atom))) wcscpy(atom, L"?");
        printf(" atom %#x [%ls]=%p", LOWORD(name), atom, data);
    }
    else printf(" [%ls]=%p", name, data);
    return TRUE;
}

static void input_scopes(void)
{
    HRESULT (WINAPI *pSetInputScope)(HWND, int) = proc(L"msctf.dll", "SetInputScope");
    HRESULT (WINAPI *pSetInputScopes)(HWND, const int *, UINT, WCHAR **, UINT, WCHAR *, WCHAR *) =
            proc(L"msctf.dll", "SetInputScopes");
    HRESULT (WINAPI *pSetInputScopeXML)(HWND, WCHAR *) = proc(L"msctf.dll", "SetInputScopeXML");
    HWND hwnd = CreateWindowExW(0, L"static", L"scopes", WS_POPUP, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
    static const int scopes[] = { 1, 5 };
    WCHAR *phrases[] = { (WCHAR *)L"one", (WCHAR *)L"two" };
    HRESULT hr;

    printf("properties before:");
    EnumPropsExW(hwnd, show_prop, 0);
    printf("\n");
    hr = pSetInputScope(hwnd, 1);
    printf("SetInputScope IS_URL: %#lx, properties:", hr);
    EnumPropsExW(hwnd, show_prop, 0);
    printf("\n");
    hr = pSetInputScope(hwnd, 29);
    printf("SetInputScope 29: %#lx, properties:", hr);
    EnumPropsExW(hwnd, show_prop, 0);
    printf("\n");
    hr = pSetInputScope(hwnd, 0);
    printf("SetInputScope IS_DEFAULT: %#lx, properties:", hr);
    EnumPropsExW(hwnd, show_prop, 0);
    printf("\n");
    hr = pSetInputScopes(hwnd, scopes, 2, phrases, 2, NULL, NULL);
    printf("SetInputScopes two, two phrases: %#lx, properties:", hr);
    EnumPropsExW(hwnd, show_prop, 0);
    printf("\n");
    hr = pSetInputScopes(hwnd, NULL, 0, NULL, 0, NULL, NULL);
    printf("SetInputScopes none: %#lx, properties:", hr);
    EnumPropsExW(hwnd, show_prop, 0);
    printf("\n");
    hr = pSetInputScopeXML(hwnd, (WCHAR *)L"<input-scope/>");
    printf("SetInputScopeXML: %#lx\n", hr);
    hr = pSetInputScope(NULL, 1);
    printf("SetInputScope NULL window: %#lx\n", hr);
    hr = pSetInputScope((HWND)0xdead, 1);
    printf("SetInputScope bad window: %#lx\n", hr);
    hr = pSetInputScope(GetDesktopWindow(), 1);
    printf("SetInputScope the desktop: %#lx\n", hr);
    hr = pSetInputScope(hwnd, -5);
    printf("SetInputScope -5: %#lx\n", hr);
    hr = pSetInputScope(hwnd, 1000);
    printf("SetInputScope 1000: %#lx\n", hr);
    DestroyWindow(hwnd);
}

static void winhttp(void)
{
    HINTERNET session, connect, request;
    DWORD value, size;
    BOOL ret;

    session = WinHttpOpen(L"probe", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
    value = 0xdead;
    size = sizeof(value);
    SetLastError(0xdeadbeef);
    ret = WinHttpQueryOption(session, 140, &value, &size);
    printf("WinHttpQueryOption IPV6_FAST_FALLBACK before: %d value %lu error %lu\n", ret, value, ret ? 0 : GetLastError());
    value = 1;
    SetLastError(0xdeadbeef);
    ret = WinHttpSetOption(session, 140, &value, sizeof(value));
    printf("WinHttpSetOption IPV6_FAST_FALLBACK session: %d error %lu\n", ret, ret ? 0 : GetLastError());
    value = 0xdead;
    size = sizeof(value);
    SetLastError(0xdeadbeef);
    ret = WinHttpQueryOption(session, 140, &value, &size);
    printf("  query: %d value %lu error %lu\n", ret, value, ret ? 0 : GetLastError());
    value = 2;
    SetLastError(0xdeadbeef);
    ret = WinHttpSetOption(session, 140, &value, sizeof(value));
    printf("  value 2: %d error %lu\n", ret, ret ? 0 : GetLastError());
    SetLastError(0xdeadbeef);
    ret = WinHttpSetOption(session, 140, &value, 2);
    printf("  size 2: %d error %lu\n", ret, ret ? 0 : GetLastError());
    connect = WinHttpConnect(session, L"localhost", 80, 0);
    value = 1;
    SetLastError(0xdeadbeef);
    ret = WinHttpSetOption(connect, 140, &value, sizeof(value));
    printf("WinHttpSetOption IPV6_FAST_FALLBACK connect: %d error %lu\n", ret, ret ? 0 : GetLastError());
    request = WinHttpOpenRequest(connect, L"GET", L"/", NULL, NULL, NULL, 0);
    SetLastError(0xdeadbeef);
    ret = WinHttpSetOption(request, 140, &value, sizeof(value));
    printf("WinHttpSetOption IPV6_FAST_FALLBACK request: %d error %lu\n", ret, ret ? 0 : GetLastError());
    value = 0xdead;
    size = sizeof(value);
    SetLastError(0xdeadbeef);
    ret = WinHttpQueryOption(request, 140, &value, &size);
    printf("  query request: %d value %lu error %lu\n", ret, value, ret ? 0 : GetLastError());
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
}

static void query(const char *what, IUnknown *unk, const GUID *iid, const char *iid_name)
{
    IUnknown *out;
    HRESULT hr = IUnknown_QueryInterface(unk, iid, (void **)&out);
    printf("  %s QueryInterface %s: %#lx\n", what, iid_name, hr);
    if (SUCCEEDED(hr)) IUnknown_Release(out);
}

static void com(void)
{
    static const struct { const GUID *clsid; const char *name; } objects[] =
    {
        { &CLSID_SAXXMLReader60_, "SAXXMLReader60" }, { &CLSID_SAXXMLReader30_, "SAXXMLReader30" },
        { &CLSID_DOMDocument60_, "DOMDocument60" }, { &CLSID_MXXMLWriter60_, "MXXMLWriter60" },
        { &CLSID_WbemLocator, "WbemLocator" },
    };
    IWbemLocator *locator;
    IWbemServices *services;
    IUnknown *unk;
    unsigned int i;
    HRESULT hr;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
    for (i = 0; i < ARRAYSIZE(objects); i++)
    {
        hr = CoCreateInstance(objects[i].clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IUnknown, (void **)&unk);
        printf("%s: %#lx\n", objects[i].name, hr);
        if (FAILED(hr)) continue;
        query(objects[i].name, unk, &IID_office_private, "{e19c7100}");
        query(objects[i].name, unk, &IID_office_sax, "{c970c32d}");
        query(objects[i].name, unk, &IID_IMarshal, "IMarshal");
        IUnknown_Release(unk);
    }

    hr = CoCreateInstance(&CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER, &IID_IWbemLocator, (void **)&locator);
    if (SUCCEEDED(hr))
    {
        BSTR path = SysAllocString(L"ROOT\\CIMV2");
        hr = IWbemLocator_ConnectServer(locator, path, NULL, NULL, NULL, 0, NULL, NULL, &services);
        printf("ConnectServer: %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            DWORD authn = 0xdead, authz = 0xdead, level = 0xdead, imp = 0xdead, caps = 0xdead;
            WCHAR *principal = NULL;

            query("IWbemServices", (IUnknown *)services, &IID_IClientSecurity_, "IClientSecurity");
            query("IWbemServices", (IUnknown *)services, &IID_office_private, "{e19c7100}");
            hr = CoSetProxyBlanket((IUnknown *)services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
                                   RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
            printf("  CoSetProxyBlanket: %#lx\n", hr);
            hr = CoQueryProxyBlanket((IUnknown *)services, &authn, &authz, &principal, &level, &imp, NULL, &caps);
            printf("  CoQueryProxyBlanket: %#lx authn %lu authz %lu principal %s level %lu imp %lu caps %#lx\n", hr,
                   authn, authz, principal ? "set" : "NULL", level, imp, caps);
            if (principal) CoTaskMemFree(principal);
            IWbemServices_Release(services);
        }
        SysFreeString(path);
        IWbemLocator_Release(locator);
    }

    {
        static const DWORD contexts[] = { CLSCTX_REMOTE_SERVER, CLSCTX_SERVER, CLSCTX_ALL,
                                          CLSCTX_INPROC_SERVER | CLSCTX_REMOTE_SERVER, CLSCTX_LOCAL_SERVER };
        for (i = 0; i < ARRAYSIZE(contexts); i++)
        {
            hr = CoCreateInstance(&CLSID_StdGlobalInterfaceTable_, NULL, contexts[i], &IID_IUnknown, (void **)&unk);
            printf("CoCreateInstance StdGlobalInterfaceTable context %#lx: %#lx\n", contexts[i], hr);
            if (SUCCEEDED(hr)) IUnknown_Release(unk);
            hr = CoCreateInstance(&CLSID_WbemLocator, NULL, contexts[i], &IID_IUnknown, (void **)&unk);
            printf("CoCreateInstance WbemLocator context %#lx: %#lx\n", contexts[i], hr);
            if (SUCCEEDED(hr)) IUnknown_Release(unk);
        }
        {
            MULTI_QI qi = { &IID_IUnknown, NULL, 0 };
            COSERVERINFO info = { 0, NULL, NULL, 0 };
            hr = CoCreateInstanceEx(&CLSID_WbemLocator, NULL, CLSCTX_REMOTE_SERVER, &info, 1, &qi);
            printf("CoCreateInstanceEx WbemLocator remote, no name: %#lx %#lx\n", hr, qi.hr);
            if (SUCCEEDED(hr)) IUnknown_Release(qi.pItf);
        }
    }

    {
        BOOL (WINAPI *pDllDebugObjectRPCHook)(BOOL, void *) = proc(L"ole32.dll", "DllDebugObjectRPCHook");
        ULONG (WINAPI *pOaBuildVersion)(void) = proc(L"oleaut32.dll", "OaBuildVersion");
        if (pDllDebugObjectRPCHook)
        {
            BYTE args[64] = { 0 };
            printf("DllDebugObjectRPCHook FALSE: %d\n", pDllDebugObjectRPCHook(FALSE, args));
        }
        else printf("no DllDebugObjectRPCHook\n");
        printf("OaBuildVersion: %#lx\n", pOaBuildVersion());
    }
    CoUninitialize();
}

static void show_sd(const char *what, BOOL ret, PSECURITY_DESCRIPTOR sd)
{
    WCHAR *sddl = NULL;

    printf("%s: %d error %lu", what, ret, ret ? 0 : GetLastError());
    if (ret && ConvertSecurityDescriptorToStringSecurityDescriptorW(sd, SDDL_REVISION_1,
            OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION |
            SACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION, &sddl, NULL))
    {
        WCHAR copy[2048];
        SECURITY_DESCRIPTOR_CONTROL control;
        DWORD revision;

        wcsncpy(copy, sddl, ARRAYSIZE(copy) - 1);
        copy[ARRAYSIZE(copy) - 1] = 0;
        scrub_sids(copy);
        GetSecurityDescriptorControl(sd, &control, &revision);
        printf(" control %#x length %lu [%ls]", control, GetSecurityDescriptorLength(sd), copy);
        LocalFree(sddl);
    }
    printf("\n");
}

static void private_security(void)
{
    GENERIC_MAPPING mapping = { FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS };
    PSECURITY_DESCRIPTOR sd, parent, creator;
    HANDLE token;
    BOOL ret;

    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token);
    sd = NULL;
    ret = CreatePrivateObjectSecurityEx(NULL, NULL, &sd, NULL, FALSE, 0, token, &mapping);
    show_sd("CreatePrivateObjectSecurityEx nothing", ret, sd);
    if (ret) printf("DestroyPrivateObjectSecurity: %d\n", DestroyPrivateObjectSecurity(&sd));
    sd = NULL;
    ret = CreatePrivateObjectSecurityEx(NULL, NULL, &sd, NULL, FALSE, 0, NULL, &mapping);
    show_sd("CreatePrivateObjectSecurityEx nothing, no token", ret, sd);
    if (ret) DestroyPrivateObjectSecurity(&sd);
    sd = NULL;
    ret = CreatePrivateObjectSecurityEx(NULL, NULL, &sd, NULL, TRUE, SEF_DACL_AUTO_INHERIT | SEF_SACL_AUTO_INHERIT,
                                        token, &mapping);
    show_sd("CreatePrivateObjectSecurityEx nothing, directory, auto inherit", ret, sd);
    if (ret) DestroyPrivateObjectSecurity(&sd);
    ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:SYD:(A;OICI;GA;;;WD)(A;CIIO;GR;;;BU)(A;;GX;;;AU)",
                                                         SDDL_REVISION_1, &parent, NULL);
    ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GR;;;BU)", SDDL_REVISION_1, &creator, NULL);
    sd = NULL;
    ret = CreatePrivateObjectSecurityEx(parent, NULL, &sd, NULL, FALSE, 0, token, &mapping);
    show_sd("CreatePrivateObjectSecurityEx parent, file", ret, sd);
    if (ret) DestroyPrivateObjectSecurity(&sd);
    sd = NULL;
    ret = CreatePrivateObjectSecurityEx(parent, NULL, &sd, NULL, TRUE, 0, token, &mapping);
    show_sd("CreatePrivateObjectSecurityEx parent, directory", ret, sd);
    if (ret) DestroyPrivateObjectSecurity(&sd);
    sd = NULL;
    ret = CreatePrivateObjectSecurityEx(parent, creator, &sd, NULL, FALSE, 0, token, &mapping);
    show_sd("CreatePrivateObjectSecurityEx parent and creator", ret, sd);
    if (ret) DestroyPrivateObjectSecurity(&sd);
    sd = NULL;
    ret = CreatePrivateObjectSecurityEx(parent, creator, &sd, NULL, FALSE, SEF_DACL_AUTO_INHERIT, token, &mapping);
    show_sd("CreatePrivateObjectSecurityEx parent and creator, auto inherit", ret, sd);
    if (ret) DestroyPrivateObjectSecurity(&sd);
    sd = NULL;
    ret = CreatePrivateObjectSecurity(NULL, creator, &sd, FALSE, token, &mapping);
    show_sd("CreatePrivateObjectSecurity creator", ret, sd);
    if (ret) DestroyPrivateObjectSecurity(&sd);
    LocalFree(parent);
    LocalFree(creator);
    CloseHandle(token);
}

static BOOL enable_privilege(const WCHAR *name)
{
    TOKEN_PRIVILEGES privs = { 1 };
    HANDLE token;
    BOOL ret;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &token)) return FALSE;
    LookupPrivilegeValueW(NULL, name, &privs.Privileges[0].Luid);
    privs.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    ret = AdjustTokenPrivileges(token, FALSE, &privs, sizeof(privs), NULL, NULL) && GetLastError() == ERROR_SUCCESS;
    CloseHandle(token);
    return ret;
}

static const WCHAR *last_part(const WCHAR *path)
{
    const WCHAR *p = wcsrchr(path, '\\');
    return p ? p + 1 : path;
}

static void short_names(void)
{
    static const WCHAR name[] = L"sfn-probe-long-file-name.txt";
    WCHAR full[MAX_PATH], shortpath[MAX_PATH];
    HANDLE file;
    BOOL ret;
    int pass;

    GetFullPathNameW(name, ARRAYSIZE(full), full, NULL);
    file = CreateFileW(full, GENERIC_ALL, 0, NULL, CREATE_ALWAYS, 0, NULL);
    shortpath[0] = 0;
    GetShortPathNameW(full, shortpath, ARRAYSIZE(shortpath));
    printf("short name at first [%ls]\n", last_part(shortpath));
    for (pass = 0; pass < 2; pass++)
    {
        if (pass) printf("SeRestorePrivilege enabled: %d\n", enable_privilege(L"SeRestorePrivilege"));
        SetLastError(0xdeadbeef);
        ret = SetFileShortNameW(file, L"SFNPRB~9.TXT");
        shortpath[0] = 0;
        GetShortPathNameW(full, shortpath, ARRAYSIZE(shortpath));
        printf("SetFileShortName SFNPRB~9.TXT: %d error %lu, now [%ls]\n", ret, ret ? 0 : GetLastError(), last_part(shortpath));
        SetLastError(0xdeadbeef);
        ret = SetFileShortNameW(file, L"");
        shortpath[0] = 0;
        GetShortPathNameW(full, shortpath, ARRAYSIZE(shortpath));
        printf("SetFileShortName empty: %d error %lu, now [%ls]\n", ret, ret ? 0 : GetLastError(), last_part(shortpath));
        SetLastError(0xdeadbeef);
        ret = SetFileShortNameW(file, L"a long invalid name.text");
        printf("SetFileShortName invalid: %d error %lu\n", ret, ret ? 0 : GetLastError());
        SetLastError(0xdeadbeef);
        ret = SetFileShortNameW(file, NULL);
        printf("SetFileShortName NULL: %d error %lu\n", ret, ret ? 0 : GetLastError());
    }
    CloseHandle(file);
    file = CreateFileW(full, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    SetLastError(0xdeadbeef);
    ret = SetFileShortNameW(file, L"SFNPRB~8.TXT");
    printf("SetFileShortName read-only handle: %d error %lu\n", ret, ret ? 0 : GetLastError());
    CloseHandle(file);
    DeleteFileW(full);
}

static void performance(void)
{
    BYTE buffer[1024];
    ULONG len = 0xdead, i;
    NTSTATUS status;
    MEMORYSTATUSEX mem = { sizeof(mem) };

    status = NtQuerySystemInformation(SystemPerformanceInformation, buffer, 0, &len);
    printf("SystemPerformanceInformation size 0: %#lx len %lu\n", status, len);
    status = NtQuerySystemInformation(SystemPerformanceInformation, buffer, 0x138, &len);
    printf("SystemPerformanceInformation size 0x138: %#lx len %lu\n", status, len);
    memset(buffer, 0xcc, sizeof(buffer));
    status = NtQuerySystemInformation(SystemPerformanceInformation, buffer, sizeof(buffer), &len);
    printf("SystemPerformanceInformation: %#lx len %lu\n", status, len);
    GlobalMemoryStatusEx(&mem);
    printf("  memory: total %llu MiB, available %llu MiB, commit limit %llu MiB, commit available %llu MiB\n",
           mem.ullTotalPhys >> 20, mem.ullAvailPhys >> 20, mem.ullTotalPageFile >> 20, mem.ullAvailPageFile >> 20);
    if (!status)
    {
        for (i = 0; i < len / 4; i++)
        {
            if (i % 8 == 0) printf("  %03lx:", i * 4);
            printf(" %lu", ((ULONG *)buffer)[i]);
            if (i % 8 == 7) printf("\n");
        }
        printf("\n");
    }
}

static void power(void)
{
    SYSTEM_POWER_CAPABILITIES caps;
    SYSTEM_BATTERY_STATE battery;
    SYSTEM_POWER_STATUS status;
    NTSTATUS ret;
    BYTE *b = (BYTE *)&caps;
    unsigned int i;

    memset(&caps, 0xcc, sizeof(caps));
    ret = CallNtPowerInformation(SystemPowerCapabilities, NULL, 0, &caps, sizeof(caps));
    printf("SystemPowerCapabilities: %#lx size %u\n", ret, (unsigned int)sizeof(caps));
    printf("  power button %d, sleep button %d, lid %d, S1-S5 %d%d%d%d%d, hiber file %d, full wake %d, video dim %d,"
           " apm %d, ups %d, thermal %d, throttle %d (min %d max %d), fast S4 %d, hiberboot %d, wake alarm %d,"
           " aoac %d, disk spin down %d, hiber file type %d, aoac connectivity %d\n",
           caps.PowerButtonPresent, caps.SleepButtonPresent, caps.LidPresent, caps.SystemS1, caps.SystemS2,
           caps.SystemS3, caps.SystemS4, caps.SystemS5, caps.HiberFilePresent, caps.FullWake, caps.VideoDimPresent,
           caps.ApmPresent, caps.UpsPresent, caps.ThermalControl, caps.ProcessorThrottle, caps.ProcessorMinThrottle,
           caps.ProcessorMaxThrottle, caps.FastSystemS4, caps.spare2[0], caps.spare2[1], caps.spare2[2],
           caps.DiskSpinDown, caps.spare3[0], caps.spare3[1]);
    printf("  batteries %d short term %d, battery scale %lu/%lu %lu/%lu %lu/%lu, ac wake %d, soft lid wake %d,"
           " rtc wake %d, min device wake %d, default low latency wake %d\n",
           caps.SystemBatteriesPresent, caps.BatteriesAreShortTerm, caps.BatteryScale[0].Granularity,
           caps.BatteryScale[0].Capacity, caps.BatteryScale[1].Granularity, caps.BatteryScale[1].Capacity,
           caps.BatteryScale[2].Granularity, caps.BatteryScale[2].Capacity, caps.AcOnLineWake, caps.SoftLidWake,
           caps.RtcWake, caps.MinDeviceWakeState, caps.DefaultLowLatencyWake);
    printf("  raw:");
    for (i = 0; i < sizeof(caps); i++) printf(" %02x", b[i]);
    printf("\n");
    ret = CallNtPowerInformation(SystemBatteryState, NULL, 0, &battery, sizeof(battery));
    printf("SystemBatteryState: %#lx ac %d present %d charging %d discharging %d, max %lu remaining %lu rate %ld,"
           " estimated %lu, alerts %lu %lu\n", ret, battery.AcOnLine, battery.BatteryPresent, battery.Charging,
           battery.Discharging, battery.MaxCapacity, battery.RemainingCapacity, (LONG)battery.Rate,
           battery.EstimatedTime, battery.DefaultAlert1, battery.DefaultAlert2);
    GetSystemPowerStatus(&status);
    printf("GetSystemPowerStatus: ac %u flag %#x percent %u saver %u life %ld full %ld\n", status.ACLineStatus,
           status.BatteryFlag, status.BatteryLifePercent, status.SystemStatusFlag, (LONG)status.BatteryLifeTime,
           (LONG)status.BatteryFullLifeTime);
    ret = CallNtPowerInformation(SystemPowerCapabilities, NULL, 0, &caps, sizeof(caps) - 1);
    printf("SystemPowerCapabilities a byte short: %#lx\n", ret);
}

static void xml_properties(void)
{
    IXmlReader *reader;
    IXmlWriter *writer;
    LONG_PTR value;
    unsigned int i;
    HRESULT hr;

    if (SUCCEEDED(CreateXmlReader(&IID_IXmlReader, (void **)&reader, NULL)))
    {
        printf("reader properties:");
        for (i = 0; i < 32; i++)
        {
            value = 0xdead;
            hr = IXmlReader_GetProperty(reader, i, &value);
            if (hr == E_INVALIDARG) printf(" %u:inv", i);
            else printf(" %u:%#lx=%llx", i, hr, (ULONG64)value);
        }
        printf("\n");
        value = 0xdead;
        IXmlReader_GetProperty(reader, 18, &value);
        hr = IXmlReader_SetProperty(reader, 18, value);
        printf("reader SetProperty 18 to itself: %#lx\n", hr);
        hr = IXmlReader_SetProperty(reader, 18, 1);
        IXmlReader_GetProperty(reader, 18, &value);
        printf("reader SetProperty 18 to 1: %#lx, now %llx\n", hr, (ULONG64)value);
        hr = IXmlReader_SetProperty(reader, 18, 0);
        IXmlReader_GetProperty(reader, 18, &value);
        printf("reader SetProperty 18 to 0: %#lx, now %llx\n", hr, (ULONG64)value);
        IXmlReader_Release(reader);
    }
    if (SUCCEEDED(CreateXmlWriter(&IID_IXmlWriter, (void **)&writer, NULL)))
    {
        printf("writer properties:");
        for (i = 0; i < 32; i++)
        {
            value = 0xdead;
            hr = IXmlWriter_GetProperty(writer, i, &value);
            if (hr == E_INVALIDARG) printf(" %u:inv", i);
            else printf(" %u:%#lx=%llx", i, hr, (ULONG64)value);
        }
        printf("\n");
        IXmlWriter_Release(writer);
    }
}

static DWORD persistent_ids[4], regular_id;
static volatile LONG apc_ran;

static DWORD WINAPI persistent_item(void *arg)
{
    persistent_ids[(ULONG_PTR)arg] = GetCurrentThreadId();
    return 0;
}

static DWORD WINAPI regular_item(void *arg)
{
    regular_id = GetCurrentThreadId();
    return 0;
}

static void CALLBACK apc(ULONG_PTR arg)
{
    apc_ran = GetCurrentThreadId();
}

static void CALLBACK simple_callback(TP_CALLBACK_INSTANCE *instance, void *arg)
{
    persistent_ids[(ULONG_PTR)arg] = GetCurrentThreadId();
}

static void persistent_threads(void)
{
    TP_CALLBACK_ENVIRON env;
    HANDLE thread;
    BOOL ret;

    ret = QueueUserWorkItem(persistent_item, (void *)0, WT_EXECUTEINPERSISTENTTHREAD);
    Sleep(300);
    QueueUserWorkItem(persistent_item, (void *)1, WT_EXECUTEINPERSISTENTTHREAD);
    QueueUserWorkItem(regular_item, NULL, WT_EXECUTEDEFAULT);
    Sleep(300);
    printf("QueueUserWorkItem persistent: %d, both on one thread %d, that thread is %s, main thread %d\n", ret,
           persistent_ids[0] && persistent_ids[0] == persistent_ids[1],
           persistent_ids[0] == regular_id ? "the regular one" : "another", persistent_ids[0] == GetCurrentThreadId());
    thread = OpenThread(THREAD_SET_CONTEXT, FALSE, persistent_ids[0]);
    if (thread)
    {
        QueueUserAPC(apc, thread, 0);
        Sleep(500);
        printf("  an APC queued to it %s\n", apc_ran == (LONG)persistent_ids[0] ? "ran there" : "did not run");
        CloseHandle(thread);
    }
    TpInitializeCallbackEnviron(&env);
    SetThreadpoolCallbackPersistent(&env);
    ret = TrySubmitThreadpoolCallback(simple_callback, (void *)2, &env);
    Sleep(300);
    ret = ret && TrySubmitThreadpoolCallback(simple_callback, (void *)3, &env);
    Sleep(300);
    printf("TrySubmitThreadpoolCallback persistent: %d, both on one thread %d, the thread of the work items %d\n", ret,
           persistent_ids[2] && persistent_ids[2] == persistent_ids[3], persistent_ids[2] == persistent_ids[0]);
    DestroyThreadpoolEnvironment(&env);
}

static const struct { const char *name; void (*run)(void); } sections[] =
{
    { "variants", variants }, { "version", version_info }, { "time", interrupt_time }, { "timers", timers },
    { "join", rpc_and_join }, { "heap", heap_info }, { "signed", signed_load }, { "mitigation", mitigation },
    { "packages", packages }, { "scopes", input_scopes }, { "winhttp", winhttp }, { "com", com },
    { "security", private_security }, { "shortnames", short_names }, { "performance", performance },
    { "power", power }, { "xml", xml_properties }, { "threads", persistent_threads }, { "licenses", licenses },
};

int wmain(int argc, WCHAR **argv)
{
    WCHAR exe[MAX_PATH], cmdline[MAX_PATH + 64];
    unsigned int i;

    if (argc >= 3 && !wcscmp(argv[1], L"child")) return child(argv[2]);
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc >= 3 && !wcscmp(argv[1], L"section"))
    {
        for (i = 0; i < ARRAYSIZE(sections); i++)
        {
            WCHAR name[32];
            swprintf(name, ARRAYSIZE(name), L"%hs", sections[i].name);
            if (!wcscmp(argv[2], name)) sections[i].run();
        }
        return 0;
    }
    GetModuleFileNameW(NULL, exe, ARRAYSIZE(exe));
    for (i = 0; i < ARRAYSIZE(sections); i++)
    {
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        DWORD code = 0xdead;

        if (argc >= 2 && wcsstr(argv[1], L"=")) continue;
        swprintf(cmdline, ARRAYSIZE(cmdline), L"\"%ls\" section %hs", exe, sections[i].name);
        if (!CreateProcessW(exe, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
        {
            printf("section %s: CreateProcess failed %lu\n", sections[i].name, GetLastError());
            continue;
        }
        if (WaitForSingleObject(pi.hProcess, 120000)) TerminateProcess(pi.hProcess, 0xdead);
        GetExitCodeProcess(pi.hProcess, &code);
        if (code) printf("section %s ended with %#lx\n", sections[i].name, code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    return 0;
}
