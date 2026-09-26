/* uilanginfo: the locale data behind the UI language lists and their filters: parent, console
 * fallback name, scripts, code pages and reading layout of each language uilangscripts sets.
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
    L"zh-Hans", L"zh-Hant", L"sr-Latn", L"sr", L"nb", L"no", L"mn-Cyrl", L"mn", L"ca-ES-valencia", L"ca-ES",
    L"es-419", L"es-ES_tradnl", L"de-DE_phoneb", L"en-AT", L"iu-Latn-CA", L"vi",
};

int main( void )
{
    WCHAR parent[LOCALE_NAME_MAX_LENGTH], console[LOCALE_NAME_MAX_LENGTH], scripts[64], layout[8];
    WCHAR acp[8], oemcp[8], lcid_name[LOCALE_NAME_MAX_LENGTH];
    unsigned int i;
    LCID lcid;

    for (i = 0; i < ARRAY_SIZE(langs); i++)
    {
        parent[0] = console[0] = scripts[0] = layout[0] = acp[0] = oemcp[0] = lcid_name[0] = 0;
        GetLocaleInfoEx( langs[i], LOCALE_SPARENT, parent, ARRAY_SIZE(parent) );
        GetLocaleInfoEx( langs[i], LOCALE_SCONSOLEFALLBACKNAME, console, ARRAY_SIZE(console) );
        GetLocaleInfoEx( langs[i], LOCALE_SSCRIPTS, scripts, ARRAY_SIZE(scripts) );
        GetLocaleInfoEx( langs[i], LOCALE_IREADINGLAYOUT, layout, ARRAY_SIZE(layout) );
        GetLocaleInfoEx( langs[i], LOCALE_IDEFAULTANSICODEPAGE, acp, ARRAY_SIZE(acp) );
        GetLocaleInfoEx( langs[i], LOCALE_IDEFAULTCODEPAGE, oemcp, ARRAY_SIZE(oemcp) );
        lcid = LocaleNameToLCID( langs[i], LOCALE_ALLOW_NEUTRAL_NAMES );
        LCIDToLocaleName( LANGIDFROMLCID( lcid ), lcid_name, ARRAY_SIZE(lcid_name), LOCALE_ALLOW_NEUTRAL_NAMES );
        printf( "%-14ls lcid %05lx (%ls) parent %-8ls console %-8ls scripts %-12ls layout %ls acp %-5ls oemcp %ls\n",
                langs[i], lcid, lcid_name, parent, console, scripts, layout, acp, oemcp );
        {
            LOCALESIGNATURE sig;
            memset( &sig, 0, sizeof(sig) );
            GetLocaleInfoEx( langs[i], LOCALE_FONTSIGNATURE, (WCHAR *)&sig, sizeof(sig) / sizeof(WCHAR) );
            printf( "%-14ls usb %08lx %08lx %08lx %08lx csb %08lx %08lx / %08lx %08lx\n", langs[i],
                    sig.lsUsb[0], sig.lsUsb[1], sig.lsUsb[2], sig.lsUsb[3],
                    sig.lsCsbDefault[0], sig.lsCsbDefault[1], sig.lsCsbSupported[0], sig.lsCsbSupported[1] );
        }
    }
    return 0;
}
