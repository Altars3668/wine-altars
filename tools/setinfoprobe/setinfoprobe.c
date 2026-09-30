/* What NtSetInformationFile and NtQueryInformationFile leave in the caller's IO_STATUS_BLOCK when they
 * fail, which status a buffer that is too short or a class that does not apply gets, and what setting the
 * allocation size and a directory's case sensitivity does.
 *
 * Wine's tests expect the block untouched after a failed set (io.Status still 0xdeadbeef) for the
 * rename, link and completion cases they cover.  This asks the same of the cases they do not cover --
 * buffers shorter than the class needs, classes that are query-only or out of range, access checks --
 * and of a 32-bit build, which on 64-bit Windows goes through wow64's own copy of the block.  The block
 * is filled with 0xbe bytes before each call; "untouched" means it still is.  Files go in a new
 * directory under %TEMP%, removed at the end.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror setinfoprobe.c -o setinfoprobe.exe -lntdll
 *   i686-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror setinfoprobe.c -o setinfoprobe32.exe -lntdll
 */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <string.h>

static WCHAR dir[MAX_PATH], path[MAX_PATH], other[MAX_PATH];

static void report(const char *what, int class, ULONG len, NTSTATUS status, const IO_STATUS_BLOCK *io)
{
    IO_STATUS_BLOCK pattern;

    memset(&pattern, 0xbe, sizeof(pattern));
    if (!memcmp(io, &pattern, sizeof(*io)))
        printf("%-34s class %3d len %4lu: %#010lx, io untouched\n", what, class, len, (unsigned long)status);
    else
        printf("%-34s class %3d len %4lu: %#010lx, io Status %#010lx Information %#lx\n", what, class, len,
               (unsigned long)status, (unsigned long)io->Status, (unsigned long)io->Information);
}

static void set(const char *what, HANDLE handle, void *buf, ULONG len, int class)
{
    IO_STATUS_BLOCK io;
    NTSTATUS status;

    memset(&io, 0xbe, sizeof(io));
    status = NtSetInformationFile(handle, &io, buf, len, (FILE_INFORMATION_CLASS)class);
    report(what, class, len, status, &io);
}

static void query(const char *what, HANDLE handle, void *buf, ULONG len, int class)
{
    IO_STATUS_BLOCK io;
    NTSTATUS status;

    memset(&io, 0xbe, sizeof(io));
    status = NtQueryInformationFile(handle, &io, buf, len, (FILE_INFORMATION_CLASS)class);
    report(what, class, len, status, &io);
}

static void sizes(const char *what, HANDLE handle)
{
    FILE_STANDARD_INFORMATION std;
    IO_STATUS_BLOCK io;
    NTSTATUS status;

    memset(&std, 0, sizeof(std));
    status = NtQueryInformationFile(handle, &io, &std, sizeof(std), FileStandardInformation);
    printf("%-34s %#010lx end of file %lu, allocation %lu\n", what, (unsigned long)status,
           (unsigned long)std.EndOfFile.QuadPart, (unsigned long)std.AllocationSize.QuadPart);
}

static HANDLE open_file(const WCHAR *name, DWORD access, DWORD flags)
{
    return CreateFileW(name, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_ALWAYS,
                       flags, NULL);
}

static HANDLE open_dir(DWORD access)
{
    return CreateFileW(dir, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS, NULL);
}

