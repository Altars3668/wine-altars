/* colonpathprobe: which error a path gets when its drive, its device or its
 * server does not exist, before or after a component the file system would
 * refuse (a colon in the middle, "." or ".."); the source resolver of mfplat
 * tests the Win32 side with names like "::C:\..." and "\\file:\...".
 * Opens only, read-attributes access; nothing is created. */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

typedef NTSTATUS (WINAPI *open_file_t)(HANDLE *, ACCESS_MASK, OBJECT_ATTRIBUTES *, IO_STATUS_BLOCK *, ULONG, ULONG);
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static open_file_t pNtOpenFile;
static WCHAR absent;

/* "?" in a probe path stands for a drive letter that does not exist */
static void expand(const WCHAR *in, WCHAR *out)
{
    for (; *in; in++) *out++ = *in == '?' && in[1] == ':' ? absent : *in;
    *out = 0;
}

static void win32(const WCHAR *probe)
{
    WCHAR path[MAX_PATH], full[MAX_PATH];
    HANDLE file;
    DWORD len, err;

    expand(probe, path);
    len = GetFullPathNameW(path, MAX_PATH, full, NULL);
    SetLastError(0xdeadbeef);
    file = CreateFileW(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    err = GetLastError();
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    printf("CreateFileW(%ls): %s %lu; full %ls\n", probe, file == INVALID_HANDLE_VALUE ? "fails" : "opens",
           file == INVALID_HANDLE_VALUE ? err : 0, len ? full : L"(none)");
}

static void nt(const WCHAR *probe)
{
    WCHAR path[MAX_PATH], shown[MAX_PATH];
    UNICODE_STRING name;
    unsigned int i;
    OBJECT_ATTRIBUTES attr;
    IO_STATUS_BLOCK io;
    NTSTATUS status;
    HANDLE file;

    expand(probe, path);
    RtlInitUnicodeString(&name, path);
    InitializeObjectAttributes(&attr, &name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    status = pNtOpenFile(&file, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &attr, &io,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_SYNCHRONOUS_IO_NONALERT);
    if (!status) CloseHandle(file);
    for (i = 0; probe[i] && i < MAX_PATH - 1; i++) shown[i] = probe[i] < 32 ? '^' : probe[i];
    shown[i] = 0;
    printf("NtOpenFile(%ls): %#lx\n", shown, (unsigned long)status);
}

int main(void)
{
    static const WCHAR *win32_paths[] =
    {
        L"C:\\Windows\\win.ini", L"::C:\\Windows\\win.ini", L":::::C:\\Windows\\win.ini", L":C:\\Windows\\win.ini",
        L"::file://C:\\Windows\\win.ini", L":file://C:\\Windows\\win.ini", L"/file://C:\\Windows\\win.ini",
        L"::", L"::\\", L"::x", L"1:\\x", L"?:\\x", L"?:x", L"?:\\Windows:x\\win.ini", L"?:\\a\\b:c",
        L"C:\\Windows:x\\win.ini", L"C:\\nodir\\a:b\\c", L"C:\\nodir\\a:b",
        L"\\\\file:\\C:\\Windows\\win.ini", L"\\\\?\\?:\\x", L"\\\\?\\?:\\a:b\\c", L"\\\\?\\::\\x",
        L"\\\\.\\?:", L"\\\\.\\::",
    };
    static const WCHAR *nt_paths[] =
    {
        L"\\??\\C:\\Windows\\win.ini", L"\\??\\::\\C:\\Windows\\win.ini", L"\\??\\::", L"\\??\\::\\x",
        L"\\??\\?:", L"\\??\\?:\\x", L"\\??\\?:\\Windows:x\\win.ini", L"\\??\\?:\\.\\x", L"\\??\\?:\\x\\..\\y",
        L"\\??\\?:\\a\\\\b", L"\\??\\?:\\a<b\\c", L"\\??\\?:\\a<b",
        L"\\??\\C:\\Windows:x\\win.ini", L"\\??\\C:\\.\\Windows", L"\\??\\C:\\nodir\\a:b\\c", L"\\??\\C:\\nodir\\a<b\\c",
        L"\\??\\nodevice\\x", L"\\??\\nodevice\\a:b\\c", L"\\??\\nodevice",
        L"\\??\\1:\\x", L"\\??\\a\\b",
        /* on a drive that exists */
        L"\\??\\C:\\.", L"\\??\\C:\\..", L"\\??\\C:\\Windows\\.", L"\\??\\C:\\Windows\\..",
        L"\\??\\C:\\Windows\\.\\win.ini", L"\\??\\C:\\Windows\\..\\Windows", L"\\??\\C:\\nodir\\.\\x",
        L"\\??\\C:\\nodir\\..\\x", L"\\??\\C:\\Windows\\\\win.ini", L"\\??\\C:\\nodir\\\\x", L"\\??\\C:\\Windows\\",
        L"\\??\\C:\\Windows\\a<b\\c", L"\\??\\C:\\Windows\\a<b", L"\\??\\C:\\nodir\\a<b",
        L"\\??\\C:\\Windows\\a|b\\c", L"\\??\\C:\\nodir\\a|b\\c", L"\\??\\C:\\nodir\\a|b",
        L"\\??\\C:\\Windows\\a*b\\c", L"\\??\\C:\\nodir\\a*b\\c", L"\\??\\C:\\nodir\\a\"b\\c",
        L"\\??\\C:\\nodir\\a\x01z\\c", L"\\??\\C:\\Windows\\a\x01z\\c", L"\\??\\C:\\nodir\\a/b\\c",
        L"\\??\\C:\\Windows\\a/b\\c", L"\\??\\C:\\nodir\\a:b:c\\d", L"\\??\\C:\\nodir\\a::$DATA\\b",
        L"\\??\\C:\\Windows\\win.ini::$DATA", L"\\??\\C:\\Windows\\win.ini:x:$DATA",
    };
    DWORD drives;
    unsigned int i;

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    pNtOpenFile = (open_file_t)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtOpenFile");
    drives = GetLogicalDrives();
    for (absent = 'Q'; absent <= 'Y' && (drives & (1u << (absent - 'A'))); absent++) ;
    printf("a drive that does not exist: %s\n", absent <= 'Y' ? "found" : "none");
    for (i = 0; i < ARRAY_SIZE(win32_paths); i++) win32(win32_paths[i]);
    for (i = 0; i < ARRAY_SIZE(nt_paths); i++) nt(nt_paths[i]);
    return 0;
}
