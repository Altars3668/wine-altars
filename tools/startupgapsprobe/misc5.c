/* misc5: what Word's last startup fixmes ask -- the extended information of a heap at the levels other than 1 (Word
 * asks for 0x80000000), and how WinINet takes the Secure, HttpOnly and SameSite attributes of a cookie and gives
 * such cookies back for http and https.  The cookies are session cookies of a domain that cannot exist
 * (wine-altars-probe.invalid), expired again at the end.  Prints results only. */
#include <windows.h>
#include <winternl.h>
#include <wininet.h>
#include <stdio.h>

static void *proc(const WCHAR *dll, const char *name)
{
    HMODULE module = LoadLibraryW(dll);
    return module ? (void *)GetProcAddress(module, name) : NULL;
}

static void heap_levels(void)
{
    NTSTATUS (WINAPI *pRtlQueryHeapInformation)(HANDLE, HEAP_INFORMATION_CLASS, void *, SIZE_T, SIZE_T *) =
            proc(L"ntdll.dll", "RtlQueryHeapInformation");
    static const ULONG levels[] = { 0, 1, 2, 3, 4, 5, 6, 0x80000000, 0x80000001, 0x40000000 };
    static const SIZE_T sizes[] = { 40, 88, 128, 256, 1024 };
    BYTE buffer[1024];
    unsigned int i, j, k;

    for (i = 0; i < ARRAYSIZE(levels); i++)
    {
        for (j = 0; j < ARRAYSIZE(sizes); j++)
        {
            SIZE_T ret = 0xdead;
            NTSTATUS status;

            memset(buffer, 0xcc, sizeof(buffer));
            /* HEAP_EXTENDED_INFORMATION: process, heap, level, callback, context, then what is asked */
            *(HANDLE *)buffer = GetCurrentProcess();
            *(ULONG_PTR *)(buffer + 8) = (ULONG_PTR)GetProcessHeap();
            *(ULONG *)(buffer + 16) = levels[i];
            *(ULONG *)(buffer + 20) = 0;
            *(void **)(buffer + 24) = NULL;
            *(void **)(buffer + 32) = NULL;
            status = pRtlQueryHeapInformation(GetProcessHeap(), 2, buffer, sizes[j], &ret);
            printf("level %#lx size %llu: %#lx ret %llu", levels[i], (ULONG64)sizes[j], status, (ULONG64)ret);
            if (!status)
            {
                /* the words written after the request, as they come: pointers and sizes vary, so only their
                 * shape is shown -- 0, small numbers as they are, others as "big" */
                printf(" [");
                for (k = 40; k + 8 <= ret && k + 8 <= sizes[j] && k < 40 + 160; k += 8)
                {
                    ULONG64 v = *(ULONG64 *)(buffer + k);
                    if (v == 0xcccccccccccccccc) printf(" cc");
                    else if (v < 0x10000) printf(" %llu", v);
                    else printf(" big");
                }
                printf(" ]");
            }
            printf("\n");
        }
    }
    {
        SIZE_T ret = 0xdead;
        NTSTATUS status;

        memset(buffer, 0, sizeof(buffer));
        *(HANDLE *)buffer = GetCurrentProcess();
        *(ULONG *)(buffer + 16) = 0x80000000;
        status = pRtlQueryHeapInformation(NULL, 2, buffer, sizeof(buffer), &ret);
        printf("level 0x80000000, no heap: %#lx ret %llu\n", status, (ULONG64)ret);
        memset(buffer, 0, sizeof(buffer));
        *(HANDLE *)buffer = GetCurrentProcess();
        *(ULONG *)(buffer + 16) = 1;
        status = pRtlQueryHeapInformation(NULL, 2, buffer, sizeof(buffer), &ret);
        printf("level 1, no heap: %#lx ret %llu\n", status, (ULONG64)ret);
    }
}

/* the raw answer of a heap query after its 40-byte request, in 8-byte words */
static void dump_heap(const char *what, HANDLE heap, ULONG level, SIZE_T size)
{
    NTSTATUS (WINAPI *pRtlQueryHeapInformation)(HANDLE, HEAP_INFORMATION_CLASS, void *, SIZE_T, SIZE_T *) =
            proc(L"ntdll.dll", "RtlQueryHeapInformation");
    static BYTE buffer[16384];
    SIZE_T ret = 0xdead, k;
    NTSTATUS status;

    memset(buffer, 0, sizeof(buffer));
    *(HANDLE *)buffer = GetCurrentProcess();
    *(ULONG_PTR *)(buffer + 8) = (ULONG_PTR)heap;
    *(ULONG *)(buffer + 16) = level;
    status = pRtlQueryHeapInformation(heap, 2, buffer, size, &ret);
    printf("%s (heap %s, level %#lx): %#lx ret %llu\n", what, heap ? (heap == GetProcessHeap() ? "process" : "own")
           : "none", level, status, (ULONG64)ret);
    if (status) return;
    for (k = 40; k + 8 <= ret; k += 8)
    {
        if ((k - 40) % 64 == 0) printf("  %04llx:", (ULONG64)(k - 40));
        printf(" %llx", *(ULONG64 *)(buffer + k));
        if ((k - 40) % 64 == 56) printf("\n");
    }
    printf("\n");
}

