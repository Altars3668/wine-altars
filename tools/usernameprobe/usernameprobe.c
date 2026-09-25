/*
 * usernameprobe - what GetUserNameExW answers for each name format, for
 * the user of a machine in no domain.
 *
 * Word asks for NameDisplay, NameUserPrincipal and NameDnsDomain, which
 * Wine answered with FIXMEs.  The names themselves are the user's own, so
 * this prints only their shape: whether the call succeeds, the error, the
 * length it reports, and whether the name has a backslash, an at sign or a
 * dot in it, and whether it equals GetUserNameW's answer.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define SECURITY_WIN32
#include <windows.h>
#include <security.h>
#include <stdio.h>

int main(void)
{
    WCHAR name[512], user[256];
    DWORD size, error, usersize = ARRAYSIZE(user);
    BOOLEAN ret;
    int format;

    setvbuf(stdout, NULL, _IONBF, 0);
    GetUserNameW(user, &usersize);
    for (format = 0; format <= 20; format++)
    {
        size = ARRAYSIZE(name);
        memset(name, 0, sizeof(name));
        SetLastError(0xdeadbeef);
        ret = GetUserNameExW(format, name, &size);
        error = GetLastError();
        printf("format %2d: %d error %lu size %lu", format, ret, ret ? 0 : error, size);
        if (ret)
            printf(", length %u%s%s%s%s", (unsigned)wcslen(name), wcschr(name, '\\') ? ", a backslash" : "",
                   wcschr(name, '@') ? ", an at sign" : "", wcschr(name, '.') ? ", a dot" : "",
                   !wcsicmp(name, user) ? ", the user name" : "");
        printf("\n");

        size = 0;
        SetLastError(0xdeadbeef);
        ret = GetUserNameExW(format, NULL, &size);
        error = GetLastError();
        printf("           no buffer: %d error %lu size %lu\n", ret, error, size);
    }
    printf("done\n");
    return 0;
}
