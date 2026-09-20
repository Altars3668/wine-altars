/* offregprobe -- can OffReg read a real registry hive under Wine?
 *
 * Office's Click-to-Run installs the App-V virtual registry as fifteen
 * root\vreg\*.vreg.dat files.  A +file trace of the install shows the same
 * thread loading offreg.dll, reading a staged hive whole (524288 bytes for
 * dcf.x-none), and then deleting that file and writing 8192 bytes back -- an
 * empty hive.  This runs the same library against a hive by hand so the step
 * that loses the contents can be seen directly.
 *
 * Build:
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -municode \
 *       -o offregprobe.exe offregprobe.c
 *
 * Usage:  offregprobe.exe <hive.dat> [save-to.dat]
 */

#include <windows.h>
#include <stdio.h>

typedef struct _ORHKEY *ORHKEY;
typedef ORHKEY *PORHKEY;

typedef DWORD (WINAPI *fn_OROpenHive)( PCWSTR, PORHKEY );
typedef DWORD (WINAPI *fn_ORCloseHive)( ORHKEY );
typedef DWORD (WINAPI *fn_ORSaveHive)( ORHKEY, PCWSTR, DWORD, DWORD );
typedef DWORD (WINAPI *fn_OROpenKey)( ORHKEY, PCWSTR, PORHKEY );
typedef DWORD (WINAPI *fn_ORCloseKey)( ORHKEY );
typedef DWORD (WINAPI *fn_OREnumKey)( ORHKEY, DWORD, PWSTR, PDWORD, PWSTR, PDWORD, PFILETIME );
typedef DWORD (WINAPI *fn_ORQueryInfoKey)( ORHKEY, PWSTR, PDWORD, PDWORD, PDWORD, PDWORD,
                                           PDWORD, PDWORD, PDWORD, PDWORD, PFILETIME );
typedef DWORD (WINAPI *fn_ORGetVersion)( PDWORD, PDWORD );

static void out( const char *s )
{
    DWORD w;
    WriteFile( GetStdHandle( STD_OUTPUT_HANDLE ), s, (DWORD)strlen( s ), &w, NULL );
}

static void outf( const char *fmt, ... )
{
    char buf[1024];
    va_list ap;
    va_start( ap, fmt );
    vsnprintf( buf, sizeof(buf), fmt, ap );
    va_end( ap );
    out( buf );
}

static const char *u8( const WCHAR *w )
{
    static char buf[512];
    WideCharToMultiByte( CP_UTF8, 0, w, -1, buf, sizeof(buf), NULL, NULL );
    return buf;
}

static LONGLONG file_size( const WCHAR *path )
{
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW( path, GetFileExInfoStandard, &d )) return -1;
    return ((LONGLONG)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}

int wmain( int argc, WCHAR **argv )
{
    fn_ORQueryInfoKey pORQueryInfoKey;
    fn_ORGetVersion pORGetVersion;
    fn_ORCloseHive pORCloseHive;
    fn_OROpenHive pOROpenHive;
    fn_ORSaveHive pORSaveHive;
    fn_OREnumKey pOREnumKey;
    fn_OROpenKey pOROpenKey;
    fn_ORCloseKey pORCloseKey;
    DWORD subkeys = 0, values = 0, maj = 0, min = 0, i, err;
    ORHKEY hive = NULL, key = NULL;
    HMODULE mod;

    if (argc < 2) { out( "usage: offregprobe <hive.dat> [save-to.dat]\n" ); return 2; }

    if (!(mod = LoadLibraryW( L"offreg.dll" )))
    {
        outf( "LoadLibrary offreg.dll failed: %lu\n", GetLastError() );
        return 1;
    }
    outf( "offreg.dll                   : loaded at %p\n", mod );

#define GET(x) do { p##x = (fn_##x)(void *)GetProcAddress( mod, #x ); \
    if (!p##x) { outf( "  missing export %s\n", #x ); return 1; } } while (0)
    GET(OROpenHive); GET(ORCloseHive); GET(ORSaveHive);
    GET(OROpenKey); GET(ORCloseKey); GET(OREnumKey);
    GET(ORQueryInfoKey); GET(ORGetVersion);
#undef GET

    if (!(err = pORGetVersion( &maj, &min )))
        outf( "ORGetVersion                 : %lu.%lu\n", maj, min );
    else
        outf( "ORGetVersion                 : error %lu\n", err );

    outf( "hive                         : %s (%lld bytes)\n", u8( argv[1] ), file_size( argv[1] ) );

    err = pOROpenHive( argv[1], &hive );
    outf( "OROpenHive                   : %lu%s hive=%p\n", err, err ? "  <-- FAILED" : "", hive );
    if (err) return 1;

    err = pORQueryInfoKey( hive, NULL, NULL, &subkeys, NULL, NULL, &values, NULL, NULL, NULL, NULL );
    outf( "ORQueryInfoKey(root)         : %lu  subkeys=%lu values=%lu\n", err, subkeys, values );

    for (i = 0; i < subkeys && i < 8; i++)
    {
        WCHAR name[256];
        DWORD len = ARRAYSIZE(name);
        err = pOREnumKey( hive, i, name, &len, NULL, NULL, NULL );
        if (err) { outf( "  OREnumKey(%lu) error %lu\n", i, err ); break; }
        outf( "  [%lu] %s\n", i, u8( name ) );
    }

    /* The path the virtual registry actually lives under. */
    err = pOROpenKey( hive, L"REGISTRY\\MACHINE\\Software\\Classes", &key );
    outf( "OROpenKey REGISTRY\\MACHINE\\Software\\Classes : %lu\n", err );
    if (!err)
    {
        subkeys = 0;
        pORQueryInfoKey( key, NULL, NULL, &subkeys, NULL, NULL, &values, NULL, NULL, NULL, NULL );
        outf( "    subkeys=%lu values=%lu\n", subkeys, values );
        for (i = 0; i < subkeys && i < 6; i++)
        {
            WCHAR name[256];
            DWORD len = ARRAYSIZE(name);
            if (pOREnumKey( key, i, name, &len, NULL, NULL, NULL )) break;
            outf( "      %s\n", u8( name ) );
        }
        pORCloseKey( key );
    }

    if (argc > 2)
    {
        DeleteFileW( argv[2] );
        err = pORSaveHive( hive, argv[2], 6, 1 );
        outf( "ORSaveHive -> %s            : %lu, %lld bytes\n", u8( argv[2] ), err, file_size( argv[2] ) );
    }

    pORCloseHive( hive );
    out( "done\n" );
    return 0;
}
