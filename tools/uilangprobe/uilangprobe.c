/* uilangprobe: what the preferred UI language functions answer, and what they are made of.
 *
 * Word asks GetUserPreferredUILanguages and GetSystemPreferredUILanguages for names, in pairs,
 * as it picks the language of its resources.  This prints every list with its count and size,
 * for each flag, before and after the thread and the process set lists of their own, and the
 * registry values and installed UI languages the lists come from.
 */
#include <windows.h>
#include <stdio.h>

typedef BOOL (WINAPI *get_func)( DWORD, ULONG *, WCHAR *, ULONG * );

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

static void get_list( const char *name, get_func func, DWORD flags )
{
    WCHAR buf[256];
    ULONG count, size;
    DWORD err;
    BOOL ret;

    count = 0xdead;
    size = 0;
    SetLastError( 0xdeadbeef );
    ret = func( flags, &count, NULL, &size );
    err = GetLastError();
    printf( "%s(%#lx) query: %d error %lu count %#lx size %lu\n", name, flags, ret, err, count, size );

    count = 0xdead;
    size = ARRAY_SIZE(buf);
    memset( buf, 0xcc, sizeof(buf) );
    SetLastError( 0xdeadbeef );
    ret = func( flags, &count, buf, &size );
    err = GetLastError();
    printf( "%s(%#lx): %d error %lu count %#lx size %lu", name, flags, ret, err, count, size );
    if (ret) print_multi( buf, size );
    printf( "\n" );
}

static void get_small( const char *name, get_func func, DWORD flags )
{
    WCHAR buf[256];
    ULONG count, size;
    DWORD err;
    BOOL ret;

    count = 0xdead;
    size = 2;
    memset( buf, 0xcc, sizeof(buf) );
    SetLastError( 0xdeadbeef );
    ret = func( flags, &count, buf, &size );
    err = GetLastError();
    printf( "%s(%#lx) size 2: %d error %lu count %#lx size %lu buf[0] %#x\n", name, flags, ret, err, count, size, buf[0] );

    count = 0xdead;
    size = 0;
    SetLastError( 0xdeadbeef );
    ret = func( flags, &count, buf, &size );
    err = GetLastError();
    printf( "%s(%#lx) buffer, size 0: %d error %lu count %#lx size %lu\n", name, flags, ret, err, count, size );
}

static BOOL WINAPI get_user( DWORD flags, ULONG *count, WCHAR *buf, ULONG *size ) { return GetUserPreferredUILanguages( flags, count, buf, size ); }
static BOOL WINAPI get_system( DWORD flags, ULONG *count, WCHAR *buf, ULONG *size ) { return GetSystemPreferredUILanguages( flags, count, buf, size ); }
static BOOL WINAPI get_thread( DWORD flags, ULONG *count, WCHAR *buf, ULONG *size ) { return GetThreadPreferredUILanguages( flags, count, buf, size ); }
static BOOL WINAPI get_process( DWORD flags, ULONG *count, WCHAR *buf, ULONG *size ) { return GetProcessPreferredUILanguages( flags, count, buf, size ); }

static const DWORD thread_flags[] =
{
    0, MUI_LANGUAGE_NAME, MUI_LANGUAGE_ID, MUI_LANGUAGE_NAME | MUI_LANGUAGE_ID,
    MUI_LANGUAGE_NAME | MUI_MERGE_USER_FALLBACK, MUI_LANGUAGE_NAME | MUI_MERGE_SYSTEM_FALLBACK,
    MUI_LANGUAGE_NAME | MUI_MERGE_SYSTEM_FALLBACK | MUI_MERGE_USER_FALLBACK,
    MUI_LANGUAGE_ID | MUI_MERGE_SYSTEM_FALLBACK | MUI_MERGE_USER_FALLBACK,
    MUI_LANGUAGE_NAME | MUI_THREAD_LANGUAGES,
    MUI_LANGUAGE_NAME | MUI_THREAD_LANGUAGES | MUI_MERGE_USER_FALLBACK,
    MUI_LANGUAGE_NAME | MUI_CONSOLE_FILTER, MUI_LANGUAGE_NAME | MUI_COMPLEX_SCRIPT_FILTER,
    MUI_LANGUAGE_NAME | MUI_RESET_FILTERS, MUI_LANGUAGE_NAME | 0x80, MUI_LANGUAGE_NAME | 0x400,
};

static void thread_lists( const char *when )
{
    unsigned int i;

    printf( "-- thread, %s: GetThreadUILanguage %04x\n", when, GetThreadUILanguage() );
    for (i = 0; i < ARRAY_SIZE(thread_flags); i++) get_list( "thread", get_thread, thread_flags[i] );
    get_list( "process", get_process, MUI_LANGUAGE_NAME );
}

