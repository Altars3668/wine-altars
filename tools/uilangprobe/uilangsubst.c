/* uilangsubst: where the languages a filter puts in for those it takes out go in the merged
 * lists, and what GetThreadUILanguage makes of them.
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
    BOOL ret;

    ret = GetThreadPreferredUILanguages( flags, &count, buf, &size );
    printf( "  %#lx: %d count %lu", flags, ret, count );
    if (ret) print_multi( buf, size );
    printf( "\n" );
}

static void run( const WCHAR *list, DWORD filter )
{
    SetThreadPreferredUILanguages( MUI_RESET_FILTERS, NULL, NULL );
    SetThreadPreferredUILanguages( MUI_LANGUAGE_NAME, list, NULL );
    SetThreadPreferredUILanguages( filter, NULL, NULL );
    printf( "filter %#lx", filter );
    print_multi( list, 64 );
    printf( ": GetThreadUILanguage %04x\n", GetThreadUILanguage() );
    thread_list( MUI_LANGUAGE_NAME | MUI_THREAD_LANGUAGES );
    thread_list( MUI_LANGUAGE_NAME );
    thread_list( MUI_LANGUAGE_NAME | MUI_MERGE_SYSTEM_FALLBACK );
    thread_list( MUI_LANGUAGE_NAME | MUI_MERGE_SYSTEM_FALLBACK | MUI_MERGE_USER_FALLBACK );
    thread_list( MUI_LANGUAGE_ID | MUI_MERGE_SYSTEM_FALLBACK | MUI_MERGE_USER_FALLBACK );
}

int main( void )
{
    run( L"ar-SA\0he-IL\0", MUI_COMPLEX_SCRIPT_FILTER );
    run( L"ar-SA\0de-DE\0", MUI_COMPLEX_SCRIPT_FILTER );
    run( L"zh\0de-DE\0", MUI_COMPLEX_SCRIPT_FILTER );
    run( L"ja-JP\0fr-FR\0", MUI_CONSOLE_FILTER );
    run( L"sr\0no\0mn\0zh-Hant\0zh-Hans\0", MUI_COMPLEX_SCRIPT_FILTER );
    run( L"sr\0no\0mn\0zh-Hant\0zh-Hans\0", MUI_CONSOLE_FILTER );
    SetThreadPreferredUILanguages( MUI_RESET_FILTERS, NULL, NULL );
    SetThreadPreferredUILanguages( 0, NULL, NULL );
    printf( "done\n" );
    return 0;
}
