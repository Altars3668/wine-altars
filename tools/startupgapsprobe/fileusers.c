/* fileusers: what NtQueryInformationFile's FileProcessIdsUsingFileInformation says -- for a file the probe holds open,
 * for one a child holds open too, for the probe's own image and a system DLL every process maps, at what buffer sizes
 * and with what access; the process ids only as counts and as "ours" or "the child's".  Prints results only. */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

#define FileProcessIdsUsingFileInformation 47
#define STATUS_BUFFER_OVERFLOW ((NTSTATUS)0x80000005)

typedef struct
{
    ULONG NumberOfProcessIdsInList;
    ULONG_PTR ProcessIdList[1];
} FILE_PROCESS_IDS_USING_FILE_INFORMATION;

static NTSTATUS (WINAPI *pNtQueryInformationFile)(HANDLE, IO_STATUS_BLOCK *, void *, ULONG, int);

static void query(const char *what, HANDLE file, DWORD child)
{
    static const ULONG sizes[] = { 0, 4, 8, 12, 16, 24, 4096 };
    BYTE buffer[4096];
    unsigned int i;

    printf("%s:", what);
    for (i = 0; i < ARRAYSIZE(sizes); i++)
    {
        FILE_PROCESS_IDS_USING_FILE_INFORMATION *info = (void *)buffer;
        IO_STATUS_BLOCK io = { { 0xdead }, 0xdead };
        NTSTATUS status;

        memset(buffer, 0xcc, sizeof(buffer));
        status = pNtQueryInformationFile(file, &io, buffer, sizes[i], FileProcessIdsUsingFileInformation);
        printf(" [%lu: %#lx info %llu", sizes[i], status, (ULONG64)io.Information);
        if (sizes[i] >= 4 && (!status || status == STATUS_BUFFER_OVERFLOW || info->NumberOfProcessIdsInList != 0xcccccccc))
        {
            ULONG n = info->NumberOfProcessIdsInList, j, ours = 0, theirs = 0;
            printf(" count %lu", n);
            for (j = 0; j < n && 8 + (j + 1) * sizeof(ULONG_PTR) <= sizes[i]; j++)
            {
                if (info->ProcessIdList[j] == GetCurrentProcessId()) ours++;
                if (child && info->ProcessIdList[j] == child) theirs++;
            }
            printf(" (ours %lu, the child's %lu)", ours, theirs);
        }
        printf("]");
    }
    printf("\n");
}

int main(int argc, char **argv)
{
    WCHAR path[MAX_PATH], temp[MAX_PATH], exe[MAX_PATH], cmdline[MAX_PATH * 2];
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    HANDLE file, file2, image;

    setvbuf(stdout, NULL, _IONBF, 0);
    pNtQueryInformationFile = (void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationFile");
    GetTempPathW(ARRAYSIZE(temp), temp);
    swprintf(path, ARRAYSIZE(path), L"%sfileusers-%lu.tmp", temp, GetCurrentProcessId());

    if (argc > 1)
    {
        /* the child: hold the file until the parent goes */
        HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, atoi(argv[1]));
        swprintf(path, ARRAYSIZE(path), L"%sfileusers-%s.tmp", temp, argv[1]);
        file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, 0, NULL);
        WaitForSingleObject(parent, 30000);
        CloseHandle(file);
        return 0;
    }

    file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                       CREATE_ALWAYS, 0, NULL);
    query("a file only the probe holds", file, 0);
    file2 = CreateFileW(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                        OPEN_EXISTING, 0, NULL);
    query("  through a handle with FILE_READ_ATTRIBUTES only", file2, 0);
    CloseHandle(file2);
    file2 = CreateFileW(path, SYNCHRONIZE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                        OPEN_EXISTING, 0, NULL);
    query("  through a handle with SYNCHRONIZE only", file2, 0);
    CloseHandle(file2);

    GetModuleFileNameW(NULL, exe, ARRAYSIZE(exe));
    swprintf(cmdline, ARRAYSIZE(cmdline), L"\"%ls\" %lu", exe, GetCurrentProcessId());
    if (CreateProcessW(exe, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        Sleep(1000);
        query("the file a child holds too", file, pi.dwProcessId);
        image = CreateFileW(exe, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                            OPEN_EXISTING, 0, NULL);
        query("the probe's image, the child running it too", image, pi.dwProcessId);
        CloseHandle(image);
    }
    image = CreateFileW(L"C:\\Windows\\System32\\kernel32.dll", FILE_READ_ATTRIBUTES,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    query("kernel32.dll", image, pi.dwProcessId);
    CloseHandle(image);
    image = CreateFileW(L"C:\\Windows\\System32", FILE_READ_ATTRIBUTES,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                        FILE_FLAG_BACKUP_SEMANTICS, NULL);
    query("the System32 directory", image, pi.dwProcessId);
    CloseHandle(image);
    CloseHandle(file);
    DeleteFileW(path);
    return 0;
}
