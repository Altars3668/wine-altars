/* What NtQueryDirectoryFileEx does with each of its query flags.
 *
 * Windows 10 added NtQueryDirectoryFileEx, which takes a flags word where NtQueryDirectoryFile takes
 * the two booleans ReturnSingleEntry and RestartScan; the App-V layer of Click-to-Run Office looks it
 * up to hook it, and Wine does not have it.  In a new directory holding a.txt, ab.txt, b.txt, c.dat
 * and a directory d, this prints, for a series of calls on one handle, the flags and mask passed,
 * the status, and the names that came back, so that each flag's effect on the directory cursor and
 * on the mask can be read off:
 *
 *   SL_RESTART_SCAN 0x1, SL_RETURN_SINGLE_ENTRY 0x2, SL_INDEX_SPECIFIED 0x4,
 *   SL_RETURN_ON_DISK_ENTRIES_ONLY 0x8, SL_NO_CURSOR_UPDATE 0x10, and bits that are none of them.
 *
 * It also compares NtQueryDirectoryFile with the same calls, and tries the information classes
 * Office's file dialogs and App-V use.  The temporary directory is removed at the end.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror dirqueryprobe.c -o dirqueryprobe.exe -lntdll
 */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <string.h>

#ifndef STATUS_NO_MORE_FILES
#define STATUS_NO_MORE_FILES ((NTSTATUS)0x80000006)
#endif

typedef NTSTATUS (WINAPI *query_ex_func)(HANDLE, HANDLE, void *, void *, IO_STATUS_BLOCK *, void *, ULONG,
                                          ULONG, ULONG, UNICODE_STRING *);
typedef NTSTATUS (WINAPI *query_func)(HANDLE, HANDLE, void *, void *, IO_STATUS_BLOCK *, void *, ULONG,
                                       ULONG, BOOLEAN, UNICODE_STRING *, BOOLEAN);

static query_ex_func query_ex;
static query_func query;

/* FILE_DIRECTORY_INFORMATION and FILE_NAMES_INFORMATION both end in a length and a name; these are
 * the offsets of the length for the classes asked for here */
static ULONG name_offset(ULONG class)
{
    switch (class)
    {
    case 1: return 0x3c;   /* FileDirectoryInformation */
    case 2: return 0x3c;   /* FileFullDirectoryInformation: length at 0x3c, name at 0x44 */
    case 3: return 0x3c;   /* FileBothDirectoryInformation: length at 0x3c, name at 0x5e */
    case 12: return 0x08;  /* FileNamesInformation */
    case 37: return 0x3c;  /* FileIdBothDirectoryInformation: length at 0x3c, name at 0x68 */
    case 38: return 0x3c;  /* FileIdFullDirectoryInformation: length at 0x3c, name at 0x50 */
    case 60: return 0x3c;  /* FileIdExtdDirectoryInformation: length at 0x3c, name at 0x58 */
    case 63: return 0x3c;  /* FileIdExtdBothDirectoryInformation: length at 0x3c, name at 0x72 */
    default: return 0;
    }
}

static ULONG name_start(ULONG class)
{
    switch (class)
    {
    case 1: return 0x40;
    case 2: return 0x44;
    case 3: return 0x5e;
    case 12: return 0x0c;
    case 37: return 0x68;
    case 38: return 0x50;
    case 60: return 0x58;
    case 63: return 0x72;
    default: return 0;
    }
}

static void print_names(const BYTE *buf, ULONG class, ULONG_PTR size)
{
    const BYTE *p = buf;

    if (!size) return;
    for (;;)
    {
        ULONG next = *(const ULONG *)p, len = *(const ULONG *)(p + name_offset(class));
        printf(" %.*ls", (int)(len / sizeof(WCHAR)), (const WCHAR *)(p + name_start(class)));
        if (!next) break;
        p += next;
        if (p >= buf + size) { printf(" (next past the data)"); break; }
    }
}

