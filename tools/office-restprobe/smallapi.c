#define WIN32_LEAN_AND_MEAN
#include "probe.h"
#include <winsock2.h>
#include <ws2tcpip.h>

#ifndef AI_FQDN
#define AI_FQDN 0x00020000
#endif

static void test_impersonation(void)
{
    HANDLE primary = NULL, token = NULL, attached = NULL;
    DWORD size, level, error;
    SECURITY_IMPERSONATION_LEVEL effective;
    BOOL ret;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &primary)) return;
    for (level = SecurityAnonymous; level <= SecurityDelegation; ++level)
    {
        if (!DuplicateTokenEx(primary, TOKEN_QUERY | TOKEN_IMPERSONATE, NULL, level, TokenImpersonation, &token))
        {
            printf("impersonate level=%lu duplicate_error=%lu\n", level, GetLastError());
            continue;
        }
        SetLastError(PROBE_SENTINEL);
        ret = ImpersonateLoggedOnUser(token);
        error = GetLastError();
        printf("impersonate level=%lu ok=%u error=%lu", level, ret, error);
        if (ret)
        {
            if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &attached))
            {
                if (GetTokenInformation(attached, TokenImpersonationLevel, &effective, sizeof(effective), &size))
                    printf(" effective=%u", effective);
                CloseHandle(attached);
            }
            RevertToSelf();
        }
        putchar('\n');
        CloseHandle(token);
    }
    SetLastError(PROBE_SENTINEL);
    ret = ImpersonateLoggedOnUser(primary);
    error = GetLastError();
    printf("impersonate primary ok=%u error=%lu\n", ret, error);
    if (ret) RevertToSelf();
    CloseHandle(primary);
}

static void test_fips(void)
{
    HKEY key;
    DWORD value = PROBE_SENTINEL, size = sizeof(value), type = PROBE_SENTINEL;
    BOOLEAN enabled = 0xcc;
    NTSTATUS status;
    LONG ret = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\Lsa\\FipsAlgorithmPolicy", 0, KEY_QUERY_VALUE, &key);
    if (!ret)
    {
        ret = RegQueryValueExW(key, L"Enabled", NULL, &type, (BYTE *)&value, &size);
        RegCloseKey(key);
    }
    status = BCryptGetFipsAlgorithmMode(&enabled);
    printf("fips registry_status=%ld type=%lu value=%lu size=%lu api_status=%#lx enabled=%u\n",
           ret, type, value, size, (ULONG)status, enabled);
}

static void test_addrinfo(void)
{
    static const char *names[] = {"localhost", "127.0.0.1", "::1"};
    static const unsigned int flags[] = {0, AI_CANONNAME, AI_FQDN, AI_FQDN | AI_CANONNAME,
                                        AI_FQDN | AI_NUMERICHOST};
    ADDRINFOA hints, *result, *p;
    WSADATA data;
    unsigned int i, j, count;
    int status;
    if (WSAStartup(MAKEWORD(2,2), &data)) return;
    for (i = 0; i < ARRAYSIZE(names); ++i)
    for (j = 0; j < ARRAYSIZE(flags); ++j)
    {
        memset(&hints, 0, sizeof(hints));
        hints.ai_flags = flags[j];
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        result = NULL;
        status = getaddrinfo(names[i], "80", &hints, &result);
        count = 0;
        for (p = result; p; p = p->ai_next) count++;
        /* 不输出系统派生的主机名；只判断受控输入的 canonical 形状。 */
        printf("addr name=%u flags=%#x status=%d count=%u canon_present=%u canon_input=%u result_flags=%#x\n",
               i, flags[j], status, count, result && result->ai_canonname,
               result && result->ai_canonname && !strcmp(result->ai_canonname, names[i]), result ? result->ai_flags : 0);
        if (result) freeaddrinfo(result);
    }
    WSACleanup();
}

int main(void)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (!probe_start()) return 1;
    test_impersonation();
    test_fips();
    test_addrinfo();
    return probe_done(0);
}
