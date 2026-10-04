#define UNICODE
#include <windows.h>
#include <sddl.h>
#include <stdio.h>

int main(void)
{
    static const WCHAR *descriptors[] =
    {
        L"O:BAG:BAD:",
        L"O:BAG:BAD:(D;;GA;;;WD)(A;;GA;;;WD)",
        L"O:BAG:BAD:(A;;RC;;;OW)",
        L"O:BAG:BAD:(D;;GA;;;OW)(A;;GA;;;WD)",
        L"O:SYG:SYD:",
    };
    static const DWORD desired[] = {MAXIMUM_ALLOWED, READ_CONTROL, WRITE_DAC, DELETE, SYNCHRONIZE};
    GENERIC_MAPPING mapping = {READ_CONTROL | 1, READ_CONTROL | 1, READ_CONTROL | 1, STANDARD_RIGHTS_ALL | 1};
    union { TOKEN_OWNER owner; BYTE bytes[sizeof(TOKEN_OWNER) + SECURITY_MAX_SID_SIZE]; } owner;
    BYTE admin_sid[SECURITY_MAX_SID_SIZE];
    DWORD sid_size = sizeof(admin_sid), size, i, j;
    HANDLE primary, token;
    BOOL result;

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &primary) ||
        !DuplicateToken(primary, SecurityImpersonation, &token)) return 1;
    result = GetTokenInformation(primary, TokenOwner, &owner, sizeof(owner), &size);
    if (result && CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, admin_sid, &sid_size))
        printf("default_owner_is_administrators=%u\n", EqualSid(owner.owner.Owner, admin_sid));
    {
        BYTE dacl_buffer[65536];
        TOKEN_DEFAULT_DACL *default_dacl = (TOKEN_DEFAULT_DACL *)dacl_buffer;
        BYTE system_sid[SECURITY_MAX_SID_SIZE];
        DWORD system_size = sizeof(system_sid);
        SID_AND_ATTRIBUTES disabled = {admin_sid, 0};
        HANDLE restricted;
        union { TOKEN_USER user; BYTE bytes[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE]; } user;
        if (CreateWellKnownSid(WinLocalSystemSid, NULL, system_sid, &system_size) &&
            GetTokenInformation(primary, TokenDefaultDacl, dacl_buffer, sizeof(dacl_buffer), &size))
        {
            for (i = 0; default_dacl->DefaultDacl && i < default_dacl->DefaultDacl->AceCount; ++i)
            {
                ACCESS_ALLOWED_ACE *ace;
                if (GetAce(default_dacl->DefaultDacl, i, (void **)&ace))
                    printf("default_dacl admin=%u system=%u logon=%u restricted_code=%u mask=%#lx\n",
                           EqualSid(&ace->SidStart, admin_sid), EqualSid(&ace->SidStart, system_sid),
                           IsWellKnownSid(&ace->SidStart, WinLogonIdsSid),
                           IsWellKnownSid(&ace->SidStart, WinRestrictedCodeSid), ace->Mask);
            }
        }
        if (CreateRestrictedToken(primary, 0, 1, &disabled, 0, NULL, 0, NULL, &restricted))
        {
            if (GetTokenInformation(restricted, TokenOwner, &owner, sizeof(owner), &size) &&
                GetTokenInformation(restricted, TokenUser, &user, sizeof(user), &size))
                printf("restricted_owner_admin=%u restricted_owner_user=%u\n", EqualSid(owner.owner.Owner, admin_sid),
                       EqualSid(owner.owner.Owner, user.user.User.Sid));
            CloseHandle(restricted);
        }
    }
    CloseHandle(primary);
    for (i = 0; i < ARRAYSIZE(descriptors); ++i)
    {
        PSECURITY_DESCRIPTOR sd;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(descriptors[i], SDDL_REVISION_1, &sd, NULL))
        {
            printf("descriptor=%lu parse_error=%lu\n", i, GetLastError());
            continue;
        }
        for (j = 0; j < ARRAYSIZE(desired); ++j)
        {
            PRIVILEGE_SET privileges;
            DWORD privilege_size = sizeof(privileges), granted = 0xdeadbeef;
            BOOL access = FALSE;
            SetLastError(0xdeadbeef);
            result = AccessCheck(sd, token, desired[j], &mapping, &privileges, &privilege_size, &granted, &access);
            printf("descriptor=%lu desired=%#lx result=%u access=%u granted=%#lx error=%lu\n",
                   i, desired[j], result, access, granted, GetLastError());
        }
        LocalFree(sd);
    }
    CloseHandle(token);
    puts("done");
    return 0;
}