static void ask_ex(HANDLE dir, ULONG flags, const WCHAR *mask, ULONG class, ULONG length)
{
    static BYTE buf[4096];
    IO_STATUS_BLOCK io = { .Status = 0xdeadbeef, .Information = 0xdeadbeef };
    UNICODE_STRING str;
    NTSTATUS status;

    if (mask) RtlInitUnicodeString(&str, mask);
    memset(buf, 0xcc, sizeof(buf));
    status = query_ex(dir, NULL, NULL, NULL, &io, buf, length, class, flags, mask ? &str : NULL);
    printf("  ex flags %#lx mask %ls class %lu: %#lx, io %#lx %lu:", flags, mask ? mask : L"(none)", class,
           status, io.Status, (unsigned long)io.Information);
    if (!status) print_names(buf, class, io.Information);
    printf("\n");
}

static void ask(HANDLE dir, BOOLEAN single, BOOLEAN restart, const WCHAR *mask)
{
    static BYTE buf[4096];
    IO_STATUS_BLOCK io = { .Status = 0xdeadbeef, .Information = 0xdeadbeef };
    UNICODE_STRING str;
    NTSTATUS status;

    if (mask) RtlInitUnicodeString(&str, mask);
    status = query(dir, NULL, NULL, NULL, &io, buf, sizeof(buf), 1, single, mask ? &str : NULL, restart);
    printf("  old single %d restart %d mask %ls: %#lx:", single, restart, mask ? mask : L"(none)", status);
    if (!status) print_names(buf, 1, io.Information);
    printf("\n");
}

