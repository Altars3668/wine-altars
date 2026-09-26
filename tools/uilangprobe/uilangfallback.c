/* uilangfallback: how the thread's preferred UI language list is expanded with neutral and
 * fallback languages, language by language, and what the installed UI languages say of their
 * fallbacks.  A companion of uilangprobe.
 */
#include <windows.h>
#include <stdio.h>

static void print_multi( const WCHAR *buf, ULONG size )
{
    const WCHAR *p = buf;

    printf( " \"" );
    while (p < buf + size && *p)
    {
        printf( "%s%ls", p == buf ? "" : "|", p );
        p += wcslen( p ) + 1;
    }
    printf( "\"" );
}

static void thread_list( DWORD flags )
{
    WCHAR buf[512];
    ULONG count = 0xdead, size = ARRAY_SIZE(buf);
    DWORD err;
    BOOL ret;

    SetLastError( 0xdeadbeef );
    ret = GetThreadPreferredUILanguages( flags, &count, buf, &size );
    err = GetLastError();
    printf( "  %#lx: %d error %lu count %lu", flags, ret, err, count );
    if (ret) print_multi( buf, size );
    printf( "\n" );
}

static void with_thread( const WCHAR *list )
{
    ULONG count = 0xdead;
    DWORD err;
    BOOL ret;

    SetLastError( 0xdeadbeef );
    ret = SetThreadPreferredUILanguages( MUI_LANGUAGE_NAME, list, &count );
    err = GetLastError();
    printf( "thread" );
    print_multi( list, 64 );
    printf( ": %d error %lu count %lu, GetThreadUILanguage %04x\n", ret, err, count, GetThreadUILanguage() );
    if (!ret) return;
    thread_list( MUI_LANGUAGE_NAME );
    thread_list( MUI_LANGUAGE_NAME | MUI_MERGE_SYSTEM_FALLBACK );
    thread_list( MUI_LANGUAGE_NAME | MUI_MERGE_USER_FALLBACK );
    thread_list( MUI_LANGUAGE_NAME | MUI_MERGE_SYSTEM_FALLBACK | MUI_MERGE_USER_FALLBACK );
    SetThreadPreferredUILanguages( 0, NULL, NULL );
}

static void print_key( const WCHAR *path )
{
    WCHAR name[256];
    BYTE data[512];
    DWORD i, type, len, size;
    HKEY key;

    if (RegOpenKeyExW( HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &key ))
    {
        printf( "reg %ls: cannot open\n", path );
        return;
    }
    for (i = 0; ; i++)
    {
        len = ARRAY_SIZE(name);
        size = sizeof(data) - sizeof(WCHAR);
        memset( data, 0, sizeof(data) );
        if (RegEnumValueW( key, i, name, &len, NULL, &type, data, &size )) break;
        printf( "reg %ls\\%ls type %lu", path, name, type );
        if (type == REG_MULTI_SZ) print_multi( (WCHAR *)data, size / sizeof(WCHAR) );
        else if (type == REG_SZ || type == REG_EXPAND_SZ) printf( " \"%ls\"", (WCHAR *)data );
        else if (type == REG_DWORD) printf( " %#lx", *(DWORD *)data );
        printf( "\n" );
    }
    RegCloseKey( key );
}

int main( void )
{
    static const WCHAR *lists[] =
    {
        L"zh-CN\0", L"en-US\0", L"zh-TW\0", L"zh-HK\0", L"zh-SG\0", L"zh-MO\0", L"zh-Hans\0", L"zh-Hant\0", L"zh\0",
        L"en-GB\0", L"pt-BR\0", L"pt-PT\0", L"es-MX\0", L"de-CH\0", L"fr-CA\0", L"ja-JP\0", L"ko-KR\0",
        L"sr-Latn-RS\0", L"sr-Cyrl-RS\0", L"uz-Latn-UZ\0", L"az-Cyrl-AZ\0", L"bs-Cyrl-BA\0", L"iu-Latn-CA\0",
        L"mn-MN\0", L"ha-Latn-NG\0", L"nb-NO\0", L"nn-NO\0", L"no\0", L"en-US\0zh-CN\0", L"de\0de-DE\0",
        L"ca-ES-valencia\0", L"en-IN\0", L"es-419\0", L"x-IV\0", L"zh-Hans-CN\0", L"en-Latn-US\0",
    };
    unsigned int i;

    print_key( L"SYSTEM\\CurrentControlSet\\Control\\MUI\\UILanguages\\zh-CN" );
    print_key( L"SYSTEM\\CurrentControlSet\\Control\\MUI\\UILanguages\\en-US" );
    print_key( L"SYSTEM\\CurrentControlSet\\Control\\MUI\\Settings" );
    print_key( L"SYSTEM\\CurrentControlSet\\Control\\MUI\\Settings\\LanguageConfiguration" );
    print_key( L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\LanguagePack\\InstalledLanguages" );
    printf( "nothing set:\n" );
    thread_list( MUI_LANGUAGE_NAME );
    thread_list( MUI_LANGUAGE_NAME | MUI_MERGE_SYSTEM_FALLBACK );
    for (i = 0; i < ARRAY_SIZE(lists); i++) with_thread( lists[i] );
    printf( "done\n" );
    return 0;
}
