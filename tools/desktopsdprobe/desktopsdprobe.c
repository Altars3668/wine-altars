/*
 * desktopsdprobe - the security descriptors of window stations and desktops.
 *
 * Office's sandbox (a Chromium-style broker in mso.dll) reads the DACL of the
 * thread's desktop with GetSecurityInfo() to add the sandboxed token's SID to
 * it, and uses the DACL without checking for NULL.  This prints, as SDDL, the
 * owner, group and DACL of the process's window station and the thread's
 * desktop, read both as SE_WINDOW_OBJECT and SE_KERNEL_OBJECT, of a desktop
 * and a window station this probe creates with no security attributes, and
 * of the token's default DACL, logon SID and user, whose SIDs are printed by
 * their shape only.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <stdio.h>

/* SIDs of this machine and logon are shown by their shape: S-1-5-21-<domain> as D, S-1-5-5-x-y as the logon SID */
static void print_sddl(const WCHAR *what, PSECURITY_DESCRIPTOR sd, SECURITY_INFORMATION info)
{
    WCHAR *str = NULL, *p;
    char out[4096];
    int i = 0;

    if (!ConvertSecurityDescriptorToStringSecurityDescriptorW(sd, SDDL_REVISION_1, info, &str, NULL))
    {
        printf("  %ls: convert failed %lu\n", what, GetLastError());
        return;
    }
    for (p = str; *p && i < sizeof(out) - 16; p++)
    {
        if (!wcsncmp(p, L"S-1-5-21-", 9))
        {
            i += sprintf(out + i, "S-1-5-21-<machine>");
            p += 9;
            while (p[1] && (iswdigit(p[1]) || p[1] == '-')) p++;
            /* keep the RID */
            continue;
        }
        if (!wcsncmp(p, L"S-1-5-5-", 8))
        {
            i += sprintf(out + i, "S-1-5-5-<logon>");
            p += 8;
            while (p[1] && (iswdigit(p[1]) || p[1] == '-')) p++;
            continue;
        }
        out[i++] = *p < 128 ? *p : '?';
    }
    out[i] = 0;
    printf("  %ls: %s\n", what, out);
    LocalFree(str);
}

static void print_object(const WCHAR *what, HANDLE handle)
{
    SECURITY_INFORMATION info = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION;
    PSECURITY_DESCRIPTOR sd;
    PACL dacl;
    WCHAR name[256], buf[300];
    DWORD len, ret;

    name[0] = 0;
    GetUserObjectInformationW(handle, UOI_NAME, name, sizeof(name), &len);
    printf("%ls %ls:\n", what, !wcsncmp(name, L"Service-0x", 10) ? L"Service-0x<session>$" : name);
    ret = GetSecurityInfo(handle, SE_WINDOW_OBJECT, info, NULL, NULL, &dacl, NULL, &sd);
    if (ret) printf("  SE_WINDOW_OBJECT: error %lu\n", ret);
    else
    {
        printf("  SE_WINDOW_OBJECT: dacl %s\n", dacl ? "present" : "NULL");
        print_sddl(L"SE_WINDOW_OBJECT", sd, info);
        LocalFree(sd);
    }
    ret = GetSecurityInfo(handle, SE_KERNEL_OBJECT, info, NULL, NULL, &dacl, NULL, &sd);
    if (ret) printf("  SE_KERNEL_OBJECT: error %lu\n", ret);
    else
    {
        swprintf(buf, ARRAY_SIZE(buf), L"SE_KERNEL_OBJECT");
        printf("  SE_KERNEL_OBJECT: dacl %s\n", dacl ? "present" : "NULL");
        print_sddl(buf, sd, info);
        LocalFree(sd);
    }
}

static void print_token(void)
{
    char buf[1024];
    TOKEN_DEFAULT_DACL *dacl = (void *)buf;
    SECURITY_DESCRIPTOR sd;
    HANDLE token;
    DWORD len;

    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token);
    if (GetTokenInformation(token, TokenDefaultDacl, buf, sizeof(buf), &len))
    {
        InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
        SetSecurityDescriptorDacl(&sd, TRUE, dacl->DefaultDacl, FALSE);
        print_sddl(L"token default DACL", &sd, DACL_SECURITY_INFORMATION);
    }
    else printf("  token default DACL: error %lu\n", GetLastError());
    CloseHandle(token);
}

int main(void)
{
    HWINSTA winsta = GetProcessWindowStation(), new_winsta;
    HDESK desktop = GetThreadDesktop(GetCurrentThreadId()), new_desktop;

    printf("token:\n");
    print_token();
    print_object(L"window station", winsta);
    print_object(L"desktop", desktop);

    new_desktop = CreateDesktopW(L"desktopsdprobe", NULL, NULL, 0, GENERIC_ALL, NULL);
    if (new_desktop)
    {
        print_object(L"created desktop", new_desktop);
        CloseDesktop(new_desktop);
    }
    else printf("CreateDesktopW failed %lu\n", GetLastError());

    new_winsta = CreateWindowStationW(NULL, 0, GENERIC_ALL, NULL);
    if (new_winsta)
    {
        print_object(L"created window station", new_winsta);
        CloseWindowStation(new_winsta);
    }
    else printf("CreateWindowStationW failed %lu\n", GetLastError());
    printf("done\n");
    return 0;
}
