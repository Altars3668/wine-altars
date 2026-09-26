/* uilangscripts: which languages SetThreadPreferredUILanguages' console and complex script
 * filters keep, five at a time (a thread keeps at most five), with the console's code pages.
 */
#include <windows.h>
#include <stdio.h>

static const WCHAR *langs[] =
{
    L"ar-SA", L"he-IL", L"th-TH", L"hi-IN", L"en-US", L"ru-RU", L"el-GR", L"uk-UA", L"vi-VN", L"tr-TR",
    L"pl-PL", L"zh-TW", L"zh-HK", L"ka-GE", L"hy-AM", L"ta-IN", L"bn-IN", L"lo-LA", L"bo-CN", L"my-MM",
    L"si-LK", L"mn-MN", L"mn-Mong-CN", L"ug-CN", L"fa-IR", L"ur-PK", L"dv-MV", L"syr-SY", L"ti-ER",
    L"iu-Cans-CA", L"chr-Cher-US", L"ii-CN", L"am-ET", L"km-KH", L"ko-KR", L"ja-JP", L"de-DE", L"fr-FR",
    L"zh-CN", L"sr-Cyrl-RS", L"bg-BG", L"kk-KZ", L"he", L"ar", L"zh-SG", L"cs-CZ", L"hu-HU", L"ro-RO",
    L"lt-LT", L"lv-LV", L"et-EE", L"is-IS", L"mt-MT", L"cy-GB", L"ga-IE", L"eu-ES", L"af-ZA", L"sw-KE",
    L"yo-NG", L"ha-Latn-NG", L"az-Latn-AZ", L"az-Cyrl-AZ", L"tt-RU", L"ba-RU", L"ky-KG", L"tg-Cyrl-TJ",
    L"pa-IN", L"gu-IN", L"or-IN", L"te-IN", L"kn-IN", L"ml-IN", L"mr-IN", L"ne-NP", L"sa-IN", L"as-IN",
    L"ps-AF", L"ku-Arab-IQ", L"sd-Arab-PK", L"pa-Arab-PK", L"tzm-Latn-DZ", L"tzm-Tfng-MA", L"zh", L"en",
};

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

static void thread_only( const char *what )
{
    WCHAR buf[512];
    ULONG count = 0xdead, size = ARRAY_SIZE(buf);
    BOOL ret;

    ret = GetThreadPreferredUILanguages( MUI_LANGUAGE_NAME | MUI_THREAD_LANGUAGES, &count, buf, &size );
    printf( "  %s: %d count %lu", what, ret, count );
    if (ret) print_multi( buf, size );
    printf( ", GetThreadUILanguage %04x\n", GetThreadUILanguage() );
}

int main( void )
{
    WCHAR list[128];
    unsigned int i, j, pos;

    printf( "console code pages: input %u output %u, ACP %u OEMCP %u\n", GetConsoleCP(), GetConsoleOutputCP(), GetACP(), GetOEMCP() );
    for (i = 0; i < ARRAY_SIZE(langs); i += 5)
    {
        for (j = i, pos = 0; j < i + 5 && j < ARRAY_SIZE(langs); j++)
        {
            wcscpy( list + pos, langs[j] );
            pos += wcslen( langs[j] ) + 1;
        }
        list[pos] = 0;
        SetThreadPreferredUILanguages( MUI_RESET_FILTERS, NULL, NULL );
        if (!SetThreadPreferredUILanguages( MUI_LANGUAGE_NAME, list, NULL ))
        {
            printf( "set" );
            print_multi( list, pos + 1 );
            printf( " failed, error %lu\n", GetLastError() );
            continue;
        }
        printf( "set" );
        print_multi( list, pos + 1 );
        printf( "\n" );
        thread_only( "no filter" );
        SetThreadPreferredUILanguages( MUI_CONSOLE_FILTER, NULL, NULL );
        thread_only( "console  " );
        SetThreadPreferredUILanguages( MUI_RESET_FILTERS, NULL, NULL );
        SetThreadPreferredUILanguages( MUI_COMPLEX_SCRIPT_FILTER, NULL, NULL );
        thread_only( "complex  " );
        SetThreadPreferredUILanguages( MUI_CONSOLE_FILTER, NULL, NULL );
        thread_only( "both     " );
    }
    SetThreadPreferredUILanguages( MUI_RESET_FILTERS, NULL, NULL );
    SetThreadPreferredUILanguages( 0, NULL, NULL );
    printf( "done\n" );
    return 0;
}
