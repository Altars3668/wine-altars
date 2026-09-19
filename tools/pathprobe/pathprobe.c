/*
 * pathprobe -- which path API refuses a path that is not there yet.
 *
 * Click-to-Run stores its download directory as a literal
 *
 *     HKLM\...\ClickToRun\...  PipelineDownloadPath (REG_SZ)
 *         %ProgramFiles%\Microsoft Office\Updates\Download\PackageFiles\{GUID}
 *
 * and expands it itself.  When the install rolls back it reports
 *
 *     Failed to get path root %ProgramFiles%\Microsoft Office\Updates\...
 *
 * so something between expanding that string and naming its volume said no.
 * Every candidate is called here on the same path, before the directory
 * exists and again after, because that is the difference between a machine
 * that has had Office and one that has not.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -municode \
 *       -o pathprobe.exe pathprobe.c -lshlwapi
 *
 *   pathprobe [path]   default: the C2R download path above
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <windows.h>
#include <shlwapi.h>
#include <userenv.h>
#include <stdio.h>
#include <stdarg.h>

/* wprintf writes UTF-16 into a pipe, which the shell then reads as NUL-riddled
 * bytes.  Everything goes out as UTF-8 through one helper instead. */
static void out(const WCHAR *s)
{
    char u[4096];
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, u, sizeof(u), NULL, NULL);
    if (n > 0) fwrite(u, 1, n - 1, stdout);
}

static void outf(const WCHAR *fmt, ...)
{
    WCHAR b[4096];
    va_list ap;
    va_start(ap, fmt);
    vswprintf(b, ARRAYSIZE(b), fmt, ap);
    va_end(ap);
    out(b);
}

static void err(const WCHAR *api, BOOL ok, const WCHAR *got)
{
    if (ok) outf(L"  %-34ls OK    %ls\n", api, got ? got : L"");
    else    outf(L"  %-34ls FAIL  err=%lu\n", api, GetLastError());
}

static void probe(const WCHAR *path)
{
    WCHAR buf[MAX_PATH * 2];
    ULARGE_INTEGER a, b, c;
    DWORD sec, byt, fre, tot;

    outf(L"路径: %ls\n", path);

    SetLastError(0);
    buf[0] = 0;
    err(L"GetVolumePathNameW", GetVolumePathNameW(path, buf, ARRAYSIZE(buf)), buf);

    SetLastError(0);
    err(L"GetDiskFreeSpaceExW", GetDiskFreeSpaceExW(path, &a, &b, &c), NULL);

    SetLastError(0);
    lstrcpynW(buf, path, ARRAYSIZE(buf));
    err(L"PathStripToRootW", PathStripToRootW(buf), buf);

    SetLastError(0);
    buf[0] = 0;
    err(L"GetFullPathNameW", GetFullPathNameW(path, ARRAYSIZE(buf), buf, NULL) != 0, buf);

    SetLastError(0);
    err(L"GetDriveTypeW(root)", GetDriveTypeW(buf) != DRIVE_UNKNOWN && GetDriveTypeW(buf) != DRIVE_NO_ROOT_DIR, NULL);

    /* GetVolumeInformationW and GetDiskFreeSpaceW want a root, so give them
     * the one GetVolumePathNameW just produced -- or C:\ if it produced none. */
    buf[0] = 0;
    if (!GetVolumePathNameW(path, buf, ARRAYSIZE(buf))) lstrcpyW(buf, L"C:\\");
    SetLastError(0);
    err(L"GetVolumeInformationW", GetVolumeInformationW(buf, NULL, 0, NULL, NULL, NULL, NULL, 0), buf);
    SetLastError(0);
    err(L"GetDiskFreeSpaceW", GetDiskFreeSpaceW(buf, &sec, &byt, &fre, &tot), buf);

    SetLastError(0);
    buf[0] = 0;
    err(L"GetVolumeNameForVolumeMountPointW",
        GetVolumePathNameW(path, buf, ARRAYSIZE(buf)) &&
        GetVolumeNameForVolumeMountPointW(buf, buf, ARRAYSIZE(buf)), buf);
    outf(L"\n");
}

/* services.exe builds a service's environment with CreateEnvironmentBlock and
 * bInherit FALSE, so calling it the same way here shows exactly what a service
 * is handed -- without having to be one. */
static void dump_service_env(void)
{
    WCHAR *env = NULL, *p;
    HANDLE token;

    outf(L"== 服务进程拿到的环境（CreateEnvironmentBlock, bInherit=FALSE）==\n");
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &token))
    {
        outf(L"  OpenProcessToken 失败 err=%lu\n\n", GetLastError());
        return;
    }
    if (!CreateEnvironmentBlock((void **)&env, token, FALSE))
    {
        outf(L"  CreateEnvironmentBlock 失败 err=%lu\n\n", GetLastError());
        CloseHandle(token);
        return;
    }
    for (p = env; *p; p += lstrlenW(p) + 1) outf(L"  %ls\n", p);
    DestroyEnvironmentBlock(env);
    CloseHandle(token);
    outf(L"\n");
}

int wmain(int argc, WCHAR **argv)
{
    static const WCHAR def[] =
        L"%ProgramFiles%\\Microsoft Office\\Updates\\Download\\PackageFiles"
        L"\\116F7616-A5BE-41BF-8ABE-493019AA6D38";
    WCHAR raw[MAX_PATH * 2], expanded[MAX_PATH * 2];

    lstrcpynW(raw, argc > 1 ? argv[1] : def, ARRAYSIZE(raw));

    outf(L"== 未展开的字面量（C2R 存在注册表里的形式）==\n");
    probe(raw);

    expanded[0] = 0;
    SetLastError(0);
    if (!ExpandEnvironmentStringsW(raw, expanded, ARRAYSIZE(expanded)))
        outf(L"ExpandEnvironmentStringsW 失败 err=%lu\n\n", GetLastError());
    else
    {
        outf(L"== 展开后 ==\n");
        probe(expanded);
    }

    dump_service_env();

    outf(L"本进程环境里的 ProgramFiles: %ls\n",
            GetEnvironmentVariableW(L"ProgramFiles", raw, ARRAYSIZE(raw)) ? raw : L"<没有>");
    return 0;
}
