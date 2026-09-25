/*
 * fileinfoprobe - what GetFileInformationByHandleEx answers for the classes
 * Wine answered ERROR_CALL_NOT_IMPLEMENTED for: storage, case sensitivity,
 * normalized name and remote protocol, for a file and a directory, local.
 *
 * Word asks for FileStorageInfo on its files as it opens them.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define _WIN32_WINNT 0x0a00
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>

/* as the SDK has them, for headers that do not yet */
typedef struct
{
    ULONG LogicalBytesPerSector;
    ULONG PhysicalBytesPerSectorForAtomicity;
    ULONG PhysicalBytesPerSectorForPerformance;
    ULONG FileSystemEffectivePhysicalBytesPerSectorForAtomicity;
    ULONG Flags;
    ULONG ByteOffsetForSectorAlignment;
    ULONG ByteOffsetForPartitionAlignment;
} STORAGE_INFO_;
#define FILE_STORAGE_INFO STORAGE_INFO_
typedef struct { ULONG Flags; } CASE_SENSITIVE_INFO_;
#define FILE_CASE_SENSITIVE_INFO CASE_SENSITIVE_INFO_

static void storage(HANDLE h)
{
    FILE_STORAGE_INFO info;
    BOOL ret;

    memset(&info, 0xcc, sizeof(info));
    SetLastError(0xdeadbeef);
    ret = GetFileInformationByHandleEx(h, FileStorageInfo, &info, sizeof(info));
    printf("  FileStorageInfo %d error %lu: logical %lu, physical atomicity %lu, performance %lu, "
           "fs atomicity %lu, flags %#lx, alignment %lu %lu\n", ret, ret ? 0 : GetLastError(),
           info.LogicalBytesPerSector, info.PhysicalBytesPerSectorForAtomicity,
           info.PhysicalBytesPerSectorForPerformance, info.FileSystemEffectivePhysicalBytesPerSectorForAtomicity,
           info.Flags, info.ByteOffsetForSectorAlignment, info.ByteOffsetForPartitionAlignment);
    SetLastError(0xdeadbeef);
    ret = GetFileInformationByHandleEx(h, FileStorageInfo, &info, sizeof(info) - 1);
    printf("  FileStorageInfo, one byte short: %d error %lu\n", ret, ret ? 0 : GetLastError());
}

static void case_sensitive(HANDLE h)
{
    FILE_CASE_SENSITIVE_INFO info;
    BOOL ret;

    memset(&info, 0xcc, sizeof(info));
    SetLastError(0xdeadbeef);
    ret = GetFileInformationByHandleEx(h, FileCaseSensitiveInfo, &info, sizeof(info));
    printf("  FileCaseSensitiveInfo %d error %lu: flags %#lx\n", ret, ret ? 0 : GetLastError(), info.Flags);
}

static void normalized(HANDLE h)
{
    union { FILE_NAME_INFO info; char buf[1024]; } u;
    BOOL ret;

    memset(&u, 0xcc, sizeof(u));
    SetLastError(0xdeadbeef);
    ret = GetFileInformationByHandleEx(h, FileNormalizedNameInfo, &u, sizeof(u));
    if (ret)
        printf("  FileNormalizedNameInfo %d: length %lu %.*ls\n", ret, u.info.FileNameLength,
               (int)(u.info.FileNameLength / sizeof(WCHAR)), u.info.FileName);
    else printf("  FileNormalizedNameInfo %d error %lu\n", ret, GetLastError());
    SetLastError(0xdeadbeef);
    ret = GetFileInformationByHandleEx(h, FileNameInfo, &u, sizeof(u));
    if (ret)
        printf("  FileNameInfo %d: length %lu %.*ls\n", ret, u.info.FileNameLength,
               (int)(u.info.FileNameLength / sizeof(WCHAR)), u.info.FileName);
    SetLastError(0xdeadbeef);
    ret = GetFileInformationByHandleEx(h, FileNormalizedNameInfo, &u, 8);
    printf("  FileNormalizedNameInfo, 8 bytes: %d error %lu, length %lu\n", ret, ret ? 0 : GetLastError(),
           u.info.FileNameLength);
}

static void remote(HANDLE h)
{
    FILE_REMOTE_PROTOCOL_INFO info;
    BOOL ret;

    memset(&info, 0xcc, sizeof(info));
    SetLastError(0xdeadbeef);
    ret = GetFileInformationByHandleEx(h, FileRemoteProtocolInfo, &info, sizeof(info));
    printf("  FileRemoteProtocolInfo %d error %lu: version %u, length %u, protocol %#lx\n", ret,
           ret ? 0 : GetLastError(), info.StructureVersion, info.StructureSize, info.Protocol);
}

int main(void)
{
    WCHAR dir[MAX_PATH], file[MAX_PATH];
    HANDLE h;

    setvbuf(stdout, NULL, _IONBF, 0);
    GetTempPathW(MAX_PATH, dir);
    wcscpy(file, dir);
    wcscat(file, L"fileinfoprobe.txt");
    h = CreateFileW(file, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    printf("a file in the temporary directory:\n");
    storage(h);
    case_sensitive(h);
    normalized(h);
    remote(h);
    {
        char buf[1024];
        BOOL ret;

        SetLastError(0xdeadbeef);
        ret = GetFileInformationByHandleEx(h, 21 /* FileDispositionInfoEx */, buf, sizeof(buf));
        printf("  FileDispositionInfoEx %d error %lu\n", ret, ret ? 0 : GetLastError());
        SetLastError(0xdeadbeef);
        ret = GetFileInformationByHandleEx(h, 22 /* FileRenameInfoEx */, buf, sizeof(buf));
        printf("  FileRenameInfoEx %d error %lu\n", ret, ret ? 0 : GetLastError());
    }
    CloseHandle(h);

    h = CreateFileW(dir, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS, NULL);
    printf("the temporary directory:\n");
    storage(h);
    case_sensitive(h);
    remote(h);
    CloseHandle(h);
    printf("done\n");
    return 0;
}
