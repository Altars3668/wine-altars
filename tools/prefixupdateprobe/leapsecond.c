#include "probe.h"
#include <stddef.h>

typedef LONG (WINAPI *query_system_fn)(ULONG, void *, ULONG, ULONG *);
typedef BOOL (WINAPI *get_leap_fn)(BOOL *, DWORD *);

/* 信息类 206：BOOLEAN 后有三字节对齐填充。 */
typedef struct
{
    BYTE Enabled;
    ULONG Flags;
} leap_information;

int main(void)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    static const ULONG sizes[] = {0, 1, 3, 4, 7, 8, 9, 15, 16, 32, 64};
    union { ULONGLONG align; BYTE bytes[64]; } buffer;
    query_system_fn query;
    get_leap_fn get_leap;
    HMODULE module;
    ULONG returned, i, j;
    LONG status;
    BOOL enabled, ok;
    DWORD flags, error;
    leap_information info;

    if (!probe_start()) return 1;
    module = GetModuleHandleW(L"ntdll.dll");
    query = module ? (query_system_fn)(void *)GetProcAddress(module, "NtQuerySystemInformation") : NULL;
    printf("NtQuerySystemInformation exported=%u struct_size=%llu flags_offset=%llu\n",
           !!query, (unsigned long long)sizeof(info),
           (unsigned long long)offsetof(leap_information, Flags));
    if (query)
        for (i = 0; i < ARRAYSIZE(sizes); ++i)
        {
            memset(&buffer, 0xa5, sizeof(buffer));
            returned = PROBE_SENTINEL;
            SetLastError(PROBE_SENTINEL);
            status = query(206, buffer.bytes, sizes[i], &returned);
            error = GetLastError();
            printf("class=206 len=%lu ntstatus=%#lx gle=%lu returned=%lu bytes=",
                   sizes[i], (ULONG)status, error, returned);
            for (j = 0; j < sizeof(buffer.bytes); ++j) printf("%02x", buffer.bytes[j]);
            if (status >= 0 && sizes[i] >= sizeof(info))
            {
                memcpy(&info, buffer.bytes, sizeof(info));
                printf(" Enabled=%u Flags=%#lx", info.Enabled, info.Flags);
            }
            puts("");
        }

    module = GetModuleHandleW(L"kernel32.dll");
    get_leap = module ? (get_leap_fn)(void *)GetProcAddress(module, "GetSystemLeapSecondInformation") : NULL;
    if (!get_leap)
    {
        module = GetModuleHandleW(L"kernelbase.dll");
        if (module) get_leap = (get_leap_fn)(void *)GetProcAddress(module, "GetSystemLeapSecondInformation");
    }
    printf("GetSystemLeapSecondInformation exported=%u\n", !!get_leap);
    if (get_leap)
    {
        enabled = (BOOL)0xa5a5a5a5U;
        flags = PROBE_SENTINEL;
        SetLastError(PROBE_SENTINEL);
        ok = get_leap(&enabled, &flags);
        error = GetLastError();
        printf("GetSystemLeapSecondInformation ok=%u gle=%lu Enabled=%#lx Flags=%#lx\n",
               ok, error, (ULONG)enabled, flags);
    }
    return probe_done(0);
}
