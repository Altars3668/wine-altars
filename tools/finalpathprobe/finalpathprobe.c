/* finalpathprobe: what a program learns about the real location of a path, which on Wine may go through
 * a symbolic link to the host (the user's Documents, Desktop...): GetFinalPathNameByHandle with each volume
 * name format, GetVolumePathName, and the file's name as NtQueryObject and FileNameInformation give it.
 * Usage: finalpathprobe <path>...   Prints paths only. */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static void show( const char *what, const WCHAR *str, int len )
{
    char buf[2048];
    int n = WideCharToMultiByte( CP_UTF8, 0, str, len, buf, sizeof(buf) - 1, NULL, NULL );
    buf[n > 0 ? n : 0] = 0;
    printf( "  %-28s %s\n", what, buf );
}

int wmain( int argc, WCHAR **argv )
{
    static const struct { DWORD flags; const char *name; } formats[] =
    {
        { FILE_NAME_NORMALIZED | VOLUME_NAME_DOS,  "final DOS" },
        { FILE_NAME_NORMALIZED | VOLUME_NAME_GUID, "final GUID" },
        { FILE_NAME_NORMALIZED | VOLUME_NAME_NT,   "final NT" },
        { FILE_NAME_NORMALIZED | VOLUME_NAME_NONE, "final none" },
        { FILE_NAME_OPENED | VOLUME_NAME_DOS,      "opened DOS" },
        { FILE_NAME_OPENED | VOLUME_NAME_NONE,     "opened none" },
    };
    SetConsoleOutputCP( CP_UTF8 );
    for (int i = 1; i < argc; i++)
    {
        WCHAR buf[1024];
        BYTE info[4096];
        IO_STATUS_BLOCK io;
        ULONG len;
        DWORD ret;
        HANDLE file;

        show( "path", argv[i], -1 );
        if (GetVolumePathNameW( argv[i], buf, ARRAY_SIZE(buf) )) show( "GetVolumePathName", buf, -1 );
        else printf( "  %-28s error %lu\n", "GetVolumePathName", GetLastError() );
        file = CreateFileW( argv[i], 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                            FILE_FLAG_BACKUP_SEMANTICS, NULL );
        if (file == INVALID_HANDLE_VALUE)
        {
            printf( "  open error %lu\n", GetLastError() );
            continue;
        }
        for (int f = 0; f < ARRAY_SIZE(formats); f++)
        {
            if ((ret = GetFinalPathNameByHandleW( file, buf, ARRAY_SIZE(buf), formats[f].flags )) && ret < ARRAY_SIZE(buf))
                show( formats[f].name, buf, -1 );
            else printf( "  %-28s error %lu\n", formats[f].name, GetLastError() );
        }
        if (!NtQueryObject( file, ObjectNameInformation, info, sizeof(info), &len ))
        {
            UNICODE_STRING *name = (UNICODE_STRING *)info;
            show( "NtQueryObject name", name->Buffer, name->Length / sizeof(WCHAR) );
        }
        if (!NtQueryInformationFile( file, &io, info, sizeof(info), 9 /* FileNameInformation */ ))
        {
            FILE_NAME_INFORMATION *name = (FILE_NAME_INFORMATION *)info;
            show( "FileNameInformation", name->FileName, name->FileNameLength / sizeof(WCHAR) );
        }
        CloseHandle( file );
    }
    return 0;
}
