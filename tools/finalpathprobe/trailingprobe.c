/* trailingprobe: what the file functions say to a file's name with a backslash after it ("f.txt\"), and to a file
 * used as a directory ("f.txt\x"): CreateFile, NtCreateFile with each directory option and relative to a directory
 * handle, attributes, delete, move, copy and find; the same names without the backslash, and a directory's name with
 * one, as the controls.  Works in a directory of its own under the current one, made again after each call that
 * could change it; prints results only. */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

NTSTATUS WINAPI NtQueryAttributesFile(const OBJECT_ATTRIBUTES *, FILE_BASIC_INFORMATION *);
NTSTATUS WINAPI NtQueryFullAttributesFile(const OBJECT_ATTRIBUTES *, void *);

static WCHAR base[MAX_PATH];

static void make_files(void)
{
    WCHAR path[MAX_PATH];
    HANDLE handle;
    DWORD len;

    CreateDirectoryW(base, NULL);
    swprintf(path, ARRAYSIZE(path), L"%ls\\f.txt", base);
    handle = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    WriteFile(handle, "x", 1, &len, NULL);
    CloseHandle(handle);
    swprintf(path, ARRAYSIZE(path), L"%ls\\s.txt", base);
    handle = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    WriteFile(handle, "yy", 2, &len, NULL);
    CloseHandle(handle);
    swprintf(path, ARRAYSIZE(path), L"%ls\\d", base);
    CreateDirectoryW(path, NULL);
}

/* what is in the directory now, other than f.txt, s.txt and d as they should be */
static void show_changes(void)
{
    WIN32_FIND_DATAW find;
    WCHAR path[MAX_PATH];
    HANDLE handle;

    swprintf(path, ARRAYSIZE(path), L"%ls\\*", base);
    if ((handle = FindFirstFileW(path, &find)) == INVALID_HANDLE_VALUE) return;
    do
    {
        if (!wcscmp(find.cFileName, L".") || !wcscmp(find.cFileName, L"..")) continue;
        if (!wcscmp(find.cFileName, L"f.txt") && find.dwFileAttributes == FILE_ATTRIBUTE_ARCHIVE &&
            find.nFileSizeLow == 1) continue;
        if (!wcscmp(find.cFileName, L"s.txt") && find.dwFileAttributes == FILE_ATTRIBUTE_ARCHIVE &&
            find.nFileSizeLow == 2) continue;
        if (!wcscmp(find.cFileName, L"d") && find.dwFileAttributes == FILE_ATTRIBUTE_DIRECTORY) continue;
        printf("    now there: %ls attributes %#lx size %lu\n", find.cFileName, find.dwFileAttributes,
               find.nFileSizeLow);
    } while (FindNextFileW(handle, &find));
    FindClose(handle);
    swprintf(path, ARRAYSIZE(path), L"%ls\\f.txt", base);
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) printf("    f.txt is gone\n");
    swprintf(path, ARRAYSIZE(path), L"%ls\\d", base);
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) printf("    d is gone\n");
}

/* remove whatever a call made and put f.txt, s.txt and d back */
static void reset(void)
{
    static const WCHAR *names[] = { L"n.txt", L"n3", L"other.txt", L"f.txt", L"d" };
    WCHAR path[MAX_PATH];
    unsigned int i;

    for (i = 0; i < ARRAYSIZE(names); i++)
    {
        swprintf(path, ARRAYSIZE(path), L"%ls\\%ls", base, names[i]);
        SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
        if (!RemoveDirectoryW(path)) DeleteFileW(path);
    }
    make_files();
}

