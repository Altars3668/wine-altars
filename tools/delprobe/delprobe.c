/* delprobe -- 关闭句柄之后，删除是否立刻可行？
 *
 * C2R 的 integrator 用 GENERIC_READ/FILE_SHARE_READ 反复读 C2RManifest*.xml，
 * 然后删掉它们。在 Wine 下这一步以 ERROR_SHARING_VIOLATION 失败。跟踪里看不到
 * 句柄泄漏，所以要问的是：Wine 的关闭对删除是不是同步可见的。
 *
 * Build:
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -municode -o delprobe.exe delprobe.c
 */
#include <windows.h>
#include <stdio.h>

static void out( const char *s ) { DWORD w; WriteFile( GetStdHandle(STD_OUTPUT_HANDLE), s, (DWORD)strlen(s), &w, NULL ); }
static void outf( const char *f, ... ) { char b[512]; va_list a; va_start(a,f); vsnprintf(b,sizeof(b),f,a); va_end(a); out(b); }

static BOOL mk( const WCHAR *src, WCHAR *dst )
{
    WCHAR dir[MAX_PATH];
    return GetTempPathW( MAX_PATH, dir ) && GetTempFileNameW( dir, L"dlp", 0, dst ) && CopyFileW( src, dst, FALSE );
}

static void report( const WCHAR *p, const char *label )
{
    if (DeleteFileW( p )) { outf( "  %-34s 删除成功\n", label ); return; }
    outf( "  %-34s 失败 %lu%s\n", label, GetLastError(),
          GetLastError() == ERROR_SHARING_VIOLATION ? " (SHARING_VIOLATION)" : "" );
    DeleteFileW( p );
}

int wmain( int argc, WCHAR **argv )
{
    WCHAR p[MAX_PATH];
    HANDLE h, map;
    void *view;
    int i;

    if (argc < 2) { out( "usage: delprobe <file>\n" ); return 2; }

    if (mk( argv[1], p )) { report( p, "对照：从未打开" ); }

    if (mk( argv[1], p ))
    {
        h = CreateFileW( p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
        CloseHandle( h );
        report( p, "打开(SHARE_READ)+关闭 后删" );
    }

    if (mk( argv[1], p ))
    {
        for (i = 0; i < 6; i++)
        {
            h = CreateFileW( p, GENERIC_READ, i & 1 ? FILE_SHARE_READ | FILE_SHARE_WRITE : FILE_SHARE_READ,
                             NULL, OPEN_EXISTING, 0, NULL );
            CloseHandle( h );
        }
        report( p, "整型 6 次开关（照 integrator）后删" );
    }

    if (mk( argv[1], p ))
    {
        h = CreateFileW( p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
        map = CreateFileMappingW( h, NULL, PAGE_READONLY, 0, 0, NULL );
        view = MapViewOfFile( map, FILE_MAP_READ, 0, 0, 0 );
        UnmapViewOfFile( view );
        CloseHandle( map );
        CloseHandle( h );
        report( p, "映射+解映射+关闭 后删" );
    }

    if (mk( argv[1], p ))
    {
        h = CreateFileW( p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
        report( p, "句柄仍打开时删（应失败）" );
        CloseHandle( h );
        DeleteFileW( p );
    }

    out( "done\n" );
    return 0;
}