static HANDLE open_dir(const WCHAR *path)
{
    return CreateFileW(path, FILE_LIST_DIRECTORY | SYNCHRONIZE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
}

int main(void)
{
    static const WCHAR *files[] = {L"a.txt", L"ab.txt", L"b.txt", L"c.dat"};
    static const ULONG classes[] = {1, 2, 3, 12, 37, 38, 60, 63, 78, 79, 80, 81, 0, 100};
    WCHAR temp[MAX_PATH], dirname[MAX_PATH], path[MAX_PATH];
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    unsigned int i;
    HANDLE dir;

    query_ex = (void *)GetProcAddress(ntdll, "NtQueryDirectoryFileEx");
    query = (void *)GetProcAddress(ntdll, "NtQueryDirectoryFile");
    printf("NtQueryDirectoryFileEx: %s, ZwQueryDirectoryFileEx: %s\n", query_ex ? "present" : "absent",
           GetProcAddress(ntdll, "ZwQueryDirectoryFileEx") ? "present" : "absent");

    GetTempPathW(MAX_PATH, temp);
    swprintf(dirname, MAX_PATH, L"%lsdirqueryprobe-%lu", temp, GetCurrentProcessId());
    if (!CreateDirectoryW(dirname, NULL)) { printf("CreateDirectory: %lu\n", GetLastError()); return 1; }
    for (i = 0; i < ARRAYSIZE(files); i++)
    {
        HANDLE file;
        swprintf(path, MAX_PATH, L"%ls\\%ls", dirname, files[i]);
        file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
        CloseHandle(file);
    }
    swprintf(path, MAX_PATH, L"%ls\\d", dirname);
    CreateDirectoryW(path, NULL);

    /* the old function, for the cursor and the mask as Wine already has them */
    printf("NtQueryDirectoryFile, FileDirectoryInformation:\n");
    dir = open_dir(dirname);
    ask(dir, TRUE, FALSE, L"*.txt");
    ask(dir, TRUE, FALSE, NULL);
    ask(dir, TRUE, FALSE, L"*");
    ask(dir, FALSE, FALSE, NULL);
    ask(dir, FALSE, FALSE, NULL);
    ask(dir, FALSE, TRUE, L"b*");
    ask(dir, FALSE, TRUE, NULL);
    CloseHandle(dir);

    if (!query_ex) goto done;

    printf("the cursor and the mask:\n");
    dir = open_dir(dirname);
    ask_ex(dir, 0x2, L"*.txt", 1, 4096);          /* first call: the mask sticks */
    ask_ex(dir, 0x2, NULL, 1, 4096);
    ask_ex(dir, 0x2, L"*", 1, 4096);              /* a new mask without a restart */
    ask_ex(dir, 0x0, NULL, 1, 4096);
    ask_ex(dir, 0x0, NULL, 1, 4096);
    ask_ex(dir, 0x1, L"b*", 1, 4096);             /* a restart with a new mask */
    ask_ex(dir, 0x1, NULL, 1, 4096);              /* a restart with no mask */
    CloseHandle(dir);

    printf("no cursor update:\n");
    dir = open_dir(dirname);
    ask_ex(dir, 0x12, L"*", 1, 4096);
    ask_ex(dir, 0x12, L"*", 1, 4096);             /* the same entry again? */
    ask_ex(dir, 0x12, L"*.dat", 1, 4096);         /* a new mask each time? */
    ask_ex(dir, 0x2, NULL, 1, 4096);              /* where the cursor is now */
    ask_ex(dir, 0x2, NULL, 1, 4096);
    ask_ex(dir, 0x10, L"a*", 1, 4096);
    ask_ex(dir, 0x0, NULL, 1, 4096);
    CloseHandle(dir);

    printf("no cursor update as the first call, then without a mask:\n");
    dir = open_dir(dirname);
    ask_ex(dir, 0x12, L"ab*", 1, 4096);
    ask_ex(dir, 0x2, NULL, 1, 4096);
    ask_ex(dir, 0x2, NULL, 1, 4096);
    CloseHandle(dir);

    printf("the other flags:\n");
    dir = open_dir(dirname);
    ask_ex(dir, 0x4, NULL, 1, 4096);              /* an index nobody gave */
    ask_ex(dir, 0x5, NULL, 1, 4096);
    ask_ex(dir, 0x8, NULL, 1, 4096);              /* entries on disk only */
    ask_ex(dir, 0x9, NULL, 1, 4096);
    ask_ex(dir, 0x20, NULL, 1, 4096);             /* bits that are no flag */
    ask_ex(dir, 0x21, NULL, 1, 4096);
    ask_ex(dir, 0x100, NULL, 1, 4096);
    ask_ex(dir, 0x80000001, NULL, 1, 4096);
    ask_ex(dir, 0x1f, NULL, 1, 4096);
    CloseHandle(dir);

    printf("buffers:\n");
    dir = open_dir(dirname);
    ask_ex(dir, 0x1, NULL, 1, 0);
    ask_ex(dir, 0x1, NULL, 1, 0x40);              /* smaller than one entry with its name */
    ask_ex(dir, 0x1, NULL, 1, 0x48);
    ask_ex(dir, 0x1, NULL, 1, 0x60);              /* one entry, not two */
    CloseHandle(dir);

    printf("information classes:\n");
    for (i = 0; i < ARRAYSIZE(classes); i++)
    {
        dir = open_dir(dirname);
        if (name_offset(classes[i])) ask_ex(dir, 0x1, L"a*", classes[i], 4096);
        else
        {
            static BYTE buf[4096];
            IO_STATUS_BLOCK io = { .Status = 0xdeadbeef, .Information = 0xdeadbeef };
            NTSTATUS status = query_ex(dir, NULL, NULL, NULL, &io, buf, sizeof(buf), classes[i], 1, NULL);
            printf("  ex flags 0x1 class %lu: %#lx, io %#lx %lu\n", classes[i], status, io.Status, (unsigned long)io.Information);
        }
        CloseHandle(dir);
    }

    printf("a file, not a directory:\n");
    swprintf(path, MAX_PATH, L"%ls\\a.txt", dirname);
    dir = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    ask_ex(dir, 0x1, NULL, 1, 4096);
    CloseHandle(dir);

done:
    for (i = 0; i < ARRAYSIZE(files); i++)
    {
        swprintf(path, MAX_PATH, L"%ls\\%ls", dirname, files[i]);
        DeleteFileW(path);
    }
    swprintf(path, MAX_PATH, L"%ls\\d", dirname);
    RemoveDirectoryW(path);
    RemoveDirectoryW(dirname);
    return 0;
}