static void set_thread( DWORD flags, const WCHAR *list, BOOL with_count )
{
    ULONG count = 0xdead;
    DWORD err;
    BOOL ret;

    SetLastError( 0xdeadbeef );
    ret = SetThreadPreferredUILanguages( flags, list, with_count ? &count : NULL );
    err = GetLastError();
    printf( "SetThreadPreferredUILanguages(%#lx", flags );
    if (list) print_multi( list, 64 );
    else printf( " NULL" );
    printf( "): %d error %lu count %#lx\n", ret, err, count );
}

static void set_process( DWORD flags, const WCHAR *list, BOOL with_count )
{
    ULONG count = 0xdead;
    DWORD err;
    BOOL ret;

    SetLastError( 0xdeadbeef );
    ret = SetProcessPreferredUILanguages( flags, list, with_count ? &count : NULL );
    err = GetLastError();
    printf( "SetProcessPreferredUILanguages(%#lx", flags );
    if (list) print_multi( list, 64 );
    else printf( " NULL" );
    printf( "): %d error %lu count %#lx\n", ret, err, count );
}

static void print_value( HKEY root, const WCHAR *path, const WCHAR *name )
{
    WCHAR buf[512];
    DWORD type, size = sizeof(buf);
    LSTATUS status;

    memset( buf, 0, sizeof(buf) );
    status = RegGetValueW( root, path, name, RRF_RT_ANY | RRF_NOEXPAND, &type, buf, &size );
    printf( "reg %ls\\%ls: %ld", path, name, status );
    if (!status)
    {
        printf( " type %lu", type );
        if (type == REG_MULTI_SZ) print_multi( buf, size / sizeof(WCHAR) );
        else if (type == REG_SZ || type == REG_EXPAND_SZ) printf( " \"%ls\"", buf );
        else if (type == REG_DWORD) printf( " %#lx", *(DWORD *)buf );
    }
    printf( "\n" );
}

static void print_subkeys( HKEY root, const WCHAR *path )
{
    WCHAR name[256];
    DWORD len, i;
    HKEY key;

    if (RegOpenKeyExW( root, path, 0, KEY_READ, &key ))
    {
        printf( "reg %ls: cannot open\n", path );
        return;
    }
    printf( "reg %ls subkeys:", path );
    for (i = 0; len = ARRAY_SIZE(name), !RegEnumKeyExW( key, i, name, &len, NULL, NULL, NULL, NULL ); i++)
        printf( " %ls", name );
    printf( "\n" );
    RegCloseKey( key );
}

static BOOL CALLBACK enum_ui_proc( WCHAR *name, LONG_PTR param )
{
    printf( " %ls", name );
    return TRUE;
}