static void nt_create(HANDLE root, const WCHAR *name, ULONG disposition, ULONG options, const char *what)
{
    WCHAR path[MAX_PATH + 8];
    UNICODE_STRING str;
    OBJECT_ATTRIBUTES attr;
    IO_STATUS_BLOCK io;
    HANDLE handle;
    NTSTATUS status;

    if (root) wcscpy(path, name);
    else swprintf(path, ARRAYSIZE(path), L"\\??\\%ls\\%ls", base, name);
    RtlInitUnicodeString(&str, path);
    InitializeObjectAttributes(&attr, &str, OBJ_CASE_INSENSITIVE, root, NULL);
    status = NtCreateFile(&handle, GENERIC_READ | SYNCHRONIZE, &attr, &io, NULL, FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, disposition,
                          options | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    printf("  NtCreateFile%s [%ls] %s: %#lx\n", root ? " relative" : "", name, what, status);
    if (!status) CloseHandle(handle);
    show_changes();
    reset();
}

static void nt_query(const WCHAR *name)
{
    WCHAR path[MAX_PATH + 8];
    UNICODE_STRING str;
    OBJECT_ATTRIBUTES attr;
    FILE_BASIC_INFORMATION basic;
    BYTE full[64];

    swprintf(path, ARRAYSIZE(path), L"\\??\\%ls\\%ls", base, name);
    RtlInitUnicodeString(&str, path);
    InitializeObjectAttributes(&attr, &str, OBJ_CASE_INSENSITIVE, NULL, NULL);
    printf("  NtQueryAttributesFile [%ls]: %#lx\n", name, NtQueryAttributesFile(&attr, &basic));
    printf("  NtQueryFullAttributesFile [%ls]: %#lx\n", name, NtQueryFullAttributesFile(&attr, full));
}

static void report(const char *call, const WCHAR *name, BOOL ret)
{
    DWORD error = GetLastError();

    if (ret) printf("  %s [%ls]: ok\n", call, name);
    else printf("  %s [%ls]: fails, error %lu\n", call, name, error);
    show_changes();
    reset();
}

static void create_file(const WCHAR *name, DWORD disposition, DWORD flags, const char *what)
{
    WCHAR path[MAX_PATH];
    HANDLE handle;
    DWORD error;

    swprintf(path, ARRAYSIZE(path), L"%ls\\%ls", base, name);
    SetLastError(0xdeadbeef);
    handle = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, disposition, flags, NULL);
    error = GetLastError();
    if (handle == INVALID_HANDLE_VALUE) printf("  CreateFile [%ls] %s: fails, error %lu\n", name, what, error);
    else
    {
        printf("  CreateFile [%ls] %s: ok, error %lu\n", name, what, error);
        CloseHandle(handle);
    }
    show_changes();
    reset();
}

static void win32(const WCHAR *name)
{
    WCHAR path[MAX_PATH], other[MAX_PATH];
    WIN32_FIND_DATAW find;
    HANDLE handle;
    DWORD attrs, error;
    BOOL ret;

    swprintf(path, ARRAYSIZE(path), L"%ls\\%ls", base, name);
    swprintf(other, ARRAYSIZE(other), L"%ls\\other.txt", base);

    create_file(name, OPEN_EXISTING, 0, "OPEN_EXISTING");
    create_file(name, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, "OPEN_EXISTING backup");
    create_file(name, OPEN_ALWAYS, 0, "OPEN_ALWAYS");
    create_file(name, CREATE_ALWAYS, 0, "CREATE_ALWAYS");
    create_file(name, CREATE_NEW, 0, "CREATE_NEW");
    create_file(name, TRUNCATE_EXISTING, 0, "TRUNCATE_EXISTING");

    SetLastError(0xdeadbeef);
    attrs = GetFileAttributesW(path);
    error = GetLastError();
    if (attrs == INVALID_FILE_ATTRIBUTES) printf("  GetFileAttributes [%ls]: fails, error %lu\n", name, error);
    else printf("  GetFileAttributes [%ls]: %#lx\n", name, attrs);
    SetLastError(0xdeadbeef);
    ret = SetFileAttributesW(path, FILE_ATTRIBUTE_HIDDEN);
    report("SetFileAttributes", name, ret);
    SetLastError(0xdeadbeef);
    handle = FindFirstFileW(path, &find);
    error = GetLastError();
    if (handle == INVALID_HANDLE_VALUE) printf("  FindFirstFile [%ls]: fails, error %lu\n", name, error);
    else
    {
        printf("  FindFirstFile [%ls]: ok, %ls\n", name, find.cFileName);
        FindClose(handle);
    }
    SetLastError(0xdeadbeef);
    ret = CopyFileW(path, other, FALSE);
    report("CopyFile from", name, ret);
    swprintf(other, ARRAYSIZE(other), L"%ls\\s.txt", base);
    SetLastError(0xdeadbeef);
    ret = CopyFileW(other, path, FALSE);
    report("CopyFile to", name, ret);
    swprintf(other, ARRAYSIZE(other), L"%ls\\other.txt", base);
    SetLastError(0xdeadbeef);
    ret = MoveFileW(path, other);
    report("MoveFile from", name, ret);
    SetLastError(0xdeadbeef);
    ret = DeleteFileW(path);
    report("DeleteFile", name, ret);
    SetLastError(0xdeadbeef);
    ret = RemoveDirectoryW(path);
    report("RemoveDirectory", name, ret);
    SetLastError(0xdeadbeef);
    ret = CreateDirectoryW(path, NULL);
    report("CreateDirectory", name, ret);
}