static void heap_raw(void)
{
    HANDLE heap = HeapCreate(0, 0, 0);
    void *blocks[10], *big;
    unsigned int i;

    printf("own heap at %p, process heap at %p\n", heap, GetProcessHeap());
    dump_heap("process heap", GetProcessHeap(), 0x80000000, 256);
    dump_heap("own heap, new", heap, 0x80000000, 256);
    for (i = 0; i < ARRAYSIZE(blocks); i++) blocks[i] = HeapAlloc(heap, 0, 100);
    dump_heap("own heap, 10 blocks of 100", heap, 0x80000000, 256);
    big = HeapAlloc(heap, 0, 1 << 20);
    dump_heap("own heap, and 1 MiB", heap, 0x80000000, 256);
    for (i = 0; i < 5; i++) HeapFree(heap, 0, blocks[i]);
    HeapFree(heap, 0, big);
    dump_heap("own heap, 5 freed and the big one", heap, 0x80000000, 256);
    dump_heap("own heap, level 1", heap, 1, 256);
    dump_heap("all heaps, level 1", NULL, 1, 256);
    dump_heap("all heaps, level 0x80000000", NULL, 0x80000000, 4096);
    dump_heap("own heap, level 0x80000001", heap, 0x80000001, 16384);
    printf("heaps: %lu\n", GetProcessHeaps(0, NULL));
    for (i = 5; i < ARRAYSIZE(blocks); i++) HeapFree(heap, 0, blocks[i]);
    HeapDestroy(heap);
}

static void show_cookies(const char *what, const WCHAR *url, DWORD flags)
{
    WCHAR buffer[1024];
    DWORD size = ARRAYSIZE(buffer);
    BOOL ret;

    buffer[0] = 0;
    SetLastError(0xdeadbeef);
    ret = InternetGetCookieExW(url, NULL, buffer, &size, flags, NULL);
    printf("  %s: %d error %lu [%ls]\n", what, ret, ret ? 0 : GetLastError(), ret ? buffer : L"");
}

static void set(const WCHAR *url, const WCHAR *data, DWORD flags)
{
    DWORD ret;

    SetLastError(0xdeadbeef);
    ret = InternetSetCookieExW(url, NULL, data, flags, 0);
    printf("set %ls on %ls flags %#lx: %lu error %lu\n", data, url, flags, ret, ret ? 0 : GetLastError());
}

static void cookies(void)
{
    static const WCHAR https[] = L"https://wine-altars-probe.invalid/", http[] = L"http://wine-altars-probe.invalid/";
    static const WCHAR *names[] = { L"a", L"b", L"c", L"d", L"e", L"f", L"g", L"h" };
    unsigned int i;

    set(https, L"a=1; secure", 0);
    set(https, L"b=2; HttpOnly", 0);
    set(https, L"b=2; HttpOnly", INTERNET_COOKIE_HTTPONLY);
    set(https, L"c=3; SameSite=None", 0);
    set(https, L"d=4; samesite=none; httponly", INTERNET_COOKIE_HTTPONLY);
    set(http, L"e=5; Secure; SameSite=Strict", 0);
    set(https, L"f=6; SameSite=Lax", 0);
    set(https, L"g=7; samesite=bogus", 0);
    set(http, L"h=8", 0);
    show_cookies("https", https, 0);
    show_cookies("https, http only ones too", https, INTERNET_COOKIE_HTTPONLY);
    show_cookies("http", http, 0);
    show_cookies("http, http only ones too", http, INTERNET_COOKIE_HTTPONLY);
    set(https, L"a=1b", 0);
    show_cookies("https after setting a without secure", https, 0);
    show_cookies("http after setting a without secure", http, 0);

    /* gone again */
    for (i = 0; i < ARRAYSIZE(names); i++)
    {
        WCHAR data[64];
        swprintf(data, ARRAYSIZE(data), L"%ls=; expires=Thu, 01 Jan 1970 00:00:00 GMT", names[i]);
        InternetSetCookieExW(https, NULL, data, INTERNET_COOKIE_HTTPONLY, 0);
        InternetSetCookieExW(http, NULL, data, INTERNET_COOKIE_HTTPONLY, 0);
    }
    show_cookies("https at the end", https, INTERNET_COOKIE_HTTPONLY);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("== heap\n");
    heap_levels();
    printf("== heap raw\n");
    heap_raw();
    printf("== cookies\n");
    cookies();
    return 0;
}
