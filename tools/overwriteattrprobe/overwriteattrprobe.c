/* What writing over an existing hidden, system or read-only file does with its attributes.
 *
 * The CreateFile documentation says CREATE_ALWAYS fails with ERROR_ACCESS_DENIED on an existing file
 * that is hidden or system unless the same attributes are given; Wine's tests only cover read-only.
 * For each kind of existing file and each set of attributes asked for, this prints what CreateFile
 * (CREATE_ALWAYS, TRUNCATE_EXISTING) and NtCreateFile (FILE_OVERWRITE, FILE_OVERWRITE_IF,
 * FILE_SUPERSEDE) answer and the attributes the file has afterwards.  Files go in a new directory under
 * %TEMP%, removed at the end.  It also asks what the profile functions, CopyFile and a replacing move do
 * with a hidden, system or read-only file they write over.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror overwriteattrprobe.c -o overwriteattrprobe.exe -lntdll
 */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

#ifndef FILE_SUPERSEDE
#define FILE_SUPERSEDE 0
#define FILE_OPEN_IF 3
#define FILE_OVERWRITE 4
#define FILE_OVERWRITE_IF 5
#endif

static WCHAR dir[MAX_PATH], path[MAX_PATH];

static void make(DWORD attr)
{
    HANDLE file;

    SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
    DeleteFileW(path);
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    WriteFile(file, "data", 4, NULL, NULL);
    CloseHandle(file);
    if (attr) SetFileAttributesW(path, attr);
}

static void report(const char *how, DWORD existing, DWORD asked, BOOL ok, DWORD error)
{
    DWORD attr = GetFileAttributesW(path);
    WIN32_FILE_ATTRIBUTE_DATA data;

    GetFileAttributesExW(path, GetFileExInfoStandard, &data);
    printf("%-22s existing %#06lx asked %#06lx: %s %lu, now %#06lx size %lu\n", how, existing, asked,
           ok ? "ok" : "failed", ok ? 0 : error, attr & ~FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
           attr == INVALID_FILE_ATTRIBUTES ? 0 : data.nFileSizeLow);
}

int main(void)
{
    static const DWORD existing[] = {0, FILE_ATTRIBUTE_HIDDEN, FILE_ATTRIBUTE_SYSTEM, FILE_ATTRIBUTE_READONLY,
                                     FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_READONLY};
    static const DWORD asked[] = {0, FILE_ATTRIBUTE_NORMAL, FILE_ATTRIBUTE_HIDDEN, FILE_ATTRIBUTE_SYSTEM,
                                  FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM, FILE_ATTRIBUTE_READONLY,
                                  FILE_ATTRIBUTE_TEMPORARY, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_TEMPORARY};
    static const struct { DWORD disposition; const char *name; } nt[] =
        {{FILE_OVERWRITE, "FILE_OVERWRITE"}, {FILE_OVERWRITE_IF, "FILE_OVERWRITE_IF"}, {FILE_SUPERSEDE, "FILE_SUPERSEDE"}};
    WCHAR temp[MAX_PATH];
    unsigned int e, a, i;

    GetTempPathW(MAX_PATH, temp);
    swprintf(dir, MAX_PATH, L"%lsoverwriteattr-%lu", temp, GetCurrentProcessId());
    CreateDirectoryW(dir, NULL);
    swprintf(path, MAX_PATH, L"%ls\\file.txt", dir);

    for (e = 0; e < ARRAYSIZE(existing); e++)
        for (a = 0; a < ARRAYSIZE(asked); a++)
        {
            HANDLE file;
            DWORD error;

            make(existing[e]);
            SetLastError(0xdeadbeef);
            file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, asked[a], NULL);
            error = GetLastError();
            report("CREATE_ALWAYS", existing[e], asked[a], file != INVALID_HANDLE_VALUE, error);
            if (file != INVALID_HANDLE_VALUE) CloseHandle(file);

            make(existing[e]);
            SetLastError(0xdeadbeef);
            file = CreateFileW(path, GENERIC_WRITE, 0, NULL, TRUNCATE_EXISTING, asked[a], NULL);
            error = GetLastError();
            report("TRUNCATE_EXISTING", existing[e], asked[a], file != INVALID_HANDLE_VALUE, error);
            if (file != INVALID_HANDLE_VALUE) CloseHandle(file);

            for (i = 0; i < ARRAYSIZE(nt); i++)
            {
                UNICODE_STRING name;
                OBJECT_ATTRIBUTES attr;
                IO_STATUS_BLOCK io;
                NTSTATUS status;
                char how[40];

                make(existing[e]);
                RtlDosPathNameToNtPathName_U(path, &name, NULL, NULL);
                InitializeObjectAttributes(&attr, &name, OBJ_CASE_INSENSITIVE, NULL, NULL);
                status = NtCreateFile(&file, GENERIC_WRITE | SYNCHRONIZE, &attr, &io, NULL, asked[a], 0,
                                      nt[i].disposition, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0);
                RtlFreeUnicodeString(&name);
                sprintf(how, "%s (%#lx)", nt[i].name, status ? (unsigned long)status : (unsigned long)io.Information);
                report(how, existing[e], asked[a], !status, status);
                if (!status) CloseHandle(file);
            }
        }

    /* what writes over files for the caller does with a hidden one: the profile functions (desktop.ini
     * is hidden and system), CopyFile, and a replacing move */
    {
        static const DWORD dest[] = {FILE_ATTRIBUTE_HIDDEN, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM,
                                     FILE_ATTRIBUTE_READONLY};
        WCHAR ini[MAX_PATH], src[MAX_PATH], value[16];
        DWORD attr, error, size;
        HANDLE file;
        BOOL ret;

        swprintf(ini, MAX_PATH, L"%ls\\profile.ini", dir);
        swprintf(src, MAX_PATH, L"%ls\\source.txt", dir);
        for (e = 0; e < ARRAYSIZE(dest); e++)
        {
            SetFileAttributesW(ini, FILE_ATTRIBUTE_NORMAL);
            DeleteFileW(ini);
            WritePrivateProfileStringW(L"s", L"k", L"1", ini);
            WritePrivateProfileStringW(NULL, NULL, NULL, ini);
            SetFileAttributesW(ini, dest[e]);
            SetLastError(0xdeadbeef);
            ret = WritePrivateProfileStringW(L"s", L"k", L"2", ini);
            error = GetLastError();
            WritePrivateProfileStringW(NULL, NULL, NULL, ini);
            attr = GetFileAttributesW(ini);
            GetPrivateProfileStringW(L"s", L"k", L"", value, ARRAYSIZE(value), ini);
            printf("WritePrivateProfileString existing %#06lx: %s %lu, now %#06lx, value %ls\n", dest[e],
                   ret ? "ok" : "failed", ret ? 0 : error, attr & ~FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, value);

            file = CreateFileW(src, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
            WriteFile(file, "source", 6, &size, NULL);
            CloseHandle(file);
            make(dest[e]);
            SetLastError(0xdeadbeef);
            ret = CopyFileW(src, path, FALSE);
            report("CopyFile", dest[e], 0, ret, GetLastError());
            make(dest[e]);
            SetLastError(0xdeadbeef);
            ret = MoveFileExW(src, path, MOVEFILE_REPLACE_EXISTING);
            report("MoveFileEx replace", dest[e], 0, ret, GetLastError());
            DeleteFileW(src);
        }
        SetFileAttributesW(ini, FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(ini);
    }

    SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
    DeleteFileW(path);
    RemoveDirectoryW(dir);
    return 0;
}
