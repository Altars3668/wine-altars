/*
 * What LockFileEx does on a handle opened for overlapped I/O and bound to an I/O completion port: what it returns
 * and leaves in the OVERLAPPED when the lock is granted at once, refused at once (LOCKFILE_FAIL_IMMEDIATELY), or
 * has to wait for another handle's lock to go; which of these post a completion packet, and what the packet
 * holds; with the low bit of hEvent set, and with FILE_SKIP_COMPLETION_PORT_ON_SUCCESS; a waiting lock cancelled
 * with CancelIoEx, and one whose handle is closed.  And a handle without overlapped I/O, whose waiting lock blocks;
 * and NtLockFile given no status block.
 */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

#ifndef FILE_SKIP_COMPLETION_PORT_ON_SUCCESS
#define FILE_SKIP_COMPLETION_PORT_ON_SUCCESS 0x1
#endif

static HANDLE port;
static char path[MAX_PATH];

static const char *status_name(ULONG_PTR status)
{
    static char buffer[16];

    switch (status)
    {
    case 0: return "STATUS_SUCCESS";
    case 0x103: return "STATUS_PENDING";
    case 0xc0000054: return "STATUS_FILE_LOCK_CONFLICT";
    case 0xc0000055: return "STATUS_LOCK_NOT_GRANTED";
    case 0xc0000005: return "STATUS_ACCESS_VIOLATION";
    }
    snprintf(buffer, sizeof(buffer), "%#Ix", status);
    return buffer;
}

/* what is waiting on the port, without waiting for more than a moment */
static void drain(const char *what)
{
    OVERLAPPED *ov;
    ULONG_PTR key;
    DWORD bytes;
    BOOL ret;
    int n = 0;

    for (;;)
    {
        SetLastError(0xdeadbeef);
        ret = GetQueuedCompletionStatus(port, &bytes, &key, &ov, 200);
        if (!ov) break;
        printf("    %s: packet ret %d error %lu, bytes %lu, key %#Ix, status %s\n", what, ret,
               ret ? 0 : GetLastError(), bytes, key, status_name(ov->Internal));
        n++;
    }
    if (!n) printf("    %s: no packet\n", what);
}

static void show(const char *what, BOOL ret, OVERLAPPED *ov)
{
    DWORD error = GetLastError();

    printf("  %s: ret %d error %lu, Internal %s, InternalHigh %Iu, event %s\n", what, ret, ret ? 0 : error,
           status_name(ov->Internal), ov->InternalHigh,
           ov->hEvent && !WaitForSingleObject((HANDLE)((ULONG_PTR)ov->hEvent & ~1), 0) ? "set" : "not set");
}

static HANDLE open_file(BOOL overlapped)
{
    return CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                       OPEN_EXISTING, overlapped ? FILE_FLAG_OVERLAPPED : 0, NULL);
}

static DWORD WINAPI unlock_later(void *arg)
{
    Sleep(300);
    printf("  (the other handle unlocks byte 0: %d)\n", UnlockFile(arg, 0, 0, 1, 0));
    return 0;
}

static DWORD WINAPI unlock_later4(void *arg)
{
    Sleep(300);
    printf("  (the other handle unlocks byte 4: %d)\n", UnlockFile(arg, 4, 0, 1, 0));
    return 0;
}

