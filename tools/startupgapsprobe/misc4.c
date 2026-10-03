/* misc4: short names as SetFileShortName sets them -- which names it takes (case, the file's own long name, another
 * file's long or short name, a directory), what handle it needs, whether the privilege must be enabled, what
 * FileAlternateNameInformation and FindFirstFile say before and after, and whether the file opens by its new short
 * name.  Works on files of its own in the current directory, deleted at the end.  Prints results only. */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

static const WCHAR long_name[] = L"sfn4-probe-long-file-name.txt";
static const WCHAR other_name[] = L"sfn4-probe-other-long-name.txt";
static const WCHAR short8_name[] = L"SFN4.TXT";
static const WCHAR dir_name[] = L"sfn4-probe-directory";

static BOOL set_privilege(const WCHAR *name, BOOL enable)
{
    TOKEN_PRIVILEGES privs = { 1 };
    HANDLE token;
    BOOL ret;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &token)) return FALSE;
    LookupPrivilegeValueW(NULL, name, &privs.Privileges[0].Luid);
    privs.Privileges[0].Attributes = enable ? SE_PRIVILEGE_ENABLED : 0;
    ret = AdjustTokenPrivileges(token, FALSE, &privs, sizeof(privs), NULL, NULL) && GetLastError() == ERROR_SUCCESS;
    CloseHandle(token);
    return ret;
}

static void show_names(const char *what, const WCHAR *name)
{
    NTSTATUS (WINAPI *pNtQueryInformationFile)(HANDLE, IO_STATUS_BLOCK *, void *, ULONG, FILE_INFORMATION_CLASS) =
            (void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationFile");
    WIN32_FIND_DATAW data;
    WCHAR shortpath[MAX_PATH];
    BYTE buffer[512];
    FILE_NAME_INFORMATION *info = (FILE_NAME_INFORMATION *)buffer;
    IO_STATUS_BLOCK io;
    NTSTATUS status;
    HANDLE find, file;
    DWORD len;

    printf("%s:", what);
    find = FindFirstFileW(name, &data);
    if (find != INVALID_HANDLE_VALUE)
    {
        printf(" find [%ls] alt [%ls]", data.cFileName, data.cAlternateFileName);
        FindClose(find);
    }
    else printf(" find error %lu", GetLastError());
    shortpath[0] = 0;
    len = GetShortPathNameW(name, shortpath, MAX_PATH);
    printf(", short path %lu [%ls]", len, shortpath);
    file = CreateFileW(name, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                       OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (file != INVALID_HANDLE_VALUE)
    {
        memset(buffer, 0, sizeof(buffer));
        status = pNtQueryInformationFile(file, &io, buffer, sizeof(buffer), 21 /* FileAlternateNameInformation */);
        printf(", alternate name %#lx", status);
        if (!status) printf(" [%.*ls] info %llu", (int)(info->FileNameLength / sizeof(WCHAR)), info->FileName,
                            (ULONGLONG)io.Information);
        status = pNtQueryInformationFile(file, &io, buffer, 6, 21);
        printf(", in 6 bytes %#lx info %llu", status, (ULONGLONG)io.Information);
        CloseHandle(file);
    }
    printf("\n");
}

static void try_set(const char *what, const WCHAR *name, DWORD access, const WCHAR *short_name)
{
    HANDLE file = CreateFileW(name, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                              OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    BOOL ret;

    if (file == INVALID_HANDLE_VALUE)
    {
        printf("%s: open error %lu\n", what, GetLastError());
        return;
    }
    SetLastError(0xdeadbeef);
    ret = SetFileShortNameW(file, short_name);
    printf("%s: %d error %lu\n", what, ret, ret ? 0 : GetLastError());
    CloseHandle(file);
}

static void create(const WCHAR *name)
{
    HANDLE file = CreateFileW(name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    CloseHandle(file);
}

int main(void)
{
    WCHAR shortpath[MAX_PATH];
    HANDLE file;

    setvbuf(stdout, NULL, _IONBF, 0);
    create(long_name);
    create(other_name);
    create(short8_name);
    CreateDirectoryW(dir_name, NULL);

    printf("SeRestorePrivilege disabled: %d\n", set_privilege(L"SeRestorePrivilege", FALSE));
    try_set("not enabled", long_name, GENERIC_ALL, L"SFNA~1.TXT");
    printf("SeRestorePrivilege enabled: %d\n", set_privilege(L"SeRestorePrivilege", TRUE));

    show_names("at first", long_name);
    show_names("an 8.3 name", short8_name);
    show_names("a directory", dir_name);

    try_set("DELETE only", long_name, DELETE, L"SFNB~1.TXT");
    show_names("  then", long_name);
    try_set("GENERIC_WRITE", long_name, GENERIC_WRITE, L"SFNC~1.TXT");
    try_set("FILE_WRITE_ATTRIBUTES", long_name, FILE_WRITE_ATTRIBUTES, L"SFNC~1.TXT");
    try_set("lower case", long_name, DELETE, L"sfnd~1.txt");
    show_names("  then", long_name);
    try_set("another file's long name", long_name, DELETE, short8_name);
    try_set("another file's short name", other_name, DELETE, L"SFND~1.TXT");
    try_set("no extension", long_name, DELETE, L"SFNE~1");
    try_set("nine characters", long_name, DELETE, L"SFNEXTRA9.TXT");
    try_set("a space", long_name, DELETE, L"SFN E.TXT");
    try_set("a plus", long_name, DELETE, L"SFN+E.TXT");
    try_set("two dots", long_name, DELETE, L"SF.N.TXT");
    try_set("its own long name", short8_name, DELETE, short8_name);
    try_set("an 8.3 file given another", short8_name, DELETE, L"SFN4X.TXT");
    show_names("  then", short8_name);
    try_set("a directory", dir_name, DELETE, L"SFNDIR~1");
    show_names("  then", dir_name);

    try_set("set again", long_name, DELETE, L"SFNF~1.TXT");
    file = CreateFileW(L"SFNF~1.TXT", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                       OPEN_EXISTING, 0, NULL);
    printf("open by the short name: %s error %lu\n", file != INVALID_HANDLE_VALUE ? "opened" : "failed",
           file != INVALID_HANDLE_VALUE ? 0 : GetLastError());
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    shortpath[0] = 0;
    GetLongPathNameW(L"SFNF~1.TXT", shortpath, MAX_PATH);
    printf("long path of the short name [%ls]\n", shortpath);
    try_set("taken away", long_name, DELETE, L"");
    show_names("  then", long_name);
    file = CreateFileW(L"SFNF~1.TXT", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    printf("open by the old short name: %s error %lu\n", file != INVALID_HANDLE_VALUE ? "opened" : "failed",
           file != INVALID_HANDLE_VALUE ? 0 : GetLastError());
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);

    DeleteFileW(long_name);
    DeleteFileW(other_name);
    DeleteFileW(short8_name);
    RemoveDirectoryW(dir_name);
    set_privilege(L"SeRestorePrivilege", FALSE);
    return 0;
}
