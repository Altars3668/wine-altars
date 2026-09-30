/* uilangfilter: the thread list's console and complex script filters, the flags left over from
 * uilangprobe, and how SetThreadUILanguage and the preferred lists meet.
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
    printf( "  thread %#lx: %d error %lu count %#lx", flags, ret, err, count );
    if (ret) print_multi( buf, size );
    printf( "\n" );
}

static void set_thread( DWORD flags, const WCHAR *list )
{
    ULONG count = 0xdead;
    DWORD err;
    BOOL ret;

    SetLastError( 0xdeadbeef );
    ret = SetThreadPreferredUILanguages( flags, list, &count );
    err = GetLastError();
    printf( "SetThreadPreferredUILanguages(%#lx", flags );
    if (list) print_multi( list, 128 );
    else printf( " NULL" );
    printf( "): %d error %lu count %#lx, GetThreadUILanguage %04x\n", ret, err, count, GetThreadUILanguage() );
}

static void process_small( ULONG size )
{
    WCHAR buf[64];
    ULONG count = 0xdead;
    DWORD err;
    BOOL ret;

    memset( buf, 0xcc, sizeof(buf) );
    SetLastError( 0xdeadbeef );
    ret = GetProcessPreferredUILanguages( MUI_LANGUAGE_NAME, &count, buf, &size );
    err = GetLastError();
    printf( "  process size in: %d error %lu count %#lx size %lu buf %#x %#x %#x\n", ret, err, count, size, buf[0], buf[1], buf[2] );
}

int main( void )
{
    LANGID lang;
    DWORD error;

    set_thread( MUI_LANGUAGE_NAME, L"ar-SA\0he-IL\0th-TH\0hi-IN\0en-US\0" );
    thread_list( MUI_LANGUAGE_NAME );
    thread_list( MUI_LANGUAGE_NAME | MUI_CONSOLE_FILTER );
    thread_list( MUI_LANGUAGE_NAME | MUI_COMPLEX_SCRIPT_FILTER );
    thread_list( MUI_LANGUAGE_NAME | MUI_THREAD_LANGUAGES | MUI_CONSOLE_FILTER );
    thread_list( MUI_LANGUAGE_NAME | MUI_THREAD_LANGUAGES | MUI_COMPLEX_SCRIPT_FILTER );
    thread_list( MUI_LANGUAGE_NAME | MUI_THREAD_LANGUAGES | MUI_MERGE_SYSTEM_FALLBACK );
    set_thread( MUI_LANGUAGE_NAME, L"ja-JP\0ko-KR\0am-ET\0km-KH\0en-US\0" );
    thread_list( MUI_LANGUAGE_NAME | MUI_CONSOLE_FILTER );
    thread_list( MUI_LANGUAGE_NAME | MUI_COMPLEX_SCRIPT_FILTER );
    set_thread( MUI_CONSOLE_FILTER, NULL );
    thread_list( MUI_LANGUAGE_NAME );
    set_thread( MUI_RESET_FILTERS, NULL );
    thread_list( MUI_LANGUAGE_NAME );
    set_thread( MUI_COMPLEX_SCRIPT_FILTER, NULL );
    thread_list( MUI_LANGUAGE_NAME );
    set_thread( MUI_RESET_FILTERS, NULL );
    set_thread( MUI_LANGUAGE_NAME | MUI_CONSOLE_FILTER, L"ar-SA\0en-GB\0" );
    thread_list( MUI_LANGUAGE_NAME );
    thread_list( MUI_LANGUAGE_NAME | MUI_THREAD_LANGUAGES );
    set_thread( MUI_RESET_FILTERS, NULL );
    set_thread( MUI_LANGUAGE_NAME, L"en-US\0en-US\0fr-FR\0" );
    thread_list( MUI_LANGUAGE_NAME | MUI_THREAD_LANGUAGES );
    set_thread( MUI_LANGUAGE_NAME, L"en-us\0FR-fr\0zh-cn\0" );
    thread_list( MUI_LANGUAGE_NAME | MUI_THREAD_LANGUAGES );
    set_thread( MUI_LANGUAGE_ID, L"0409\0040c\0" );
    thread_list( MUI_LANGUAGE_ID | MUI_THREAD_LANGUAGES );
    set_thread( MUI_LANGUAGE_ID, L"409\0" );
    set_thread( MUI_LANGUAGE_ID, L"00409\0" );
    set_thread( MUI_LANGUAGE_ID, L"0x0409\0" );
    set_thread( MUI_LANGUAGE_ID, L"en-US\0" );
    set_thread( MUI_LANGUAGE_NAME, L"0409\0" );
    set_thread( MUI_LANGUAGE_NAME, L"en-US\0xx-XX\0" );
    thread_list( MUI_LANGUAGE_NAME | MUI_THREAD_LANGUAGES );
    set_thread( MUI_LANGUAGE_NAME, L"es-ES_tradnl\0de-DE_phoneb\0" );
    thread_list( MUI_LANGUAGE_NAME | MUI_THREAD_LANGUAGES );
    set_thread( MUI_LANGUAGE_NAME, L"\0en-US\0" );
    set_thread( 0, NULL );
    thread_list( MUI_LANGUAGE_NAME | MUI_THREAD_LANGUAGES | MUI_MERGE_SYSTEM_FALLBACK );
    thread_list( MUI_LANGUAGE_NAME | MUI_THREAD_LANGUAGES | 0x1 );
    thread_list( MUI_LANGUAGE_ID | MUI_UI_FALLBACK | MUI_THREAD_LANGUAGES );

    printf( "SetThreadUILanguage:\n" );
    lang = SetThreadUILanguage( MAKELANGID( LANG_GERMAN, SUBLANG_GERMAN ) );
    printf( "  SetThreadUILanguage(0407) %04x, GetThreadUILanguage %04x\n", lang, GetThreadUILanguage() );
    thread_list( MUI_LANGUAGE_NAME | MUI_THREAD_LANGUAGES );
    thread_list( MUI_LANGUAGE_NAME );
    lang = SetThreadUILanguage( 0 );
    printf( "  SetThreadUILanguage(0) %04x, GetThreadUILanguage %04x\n", lang, GetThreadUILanguage() );
    thread_list( MUI_LANGUAGE_NAME | MUI_THREAD_LANGUAGES );
    set_thread( MUI_LANGUAGE_NAME, L"fr-FR\0it-IT\0" );
    lang = SetThreadUILanguage( 0 );
    printf( "  SetThreadUILanguage(0) %04x, GetThreadUILanguage %04x\n", lang, GetThreadUILanguage() );
    thread_list( MUI_LANGUAGE_NAME | MUI_THREAD_LANGUAGES );
    lang = SetThreadUILanguage( 0x7804 );
    printf( "  SetThreadUILanguage(7804) %04x, GetThreadUILanguage %04x\n", lang, GetThreadUILanguage() );
    thread_list( MUI_LANGUAGE_NAME | MUI_THREAD_LANGUAGES );
    lang = SetThreadUILanguage( 0x1234 );
    error = GetLastError();
    printf( "  SetThreadUILanguage(1234) %04x, GetThreadUILanguage %04x error %lu\n", lang, GetThreadUILanguage(), error );
    set_thread( 0, NULL );

    printf( "process, empty:\n" );
    process_small( 0 );
    process_small( 1 );
    process_small( 2 );
    SetProcessPreferredUILanguages( MUI_LANGUAGE_NAME, L"de-DE\0", NULL );
    printf( "process, de-DE:\n" );
    process_small( 0 );
    process_small( 2 );
    process_small( 7 );
    SetLastError( 0xdeadbeef );
    printf( "SetProcessPreferredUILanguages(0x100 NULL): %d", SetProcessPreferredUILanguages( MUI_CONSOLE_FILTER, NULL, NULL ) );
    printf( " error %lu\n", GetLastError() );
    SetLastError( 0xdeadbeef );
    printf( "SetProcessPreferredUILanguages(0x1 NULL): %d", SetProcessPreferredUILanguages( MUI_RESET_FILTERS, NULL, NULL ) );
    printf( " error %lu\n", GetLastError() );
    SetLastError( 0xdeadbeef );
    printf( "SetProcessPreferredUILanguages(0xc en-US): %d", SetProcessPreferredUILanguages( MUI_LANGUAGE_NAME | MUI_LANGUAGE_ID, L"en-US\0", NULL ) );
    printf( " error %lu\n", GetLastError() );
    process_small( 7 );
    printf( "done\n" );
    return 0;
}
