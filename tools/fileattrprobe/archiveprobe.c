/* archiveprobe: when a file's archive attribute comes back after it was cleared -- writing, extending, renaming,
 * copying, writing over, setting times, reading -- what a directory has, what a new file gets, and what
 * GetVolumePathName says to a drive in lower case.  Works in a directory of its own under the current one;
 * prints results only. */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

static WCHAR base[MAX_PATH];

static const WCHAR *path(const WCHAR *name)
{
    static WCHAR buffer[4][MAX_PATH];
    static unsigned int next;
    WCHAR *ret = buffer[next++ % 4];

    swprintf(ret, MAX_PATH, L"%ls\\%ls", base, name);
    return ret;
}

static void show(const char *what, const WCHAR *name)
{
    printf("  %-40s %#lx\n", what, GetFileAttributesW(path(name)));
}

static void make(const WCHAR *name)
{
    HANDLE file;
    DWORD len;

    SetFileAttributesW(path(name), FILE_ATTRIBUTE_NORMAL);
    file = CreateFileW(path(name), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    WriteFile(file, "data", 4, &len, NULL);
    CloseHandle(file);
}

/* a file with its archive attribute cleared */
static void cleared(const WCHAR *name)
{
    make(name);
    SetFileAttributesW(path(name), FILE_ATTRIBUTE_NORMAL);
}

static HANDLE open_file(const WCHAR *name, DWORD access, DWORD disposition)
{
    return CreateFileW(path(name), access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                       disposition, FILE_ATTRIBUTE_NORMAL, NULL);
}

int main(void)
{
    static const WCHAR *volumes[] = { L"c:\\", L"c:\\windows", L"c:\\Windows\\system32", L"C:\\windows", L"c:" };
    FILE_BASIC_INFORMATION basic;
    WIN32_FIND_DATAW find;
    IO_STATUS_BLOCK io;
    FILETIME times[3];
    WCHAR volume[MAX_PATH];
    HANDLE file;
    char buffer[4];
    unsigned int i;
    DWORD len;

    setvbuf(stdout, NULL, _IONBF, 0);
    GetCurrentDirectoryW(ARRAYSIZE(base), base);
    wcscat(base, L"\\archive");
    CreateDirectoryW(base, NULL);

    printf("a new file\n");
    make(L"a.txt");
    show("created with FILE_ATTRIBUTE_NORMAL", L"a.txt");
    SetFileAttributesW(path(L"a.txt"), FILE_ATTRIBUTE_NORMAL);
    show("set to FILE_ATTRIBUTE_NORMAL", L"a.txt");
    SetFileAttributesW(path(L"a.txt"), FILE_ATTRIBUTE_HIDDEN);
    show("set to HIDDEN", L"a.txt");
    SetFileAttributesW(path(L"a.txt"), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE);
    show("set to HIDDEN | ARCHIVE", L"a.txt");
    SetFileAttributesW(path(L"a.txt"), FILE_ATTRIBUTE_READONLY);
    show("set to READONLY", L"a.txt");
    SetFileAttributesW(path(L"a.txt"), FILE_ATTRIBUTE_TEMPORARY);
    show("set to TEMPORARY", L"a.txt");
    SetFileAttributesW(path(L"a.txt"), 0);
    show("set to 0", L"a.txt");
    file = FindFirstFileW(path(L"a.txt"), &find);
    printf("  %-40s %#lx\n", "FindFirstFile", find.dwFileAttributes);
    FindClose(file);
    file = open_file(L"a.txt", FILE_READ_ATTRIBUTES, OPEN_EXISTING);
    NtQueryInformationFile(file, &io, &basic, sizeof(basic), FileBasicInformation);
    printf("  %-40s %#lx\n", "FileBasicInformation", basic.FileAttributes);
    CloseHandle(file);

    printf("a file whose archive attribute was cleared\n");
    cleared(L"a.txt");
    file = open_file(L"a.txt", GENERIC_WRITE, OPEN_EXISTING);
    show("opened for writing", L"a.txt");
    WriteFile(file, "x", 1, &len, NULL);
    show("written to, still open", L"a.txt");
    FlushFileBuffers(file);
    show("flushed, still open", L"a.txt");
    CloseHandle(file);
    show("closed", L"a.txt");

    cleared(L"a.txt");
    file = open_file(L"a.txt", GENERIC_WRITE, OPEN_EXISTING);
    CloseHandle(file);
    show("opened for writing, closed", L"a.txt");

    cleared(L"a.txt");
    file = open_file(L"a.txt", GENERIC_WRITE, OPEN_EXISTING);
    SetFilePointer(file, 100, NULL, FILE_BEGIN);
    SetEndOfFile(file);
    show("extended, still open", L"a.txt");
    CloseHandle(file);
    show("extended, closed", L"a.txt");

    cleared(L"a.txt");
    file = open_file(L"a.txt", GENERIC_WRITE, OPEN_EXISTING);
    SetFilePointer(file, 1, NULL, FILE_BEGIN);
    SetEndOfFile(file);
    CloseHandle(file);
    show("shortened, closed", L"a.txt");

    cleared(L"a.txt");
    file = open_file(L"a.txt", GENERIC_READ, OPEN_EXISTING);
    ReadFile(file, buffer, sizeof(buffer), &len, NULL);
    CloseHandle(file);
    show("read, closed", L"a.txt");

    cleared(L"a.txt");
    file = open_file(L"a.txt", FILE_WRITE_ATTRIBUTES, OPEN_EXISTING);
    GetSystemTimeAsFileTime(&times[0]);
    SetFileTime(file, &times[0], &times[0], &times[0]);
    CloseHandle(file);
    show("times set, closed", L"a.txt");

    cleared(L"a.txt");
    MoveFileW(path(L"a.txt"), path(L"b.txt"));
    show("renamed", L"b.txt");
    MoveFileW(path(L"b.txt"), path(L"a.txt"));

    cleared(L"a.txt");
    CopyFileW(path(L"a.txt"), path(L"c.txt"), FALSE);
    show("copied: the copy", L"c.txt");
    show("copied: the original", L"a.txt");
    SetFileAttributesW(path(L"c.txt"), FILE_ATTRIBUTE_HIDDEN);
    CopyFileW(path(L"c.txt"), path(L"d.txt"), FALSE);
    show("hidden copied: the copy", L"d.txt");

    cleared(L"a.txt");
    file = open_file(L"a.txt", GENERIC_WRITE, CREATE_ALWAYS);
    CloseHandle(file);
    show("written over (CREATE_ALWAYS)", L"a.txt");

    cleared(L"a.txt");
    file = open_file(L"a.txt", GENERIC_WRITE, TRUNCATE_EXISTING);
    CloseHandle(file);
    show("truncated (TRUNCATE_EXISTING)", L"a.txt");

    cleared(L"a.txt");
    file = CreateFileW(path(L"a.txt:s"), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    WriteFile(file, "x", 1, &len, NULL);
    CloseHandle(file);
    show("a stream written", L"a.txt");

    cleared(L"a.txt");
    CreateHardLinkW(path(L"e.txt"), path(L"a.txt"), NULL);
    show("hard link made: the file", L"a.txt");
    show("hard link made: the link", L"e.txt");

    printf("a directory\n");
    CreateDirectoryW(path(L"d"), NULL);
    show("created", L"d");
    SetFileAttributesW(path(L"d"), FILE_ATTRIBUTE_ARCHIVE);
    show("set to ARCHIVE", L"d");
    make(L"d\\f.txt");
    show("a file made in it", L"d");
    SetFileAttributesW(path(L"d"), FILE_ATTRIBUTE_NORMAL);
    show("set to FILE_ATTRIBUTE_NORMAL", L"d");

    printf("GetVolumePathName\n");
    for (i = 0; i < ARRAYSIZE(volumes); i++)
    {
        if (GetVolumePathNameW(volumes[i], volume, ARRAYSIZE(volume)))
            printf("  %-40ls %ls\n", volumes[i], volume);
        else printf("  %-40ls error %lu\n", volumes[i], GetLastError());
    }

    SetFileAttributesW(path(L"a.txt"), FILE_ATTRIBUTE_NORMAL);
    SetFileAttributesW(path(L"c.txt"), FILE_ATTRIBUTE_NORMAL);
    SetFileAttributesW(path(L"d.txt"), FILE_ATTRIBUTE_NORMAL);
    DeleteFileW(path(L"a.txt"));
    DeleteFileW(path(L"c.txt"));
    DeleteFileW(path(L"d.txt"));
    DeleteFileW(path(L"e.txt"));
    DeleteFileW(path(L"d\\f.txt"));
    RemoveDirectoryW(path(L"d"));
    RemoveDirectoryW(base);
    return 0;
}