int main( void )
{
    static const DWORD user_flags[] = { 0, MUI_LANGUAGE_NAME, MUI_LANGUAGE_ID, MUI_LANGUAGE_NAME | MUI_LANGUAGE_ID,
                                        MUI_LANGUAGE_NAME | 0x10, MUI_LANGUAGE_NAME | 0x400 };
    static const DWORD system_flags[] = { 0, MUI_LANGUAGE_NAME, MUI_LANGUAGE_ID, MUI_LANGUAGE_NAME | MUI_LANGUAGE_ID,
                                          MUI_LANGUAGE_NAME | MUI_MACHINE_LANGUAGE_SETTINGS,
                                          MUI_LANGUAGE_ID | MUI_MACHINE_LANGUAGE_SETTINGS, MUI_LANGUAGE_NAME | 0x10 };
    static const DWORD process_flags[] = { 0, MUI_LANGUAGE_NAME, MUI_LANGUAGE_ID, MUI_LANGUAGE_NAME | MUI_LANGUAGE_ID,
                                           MUI_LANGUAGE_NAME | 0x10 };
    WCHAR locale[LOCALE_NAME_MAX_LENGTH];
    unsigned int i;
    DWORD err;
    BOOL ret;

    printf( "GetUserDefaultUILanguage %04x GetSystemDefaultUILanguage %04x GetThreadUILanguage %04x\n",
            GetUserDefaultUILanguage(), GetSystemDefaultUILanguage(), GetThreadUILanguage() );
    GetUserDefaultLocaleName( locale, ARRAY_SIZE(locale) );
    printf( "GetUserDefaultLocaleName %ls", locale );
    GetSystemDefaultLocaleName( locale, ARRAY_SIZE(locale) );
    printf( " GetSystemDefaultLocaleName %ls\n", locale );

    print_value( HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"PreferredUILanguages" );
    print_value( HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"PreferredUILanguagesPending" );
    print_value( HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"PreviousPreferredUILanguages" );
    print_value( HKEY_CURRENT_USER, L"Control Panel\\Desktop\\MuiCached", L"MachinePreferredUILanguages" );
    print_value( HKEY_CURRENT_USER, L"Control Panel\\International\\User Profile", L"Languages" );
    print_value( HKEY_CURRENT_USER, L"Control Panel\\International", L"LocaleName" );
    print_value( HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\MUI\\Settings", L"PreferredUILanguages" );
    print_value( HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Nls\\Language", L"InstallLanguage" );
    print_value( HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Nls\\Language", L"Default" );
    print_subkeys( HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\MUI\\UILanguages" );

    printf( "EnumUILanguages(NAME):" );
    SetLastError( 0xdeadbeef );
    ret = EnumUILanguagesW( enum_ui_proc, MUI_LANGUAGE_NAME, 0 );
    err = GetLastError();
    printf( " -> %d error %lu\n", ret, err );

    for (i = 0; i < ARRAY_SIZE(user_flags); i++) get_list( "user", get_user, user_flags[i] );
    get_small( "user", get_user, MUI_LANGUAGE_NAME );
    for (i = 0; i < ARRAY_SIZE(system_flags); i++) get_list( "system", get_system, system_flags[i] );
    get_small( "system", get_system, MUI_LANGUAGE_NAME );
    for (i = 0; i < ARRAY_SIZE(process_flags); i++) get_list( "process", get_process, process_flags[i] );
    get_small( "thread", get_thread, MUI_LANGUAGE_NAME );
    thread_lists( "nothing set" );

    set_thread( MUI_LANGUAGE_NAME, L"en-US\0fr-FR\0", TRUE );
    thread_lists( "en-US|fr-FR set" );
    set_thread( MUI_LANGUAGE_ID, L"0407\0", TRUE );
    thread_lists( "0407 set" );
    set_thread( MUI_LANGUAGE_NAME, L"xx-XX\0", TRUE );
    set_thread( MUI_LANGUAGE_NAME, L"de\0", TRUE );
    get_list( "thread", get_thread, MUI_LANGUAGE_NAME | MUI_THREAD_LANGUAGES );
    set_thread( MUI_LANGUAGE_NAME, L"en-US\0fr-FR\0de-DE\0es-ES\0it-IT\0ja-JP\0", TRUE );
    get_list( "thread", get_thread, MUI_LANGUAGE_NAME | MUI_THREAD_LANGUAGES );
    set_thread( MUI_LANGUAGE_NAME | MUI_LANGUAGE_ID, L"en-US\0", TRUE );
    set_thread( MUI_LANGUAGE_NAME, L"en-US\0", FALSE );
    set_thread( 0, L"en-US\0", TRUE );
    set_thread( MUI_LANGUAGE_NAME, L"\0", TRUE );
    get_list( "thread", get_thread, MUI_LANGUAGE_NAME | MUI_THREAD_LANGUAGES );
    set_thread( MUI_LANGUAGE_NAME, L"en-US\0", TRUE );
    set_thread( 0, NULL, TRUE );
    get_list( "thread", get_thread, MUI_LANGUAGE_NAME | MUI_THREAD_LANGUAGES );
    set_thread( MUI_LANGUAGE_NAME, L"en-US\0", TRUE );
    set_thread( MUI_RESET_FILTERS, NULL, TRUE );
    get_list( "thread", get_thread, MUI_LANGUAGE_NAME | MUI_THREAD_LANGUAGES );
    set_thread( MUI_LANGUAGE_NAME, NULL, TRUE );
    get_list( "thread", get_thread, MUI_LANGUAGE_NAME | MUI_THREAD_LANGUAGES );
    set_thread( MUI_CONSOLE_FILTER, NULL, TRUE );
    set_thread( MUI_COMPLEX_SCRIPT_FILTER, NULL, TRUE );
    set_thread( 0x80, NULL, TRUE );
    thread_lists( "after resets" );

    set_process( MUI_LANGUAGE_NAME, L"de-DE\0", TRUE );
    for (i = 0; i < ARRAY_SIZE(process_flags); i++) get_list( "process", get_process, process_flags[i] );
    thread_lists( "process de-DE set" );
    set_process( MUI_LANGUAGE_NAME, L"it-IT\0ja-JP\0", TRUE );
    get_list( "process", get_process, MUI_LANGUAGE_NAME );
    set_process( MUI_LANGUAGE_NAME, L"xx-XX\0", TRUE );
    set_process( MUI_LANGUAGE_ID, L"040c\0", TRUE );
    get_list( "process", get_process, MUI_LANGUAGE_NAME );
    set_process( 0, L"en-US\0", TRUE );
    set_process( MUI_LANGUAGE_NAME, L"en-US\0", FALSE );
    set_process( MUI_LANGUAGE_NAME, NULL, TRUE );
    get_list( "process", get_process, MUI_LANGUAGE_NAME );
    set_process( 0, NULL, TRUE );
    get_list( "process", get_process, MUI_LANGUAGE_NAME );
    set_process( MUI_LANGUAGE_NAME, L"en-US\0fr-FR\0de-DE\0es-ES\0it-IT\0ja-JP\0", TRUE );
    get_list( "process", get_process, MUI_LANGUAGE_NAME );
    set_thread( MUI_LANGUAGE_NAME, L"fr-FR\0", TRUE );
    thread_lists( "thread fr-FR, process six" );
    printf( "done\n" );
    return 0;
}