int main(void)
{
    static const struct { const char *name; int class; } settable[] =
    {
        {"FileBasicInformation", 4},
        {"FileRenameInformation", 10},
        {"FileLinkInformation", 11},
        {"FileDispositionInformation", 13},
        {"FilePositionInformation", 14},
        {"FileAllocationInformation", 19},
        {"FileEndOfFileInformation", 20},
        {"FileCompletionInformation", 30},
        {"FileValidDataLengthInformation", 39},
        {"FileIoCompletionNotificationInfo", 41},
        {"FileIoPriorityHintInformation", 43},
        {"FileDispositionInformationEx", 64},
        {"FileRenameInformationEx", 65},
        {"FileCaseSensitiveInformation", 71},
        {"FileLinkInformationEx", 72},
    };
    static const struct { const char *name; int class; } unsettable[] =
    {
        {"class 0", 0},
        {"FileStandardInformation", 5},
        {"FileAllInformation", 18},
        {"class 0xffff", 0xffff},
    };
    union { BYTE bytes[1024]; LONGLONG align; } buf;
    FILE_RENAME_INFORMATION *rename_info = (FILE_RENAME_INFORMATION *)buf.bytes;
    struct { HANDLE port; ULONG_PTR key; } completion;   /* FILE_COMPLETION_INFORMATION */
    BOOLEAN disposition;                              /* FILE_DISPOSITION_INFORMATION */
    FILE_ALLOCATION_INFO alloc_info;
    FILE_BASIC_INFORMATION basic;
    LARGE_INTEGER value;
    WCHAR temp[MAX_PATH];
    HANDLE handle, port;
    BOOL wow64 = FALSE, ret;
    unsigned int i;
    ULONG flags;
    size_t len;

    IsWow64Process(GetCurrentProcess(), &wow64);
    printf("%u-bit%s\n", (unsigned int)sizeof(void *) * 8, wow64 ? " under wow64" : "");

    GetTempPathW(MAX_PATH, temp);
    swprintf(dir, MAX_PATH, L"%lssetinfoprobe-%lu", temp, GetCurrentProcessId());
    CreateDirectoryW(dir, NULL);
    swprintf(path, MAX_PATH, L"%ls\\file.txt", dir);
    swprintf(other, MAX_PATH, L"%ls\\other.txt", dir);
    CloseHandle(open_file(other, GENERIC_WRITE, 0));

    handle = open_file(path, GENERIC_READ | GENERIC_WRITE | DELETE, 0);
    if (handle == INVALID_HANDLE_VALUE)
    {
        printf("CreateFile failed: %lu\n", GetLastError());
        return 1;
    }

    printf("-- zeroed buffers shorter than the class needs\n");
    memset(&buf, 0, sizeof(buf));
    for (i = 0; i < ARRAYSIZE(settable); i++)
    {
        set(settable[i].name, handle, &buf, 0, settable[i].class);
        set(settable[i].name, handle, &buf, 1, settable[i].class);
    }

    printf("-- classes that cannot be set, zeroed buffer of 1024 bytes\n");
    for (i = 0; i < ARRAYSIZE(unsettable); i++)
        set(unsettable[i].name, handle, &buf, sizeof(buf), unsettable[i].class);

    printf("-- failures with a buffer of the right size\n");
    CloseHandle(handle);
    handle = open_file(path, GENERIC_READ, 0);
    memset(&basic, 0, sizeof(basic));
    basic.FileAttributes = FILE_ATTRIBUTE_HIDDEN;
    set("basic, read-only handle", handle, &basic, sizeof(basic), 4);
    value.QuadPart = 4;
    set("end of file, read-only handle", handle, &value, sizeof(value), 20);
    disposition = TRUE;
    set("disposition, handle without DELETE", handle, &disposition, sizeof(disposition), 13);
    CloseHandle(handle);

    handle = open_file(path, GENERIC_READ | GENERIC_WRITE | DELETE, 0);
    memset(&buf, 0, sizeof(buf));
    len = wcslen(other) * sizeof(WCHAR);
    rename_info->FileNameLength = (ULONG)len + 4 * sizeof(WCHAR);
    memcpy(rename_info->FileName, L"\\??\\", 4 * sizeof(WCHAR));
    memcpy(rename_info->FileName + 4, other, len);
    set("rename over an existing file", handle, rename_info,
        (ULONG)(FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName) + rename_info->FileNameLength), 10);
    port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    completion.port = port;
    completion.key = 1;
    set("completion, handle not overlapped", handle, &completion, sizeof(completion), 30);
    completion.port = (HANDLE)(ULONG_PTR)0xdead0;
    set("completion, not a handle", handle, &completion, sizeof(completion), 30);
    CloseHandle(port);

    printf("-- successes\n");
    value.QuadPart = 0;
    set("position 0", handle, &value, sizeof(value), 14);
    value.QuadPart = 4;
    set("end of file 4", handle, &value, sizeof(value), 20);
    disposition = FALSE;
    set("disposition FALSE", handle, &disposition, sizeof(disposition), 13);
    CloseHandle(handle);

    printf("-- queries that fail or do not fit\n");
    handle = open_file(path, GENERIC_READ, 0);
    memset(&buf, 0, sizeof(buf));
    query("FileBasicInformation", handle, &buf, 0, 4);
    query("FileBasicInformation", handle, &buf, 1, 4);
    query("FileStandardInformation", handle, &buf, 1, 5);
    query("FileNameInformation", handle, &buf, 6, 9);
    query("FileAllInformation", handle, &buf, 100, 18);
    query("class 0", handle, &buf, sizeof(buf), 0);
    query("FileRenameInformation", handle, &buf, sizeof(buf), 10);
    query("FileAllocationInformation", handle, &buf, sizeof(buf), 19);
    query("FileEndOfFileInformation", handle, &buf, sizeof(buf), 20);
    query("FilePipeInformation, not a pipe", handle, &buf, sizeof(buf), 23);
    query("class 0xffff", handle, &buf, sizeof(buf), 0xffff);
    CloseHandle(handle);

    printf("-- allocation size\n");
    handle = open_file(path, GENERIC_READ | GENERIC_WRITE, 0);
    sizes("4-byte file", handle);
    value.QuadPart = 1 << 20;
    set("allocation 1 MiB", handle, &value, sizeof(value), 19);
    sizes("after", handle);
    value.QuadPart = 1000;
    set("allocation 1000", handle, &value, sizeof(value), 19);
    sizes("after", handle);
    value.QuadPart = 2;
    set("allocation 2", handle, &value, sizeof(value), 19);
    sizes("after", handle);
    value.QuadPart = 0;
    set("allocation 0", handle, &value, sizeof(value), 19);
    sizes("after", handle);
    value.QuadPart = -1;
    set("allocation -1", handle, &value, sizeof(value), 19);
    sizes("after", handle);
    alloc_info.AllocationSize.QuadPart = 1 << 16;
    SetLastError(0xdeadbeef);
    ret = SetFileInformationByHandle(handle, FileAllocationInfo, &alloc_info, sizeof(alloc_info));
    printf("SetFileInformationByHandle FileAllocationInfo 64 KiB: %d, error %lu\n", ret, ret ? 0 : GetLastError());
    sizes("after", handle);
    CloseHandle(handle);
    value.QuadPart = 1 << 20;
    handle = open_file(path, GENERIC_READ, 0);
    set("allocation, read-only handle", handle, &value, sizeof(value), 19);
    CloseHandle(handle);
    handle = open_file(path, FILE_APPEND_DATA | SYNCHRONIZE, 0);
    set("allocation, append-only handle", handle, &value, sizeof(value), 19);
    CloseHandle(handle);
    handle = open_dir(GENERIC_READ | GENERIC_WRITE);
    set("allocation, directory", handle, &value, sizeof(value), 19);
    CloseHandle(handle);

    printf("-- access, mode and alignment, alone and in FileAllInformation\n");
    {
        static const struct { DWORD access, options; const char *name; } opens[] =
        {
            {GENERIC_READ, 0, "GENERIC_READ"},
            {GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, FILE_SYNCHRONOUS_IO_NONALERT | FILE_SEQUENTIAL_ONLY,
             "read/write, synchronous, sequential"},
            {FILE_READ_ATTRIBUTES | SYNCHRONIZE, FILE_SYNCHRONOUS_IO_ALERT | FILE_WRITE_THROUGH,
             "read attributes, alertable, write through"},
            {GENERIC_READ | SYNCHRONIZE, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NO_INTERMEDIATE_BUFFERING,
             "read, synchronous, no buffering"},
        };
        union { FILE_ALL_INFORMATION all; BYTE bytes[1024]; } all;
        UNICODE_STRING nt_name;
        OBJECT_ATTRIBUTES attr;
        IO_STATUS_BLOCK io;
        NTSTATUS status;
        ULONG values[3];

        RtlDosPathNameToNtPathName_U(path, &nt_name, NULL, NULL);
        InitializeObjectAttributes(&attr, &nt_name, OBJ_CASE_INSENSITIVE, NULL, NULL);
        for (i = 0; i < ARRAYSIZE(opens); i++)
        {
            status = NtOpenFile(&handle, opens[i].access, &attr, &io, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                opens[i].options);
            if (status)
            {
                printf("%s: open %#010lx\n", opens[i].name, (unsigned long)status);
                continue;
            }
            memset(values, 0xcc, sizeof(values));
            NtQueryInformationFile(handle, &io, &values[0], sizeof(ULONG), (FILE_INFORMATION_CLASS)8);   /* access */
            NtQueryInformationFile(handle, &io, &values[1], sizeof(ULONG), (FILE_INFORMATION_CLASS)16);  /* mode */
            status = NtQueryInformationFile(handle, &io, &values[2], sizeof(ULONG), (FILE_INFORMATION_CLASS)17);
            printf("%s: access %#lx, mode %#lx, alignment %#lx (%#010lx)\n", opens[i].name, values[0], values[1],
                   values[2], (unsigned long)status);
            memset(&all, 0xcc, sizeof(all));
            status = NtQueryInformationFile(handle, &io, &all, sizeof(all), (FILE_INFORMATION_CLASS)18);
            printf("%s: all %#010lx: access %#lx, mode %#lx, alignment %#lx, delete pending %u, links %lu\n",
                   opens[i].name, (unsigned long)status, all.all.AccessInformation.AccessFlags,
                   all.all.ModeInformation.Mode, all.all.AlignmentInformation.AlignmentRequirement,
                   all.all.StandardInformation.DeletePending, all.all.StandardInformation.NumberOfLinks);
            CloseHandle(handle);
        }
        RtlFreeUnicodeString(&nt_name);
    }

    printf("-- case sensitivity\n");
    handle = open_dir(FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES);
    flags = 0xdeadbeef;
    query("query, directory", handle, &flags, sizeof(flags), 71);
    printf("flags %#lx\n", flags);
    flags = 1;
    set("case sensitive, directory", handle, &flags, sizeof(flags), 71);
    flags = 0xdeadbeef;
    query("query, directory", handle, &flags, sizeof(flags), 71);
    printf("flags %#lx\n", flags);
    flags = 0;
    set("case insensitive, directory", handle, &flags, sizeof(flags), 71);
    flags = 2;
    set("flags 2, directory", handle, &flags, sizeof(flags), 71);
    CloseHandle(handle);
    handle = open_dir(FILE_READ_ATTRIBUTES);
    flags = 1;
    set("case sensitive, no write access", handle, &flags, sizeof(flags), 71);
    CloseHandle(handle);
    handle = open_file(path, GENERIC_READ | GENERIC_WRITE, 0);
    set("case sensitive, file", handle, &flags, sizeof(flags), 71);
    flags = 0xdeadbeef;
    query("query, file", handle, &flags, sizeof(flags), 71);
    printf("flags %#lx\n", flags);
    CloseHandle(handle);

    DeleteFileW(path);
    DeleteFileW(other);
    RemoveDirectoryW(dir);
    return 0;
}