int main(void)
{
    NTSTATUS (WINAPI *pNtLockFile)(HANDLE, HANDLE, PIO_APC_ROUTINE, void *, IO_STATUS_BLOCK *, LARGE_INTEGER *,
                                   LARGE_INTEGER *, ULONG *, BOOLEAN, BOOLEAN);
    LARGE_INTEGER offset, length;
    NTSTATUS status;
    OVERLAPPED ov;
    HANDLE file, other, sync, thread, third;
    char dir[MAX_PATH];
    DWORD written;
    BOOL ret;

    setvbuf(stdout, NULL, _IONBF, 0);
    GetTempPathA(sizeof(dir), dir);
    GetTempFileNameA(dir, "lck", 0, path);
    file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    WriteFile(file, "0123456789", 10, &written, NULL);
    CloseHandle(file);

    file = open_file(TRUE);
    other = open_file(FALSE);
    port = CreateIoCompletionPort(file, NULL, 0x1234, 0);
    printf("port %s\n", port ? "made" : "failed");

    printf("granted at once:\n");
    memset(&ov, 0x55, sizeof(ov));
    ov.Offset = 0; ov.OffsetHigh = 0;
    ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    SetLastError(0xdeadbeef);
    ret = LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &ov);
    show("exclusive lock of byte 0", ret, &ov);
    drain("port");
    UnlockFile(file, 0, 0, 1, 0);

    printf("granted at once, event with the low bit set:\n");
    memset(&ov, 0x55, sizeof(ov));
    ov.Offset = 0; ov.OffsetHigh = 0;
    ov.hEvent = (HANDLE)((ULONG_PTR)CreateEventA(NULL, TRUE, FALSE, NULL) | 1);
    SetLastError(0xdeadbeef);
    ret = LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &ov);
    show("exclusive lock of byte 0", ret, &ov);
    drain("port");
    UnlockFile(file, 0, 0, 1, 0);

    printf("refused at once:\n");
    LockFile(other, 0, 0, 1, 0);
    memset(&ov, 0x55, sizeof(ov));
    ov.Offset = 0; ov.OffsetHigh = 0;
    ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    SetLastError(0xdeadbeef);
    ret = LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov);
    show("byte 0, locked by the other handle, fail immediately", ret, &ov);
    drain("port");

    printf("has to wait:\n");
    memset(&ov, 0x55, sizeof(ov));
    ov.Offset = 0; ov.OffsetHigh = 0;
    ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    SetLastError(0xdeadbeef);
    ret = LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &ov);
    show("byte 0, locked by the other handle", ret, &ov);
    drain("port, before the other unlocks");
    thread = CreateThread(NULL, 0, unlock_later, other, 0, NULL);
    printf("  waiting for the event: %lu\n", WaitForSingleObject(ov.hEvent, 5000));
    show("after", TRUE, &ov);
    WaitForSingleObject(thread, 5000);
    drain("port");
    UnlockFile(file, 0, 0, 1, 0);

    printf("granted at once, skipping the port on success:\n");
    printf("  SetFileCompletionNotificationModes: %d\n",
           SetFileCompletionNotificationModes(file, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS));
    memset(&ov, 0x55, sizeof(ov));
    ov.Offset = 0; ov.OffsetHigh = 0;
    ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    SetLastError(0xdeadbeef);
    ret = LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &ov);
    show("exclusive lock of byte 0", ret, &ov);
    drain("port");
    UnlockFile(file, 0, 0, 1, 0);

    printf("refused at once, skipping the port on success:\n");
    LockFile(other, 0, 0, 1, 0);
    memset(&ov, 0x55, sizeof(ov));
    ov.Offset = 0; ov.OffsetHigh = 0;
    ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    SetLastError(0xdeadbeef);
    ret = LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov);
    show("byte 0, locked by the other handle, fail immediately", ret, &ov);
    drain("port");

    printf("has to wait, skipping the port on success:\n");
    memset(&ov, 0x55, sizeof(ov));
    ov.Offset = 0; ov.OffsetHigh = 0;
    ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    SetLastError(0xdeadbeef);
    ret = LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &ov);
    show("byte 0, locked by the other handle", ret, &ov);
    thread = CreateThread(NULL, 0, unlock_later, other, 0, NULL);
    printf("  waiting for the event: %lu\n", WaitForSingleObject(ov.hEvent, 5000));
    show("after", TRUE, &ov);
    WaitForSingleObject(thread, 5000);
    drain("port");
    UnlockFile(file, 0, 0, 1, 0);

    printf("a handle without overlapped I/O:\n");
    sync = open_file(FALSE);
    memset(&ov, 0x55, sizeof(ov));
    ov.Offset = 2; ov.OffsetHigh = 0;
    ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    SetLastError(0xdeadbeef);
    ret = LockFileEx(sync, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &ov);
    show("byte 2, granted at once", ret, &ov);
    LockFile(other, 4, 0, 1, 0);
    memset(&ov, 0x55, sizeof(ov));
    ov.Offset = 4; ov.OffsetHigh = 0;
    ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    thread = CreateThread(NULL, 0, unlock_later4, other, 0, NULL);
    SetLastError(0xdeadbeef);
    ret = LockFileEx(sync, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &ov);
    show("byte 4, locked by the other handle until it unlocks", ret, &ov);
    WaitForSingleObject(thread, 5000);

    printf("  byte 4 again, locked by the other handle, fail immediately:\n");
    LockFile(other, 4, 0, 1, 0);
    memset(&ov, 0x55, sizeof(ov));
    ov.Offset = 4; ov.OffsetHigh = 0;
    ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    SetLastError(0xdeadbeef);
    ret = LockFileEx(sync, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov);
    show("byte 4", ret, &ov);
    UnlockFile(other, 4, 0, 1, 0);
    CloseHandle(sync);

    printf("a waiting lock cancelled:\n");
    LockFile(other, 6, 0, 1, 0);
    memset(&ov, 0x55, sizeof(ov));
    ov.Offset = 6; ov.OffsetHigh = 0;
    ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    SetLastError(0xdeadbeef);
    ret = LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &ov);
    show("byte 6, locked by the other handle", ret, &ov);
    SetLastError(0xdeadbeef);
    ret = CancelIoEx(file, &ov);
    printf("  CancelIoEx: %d error %lu\n", ret, ret ? 0 : GetLastError());
    printf("  waiting for the event: %lu\n", WaitForSingleObject(ov.hEvent, 2000));
    show("after", TRUE, &ov);
    drain("port");
    printf("  the other handle unlocks byte 6: %d\n", UnlockFile(other, 6, 0, 1, 0));
    SetLastError(0xdeadbeef);
    ret = LockFileEx(other, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov);
    printf("  the other handle can lock byte 6 again: %d\n", ret);
    UnlockFile(other, 6, 0, 1, 0);

    printf("a waiting lock whose handle is closed:\n");
    LockFile(other, 8, 0, 1, 0);
    memset(&ov, 0x55, sizeof(ov));
    ov.Offset = 8; ov.OffsetHigh = 0;
    ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    SetLastError(0xdeadbeef);
    ret = LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &ov);
    show("byte 8, locked by the other handle", ret, &ov);
    CloseHandle(file);
    printf("  waiting for the event: %lu\n", WaitForSingleObject(ov.hEvent, 2000));
    show("after closing", TRUE, &ov);
    drain("port");
    printf("  the other handle unlocks byte 8: %d\n", UnlockFile(other, 8, 0, 1, 0));

    printf("NtLockFile without a status block:\n");
    pNtLockFile = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtLockFile");
    offset.QuadPart = 9;
    length.QuadPart = 1;
    status = pNtLockFile(other, NULL, NULL, NULL, NULL, &offset, &length, NULL, TRUE, TRUE);
    printf("  byte 9 on the other handle: status %s\n", status_name((ULONG)status));
    third = open_file(FALSE);
    memset(&ov, 0, sizeof(ov));
    ov.Offset = 9;
    ret = LockFileEx(third, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov);
    printf("  a third handle can lock byte 9: %d\n", ret);
    if (ret) UnlockFile(third, 9, 0, 1, 0);
    else printf("  the other handle unlocks byte 9: %d\n", UnlockFile(other, 9, 0, 1, 0));
    CloseHandle(third);

    CloseHandle(other);
    CloseHandle(port);
    DeleteFileA(path);
    printf("done\n");
    return 0;
}
