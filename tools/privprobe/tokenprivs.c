/* tokenprivs: the LUID LookupPrivilegeValue gives each privilege name, and the privileges this process's token has,
 * with their attributes, its elevation type, and what AdjustTokenPrivileges says to enabling the symbolic link one.
 * Prints results only. */
#include <windows.h>
#include <stdio.h>

int main(void)
{
    static const WCHAR *names[] =
    {
        L"SeCreateTokenPrivilege", L"SeAssignPrimaryTokenPrivilege", L"SeLockMemoryPrivilege",
        L"SeIncreaseQuotaPrivilege", L"SeMachineAccountPrivilege", L"SeTcbPrivilege", L"SeSecurityPrivilege",
        L"SeTakeOwnershipPrivilege", L"SeLoadDriverPrivilege", L"SeSystemProfilePrivilege", L"SeSystemtimePrivilege",
        L"SeProfileSingleProcessPrivilege", L"SeIncreaseBasePriorityPrivilege", L"SeCreatePagefilePrivilege",
        L"SeCreatePermanentPrivilege", L"SeBackupPrivilege", L"SeRestorePrivilege", L"SeShutdownPrivilege",
        L"SeDebugPrivilege", L"SeAuditPrivilege", L"SeSystemEnvironmentPrivilege", L"SeChangeNotifyPrivilege",
        L"SeRemoteShutdownPrivilege", L"SeUndockPrivilege", L"SeSyncAgentPrivilege", L"SeEnableDelegationPrivilege",
        L"SeManageVolumePrivilege", L"SeImpersonatePrivilege", L"SeCreateGlobalPrivilege",
        L"SeTrustedCredManAccessPrivilege", L"SeRelabelPrivilege", L"SeIncreaseWorkingSetPrivilege",
        L"SeTimeZonePrivilege", L"SeCreateSymbolicLinkPrivilege", L"SeDelegateSessionUserImpersonatePrivilege",
        L"SeUnsolicitedInputPrivilege",
    };
    TOKEN_PRIVILEGES *privs, adjust;
    TOKEN_ELEVATION_TYPE type;
    WCHAR name[64];
    HANDLE token;
    DWORD i, len;
    LUID luid;
    BOOL ret;

    for (i = 0; i < ARRAYSIZE(names); i++)
    {
        SetLastError(0xdeadbeef);
        ret = LookupPrivilegeValueW(NULL, names[i], &luid);
        printf("%-44ls %d, error %lu, luid %ld:%lu\n", names[i], ret, ret ? 0 : GetLastError(), luid.HighPart,
               luid.LowPart);
    }
    for (i = 0; i <= 40; i++)
    {
        luid.HighPart = 0;
        luid.LowPart = i;
        len = ARRAYSIZE(name);
        if (LookupPrivilegeNameW(NULL, &luid, name, &len)) printf("luid %lu: %ls\n", i, name);
    }

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &token)) return 1;
    if (GetTokenInformation(token, TokenElevationType, &type, sizeof(type), &len))
        printf("elevation type %d\n", type);
    GetTokenInformation(token, TokenPrivileges, NULL, 0, &len);
    privs = malloc(len);
    if (GetTokenInformation(token, TokenPrivileges, privs, len, &len))
    {
        printf("%lu privileges:\n", privs->PrivilegeCount);
        for (i = 0; i < privs->PrivilegeCount; i++)
        {
            len = ARRAYSIZE(name);
            if (!LookupPrivilegeNameW(NULL, &privs->Privileges[i].Luid, name, &len)) wcscpy(name, L"?");
            printf("  %2lu %-44ls %#lx\n", privs->Privileges[i].Luid.LowPart, name, privs->Privileges[i].Attributes);
        }
    }
    adjust.PrivilegeCount = 1;
    adjust.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    LookupPrivilegeValueW(NULL, L"SeCreateSymbolicLinkPrivilege", &adjust.Privileges[0].Luid);
    SetLastError(0xdeadbeef);
    ret = AdjustTokenPrivileges(token, FALSE, &adjust, 0, NULL, NULL);
    printf("enabling SeCreateSymbolicLinkPrivilege: %d, error %lu\n", ret, GetLastError());
    CloseHandle(token);
    return 0;
}