static void nt_all(const WCHAR *name)
{
    WCHAR path[MAX_PATH + 8];
    UNICODE_STRING str;
    OBJECT_ATTRIBUTES attr;
    IO_STATUS_BLOCK io;
    HANDLE root;

    nt_create(NULL, name, FILE_OPEN, 0, "FILE_OPEN");
    nt_create(NULL, name, FILE_OPEN, FILE_NON_DIRECTORY_FILE, "FILE_OPEN non-directory");
    nt_create(NULL, name, FILE_OPEN, FILE_DIRECTORY_FILE, "FILE_OPEN directory");
    nt_create(NULL, name, FILE_OPEN_IF, 0, "FILE_OPEN_IF");
    nt_create(NULL, name, FILE_OPEN_IF, FILE_DIRECTORY_FILE, "FILE_OPEN_IF directory");
    nt_create(NULL, name, FILE_OVERWRITE_IF, 0, "FILE_OVERWRITE_IF");
    nt_create(NULL, name, FILE_SUPERSEDE, 0, "FILE_SUPERSEDE");
    nt_create(NULL, name, FILE_CREATE, 0, "FILE_CREATE");
    nt_create(NULL, name, FILE_CREATE, FILE_DIRECTORY_FILE, "FILE_CREATE directory");
    nt_query(name);

    swprintf(path, ARRAYSIZE(path), L"\\??\\%ls", base);
    RtlInitUnicodeString(&str, path);
    InitializeObjectAttributes(&attr, &str, OBJ_CASE_INSENSITIVE, NULL, NULL);
    if (!NtOpenFile(&root, FILE_LIST_DIRECTORY | FILE_TRAVERSE | SYNCHRONIZE, &attr, &io,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT))
    {
        nt_create(root, name, FILE_OPEN, 0, "FILE_OPEN");
        nt_create(root, name, FILE_OPEN_IF, 0, "FILE_OPEN_IF");
        CloseHandle(root);
    }
}

int main(void)
{
    WCHAR path[MAX_PATH];

    setvbuf(stdout, NULL, _IONBF, 0);
    GetCurrentDirectoryW(ARRAYSIZE(base), base);
    wcscat(base, L"\\trailing");
    reset();

    printf("a file\n");
    win32(L"f.txt");
    nt_all(L"f.txt");

    printf("a file, a backslash after it\n");
    win32(L"f.txt\\");
    nt_all(L"f.txt\\");

    printf("a file used as a directory\n");
    win32(L"f.txt\\x");
    nt_all(L"f.txt\\x");

    printf("a new name, a backslash after it\n");
    win32(L"n.txt\\");
    nt_all(L"n.txt\\");

    printf("a directory\n");
    win32(L"d");
    nt_all(L"d");

    printf("a directory, a backslash after it\n");
    win32(L"d\\");
    nt_all(L"d\\");

    swprintf(path, ARRAYSIZE(path), L"%ls\\f.txt", base);
    DeleteFileW(path);
    swprintf(path, ARRAYSIZE(path), L"%ls\\s.txt", base);
    DeleteFileW(path);
    swprintf(path, ARRAYSIZE(path), L"%ls\\d", base);
    RemoveDirectoryW(path);
    RemoveDirectoryW(base);
    return 0;
}
