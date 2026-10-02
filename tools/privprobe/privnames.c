/* privnames: what LookupPrivilegeDisplayName says of every privilege -- its text, language and the lengths it asks
 * for -- and what it does with a short buffer, an unknown name, no name and the ANSI call.  Non-ASCII text is printed
 * as \uXXXX (the ANSI call's as bytes).  Prints results only. */
#include <windows.h>
#include <stdio.h>

static void print_w(const WCHAR *str)
{
    for (; *str; str++)
    {
        if (*str >= 0x20 && *str < 0x7f) printf("%c", (char)*str);
        else printf("\\u%04x", *str);
    }
}

int main(void)
{
    static const WCHAR *names[] =
    {
        L"SeCreateTokenPrivilege", L"SeAssignPrimaryTokenPrivilege", L"SeLockMemoryPrivilege",
        L"SeIncreaseQuotaPrivilege", L"SeUnsolicitedInputPrivilege", L"SeMachineAccountPrivilege", L"SeTcbPrivilege",
        L"SeSecurityPrivilege", L"SeTakeOwnershipPrivilege", L"SeLoadDriverPrivilege", L"SeSystemProfilePrivilege",
        L"SeSystemtimePrivilege", L"SeProfileSingleProcessPrivilege", L"SeIncreaseBasePriorityPrivilege",
        L"SeCreatePagefilePrivilege", L"SeCreatePermanentPrivilege", L"SeBackupPrivilege", L"SeRestorePrivilege",
        L"SeShutdownPrivilege", L"SeDebugPrivilege", L"SeAuditPrivilege", L"SeSystemEnvironmentPrivilege",
        L"SeChangeNotifyPrivilege", L"SeRemoteShutdownPrivilege", L"SeUndockPrivilege", L"SeSyncAgentPrivilege",
        L"SeEnableDelegationPrivilege", L"SeManageVolumePrivilege", L"SeImpersonatePrivilege",
        L"SeCreateGlobalPrivilege", L"SeTrustedCredManAccessPrivilege", L"SeRelabelPrivilege",
        L"SeIncreaseWorkingSetPrivilege", L"SeTimeZonePrivilege", L"SeCreateSymbolicLinkPrivilege",
        L"SeDelegateSessionUserImpersonatePrivilege", L"sedebugprivilege", L"SeBogusPrivilege", L"",
    };
    WCHAR buf[256];
    char bufA[256];
    DWORD len, lang, i;
    BOOL ret;

    printf("user language %#x, system language %#x\n", GetUserDefaultUILanguage(), GetSystemDefaultUILanguage());
    for (i = 0; i < ARRAYSIZE(names); i++)
    {
        len = 0;
        lang = 0xdead;
        SetLastError(0xdeadbeef);
        ret = LookupPrivilegeDisplayNameW(NULL, names[i], NULL, &len, &lang);
        printf("[%ls] size: %d, error %lu, len %lu, lang %#lx;", names[i], ret, GetLastError(), len, lang);
        len = ARRAYSIZE(buf);
        lang = 0xdead;
        buf[0] = 0;
        SetLastError(0xdeadbeef);
        ret = LookupPrivilegeDisplayNameW(NULL, names[i], buf, &len, &lang);
        printf(" %d, error %lu, len %lu, lang %#lx [", ret, ret ? 0 : GetLastError(), len, lang);
        print_w(buf);
        printf("]\n");
    }

    len = 3;
    SetLastError(0xdeadbeef);
    ret = LookupPrivilegeDisplayNameW(NULL, L"SeDebugPrivilege", buf, &len, &lang);
    printf("short buffer: %d, error %lu, len %lu\n", ret, GetLastError(), len);
    len = ARRAYSIZE(buf);
    SetLastError(0xdeadbeef);
    ret = LookupPrivilegeDisplayNameW(NULL, NULL, buf, &len, &lang);
    printf("no name: %d, error %lu, len %lu\n", ret, GetLastError(), len);
    len = ARRAYSIZE(buf);
    SetLastError(0xdeadbeef);
    ret = LookupPrivilegeDisplayNameW(L"", L"SeDebugPrivilege", buf, &len, &lang);
    printf("system \"\": %d, error %lu, len %lu\n", ret, ret ? 0 : GetLastError(), len);
    len = ARRAYSIZE(bufA);
    SetLastError(0xdeadbeef);
    ret = LookupPrivilegeDisplayNameA(NULL, "SeDebugPrivilege", bufA, &len, &lang);
    printf("ANSI: %d, error %lu, len %lu, lang %#lx [", ret, ret ? 0 : GetLastError(), len, lang);
    for (i = 0; ret && bufA[i]; i++) printf((unsigned char)bufA[i] < 0x80 ? "%c" : "\\x%02x", (unsigned char)bufA[i]);
    printf("]\n");
    len = 0;
    SetLastError(0xdeadbeef);
    ret = LookupPrivilegeDisplayNameA(NULL, "SeDebugPrivilege", NULL, &len, &lang);
    printf("ANSI size: %d, error %lu, len %lu\n", ret, GetLastError(), len);
    return 0;
}
