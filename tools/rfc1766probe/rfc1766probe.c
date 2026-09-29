/* rfc1766probe - MLang's RFC 1766 names: the table IEnumRfc1766 walks, the registry copy of it
 * under HKCR\MIME\Database\Rfc1766, and what Rfc1766ToLcid, LcidToRfc1766 and the
 * IMultiLanguage methods answer for names and LCIDs in and out of that table.
 *
 * MSXML takes a language argument (ms:string-compare, ms:format-date) only when Rfc1766ToLcidW
 * answers S_OK for it.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later. */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <mlang.h>
#include <stdio.h>

static void print_ascii(const WCHAR *s)
{
    for (; s && *s; s++)
        if (*s >= 0x20 && *s < 0x7f) putchar(*s);
        else printf("\\u%04x", *s);
}

int main(void)
{
    static const WCHAR *names[] =
    {
        L"en-us", L"EN-US", L"en", L"e", L"eng", L"en-", L"-us", L"en-us-x", L"en-xx", L"en-gb", L"en-uk",
        L"de", L"de-de", L"de-at", L"de-xx", L"fr", L"fr-fr", L"fr-be", L"es", L"es-es", L"es-mx",
        L"es-us", L"pt", L"pt-br", L"pt-pt", L"zh", L"zh-cn", L"zh-tw", L"zh-hk", L"zh-sg", L"zh-mo",
        L"zh-hans", L"zh-hant", L"ja", L"ja-jp", L"ko", L"ko-kr", L"sr", L"sr-latn", L"sr-sp",
        L"no", L"nb", L"nb-no", L"nn", L"nn-no", L"he", L"iw", L"id", L"in", L"sh", L"x-default",
        L"i-klingon", L"xx", L"xx-xx", L"", L"en_us", L" en-us", L"en-us ",
        /* where the fallback to the primary language stops */
        L"en-u", L"en-usa", L"de-ch1", L"en-us1", L"kok-", L"kok-x", L"kok-in", L"syr-s", L"syr-sy",
        L"div-m", L"es-e", L"a-b", L"en--", L"en-us-", L"EN-", L"zh-xx",
    };
    static const LCID lcids[] =
    {
        0x0409, 0x0809, 0x0009, 0x0407, 0x0007, 0x040c, 0x080c, 0x040a, 0x0c0a, 0x080a, 0x0416, 0x0816,
        0x0804, 0x0404, 0x0c04, 0x1004, 0x1404, 0x0004, 0x7c04, 0x0411, 0x0412, 0x0414, 0x0814, 0x040d,
        0x0421, 0x081a, 0x0c1a, 0x0000, 0x007f, 0x0400, 0x0800, 0x1000, 0x0480, 0x0492, 0x1234,
    };
    IMultiLanguage2 *ml;
    IEnumRfc1766 *en;
    RFC1766INFO info;
    WCHAR buf[64], value[256], valname[64];
    ULONG got;
    HKEY key;
    DWORD i, count = 0;
    HRESULT hr;

    CoInitialize(NULL);
    hr = CoCreateInstance(&CLSID_CMultiLanguage, NULL, CLSCTX_INPROC_SERVER, &IID_IMultiLanguage2, (void **)&ml);
    printf("CMultiLanguage %#lx\n", hr);
    if (FAILED(hr)) return 1;

    /* the table, with its English display names */
    hr = IMultiLanguage2_EnumRfc1766(ml, 0x0409, &en);
    printf("EnumRfc1766(0409) %#lx\n", hr);
    while (SUCCEEDED(hr) && IEnumRfc1766_Next(en, 1, &info, &got) == S_OK && got)
    {
        printf("enum %04lx ", info.lcid);
        print_ascii(info.wszRfc1766);
        printf(" | ");
        print_ascii(info.wszLocaleName);
        printf("\n");
        count++;
    }
    if (SUCCEEDED(hr)) IEnumRfc1766_Release(en);
    printf("enum count %lu\n", count);

    if (!RegOpenKeyExW(HKEY_CLASSES_ROOT, L"MIME\\Database\\Rfc1766", 0, KEY_READ, &key))
    {
        for (i = 0;; i++)
        {
            DWORD nlen = ARRAY_SIZE(valname), vlen = sizeof(value), type;
            if (RegEnumValueW(key, i, valname, &nlen, NULL, &type, (BYTE *)value, &vlen)) break;
            printf("reg %ls = ", valname);
            print_ascii(value);
            printf("\n");
        }
        RegCloseKey(key);
        printf("reg count %lu\n", i);
    }
    else printf("reg: no MIME\\Database\\Rfc1766\n");

    for (i = 0; i < ARRAY_SIZE(names); i++)
    {
        LCID lcid = 0xdead;
        BSTR name = SysAllocString(names[i]);
        hr = Rfc1766ToLcidW(&lcid, names[i]);
        printf("name '%ls': Rfc1766ToLcidW %#lx %04lx", names[i], hr, lcid);
        lcid = 0xdead;
        hr = IMultiLanguage2_GetLcidFromRfc1766(ml, &lcid, name);
        printf("  GetLcidFromRfc1766 %#lx %04lx\n", hr, lcid);
        SysFreeString(name);
    }

    for (i = 0; i < ARRAY_SIZE(lcids); i++)
    {
        BSTR name = NULL;
        buf[0] = 0;
        hr = LcidToRfc1766W(lcids[i], buf, ARRAY_SIZE(buf));
        printf("lcid %04lx: LcidToRfc1766W %#lx '", lcids[i], hr);
        print_ascii(buf);
        hr = IMultiLanguage2_GetRfc1766FromLcid(ml, lcids[i], &name);
        printf("'  GetRfc1766FromLcid %#lx '", hr);
        print_ascii(name);
        printf("'\n");
        SysFreeString(name);
    }

    IMultiLanguage2_Release(ml);
    CoUninitialize();
    return 0;
}
