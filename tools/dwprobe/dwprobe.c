/*
 * dwprobe - ask DirectWrite what fonts it can actually see.
 *
 * Office renders its whole UI through Direct2D's DrawTextLayout, and when that
 * fails the window is simply blank -- no error reaches the screen, and GDI
 * tracing shows almost nothing because Office never draws text through GDI.
 * Wine's failure path ends in layout_run_get_last_resort_font, which gives up
 * if it cannot match "Tahoma" in the *system* collection. That makes "is
 * Tahoma there" the question, and asking DirectWrite directly separates a
 * broken font collection from Office asking for something unusual.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <dwrite.h>
#include <stdio.h>
#include <shellapi.h>

static void report(IDWriteFontCollection *coll, const WCHAR *name)
{
    UINT32 index = 0;
    BOOL exists = FALSE;
    HRESULT hr = IDWriteFontCollection_FindFamilyName(coll, name, &index, &exists);
    printf("  FindFamilyName(%ls) -> hr %#lx, exists %d, index %u\n", name, hr, exists, index);
    if (exists) {
        IDWriteFontFamily *fam = NULL;
        if (SUCCEEDED(IDWriteFontCollection_GetFontFamily(coll, index, &fam))) {
            IDWriteFont *font = NULL;
            hr = IDWriteFontFamily_GetFirstMatchingFont(fam, DWRITE_FONT_WEIGHT_NORMAL,
                     DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font);
            printf("      GetFirstMatchingFont -> hr %#lx\n", hr);
            if (SUCCEEDED(hr)) {
                IDWriteFontFace *face = NULL;
                hr = IDWriteFont_CreateFontFace(font, &face);
                printf("      CreateFontFace       -> hr %#lx\n", hr);
                if (face) IDWriteFontFace_Release(face);
                IDWriteFont_Release(font);
            }
            IDWriteFontFamily_Release(fam);
        }
    }
}

int main(void)
{
    IDWriteFactory *factory = NULL;
    IDWriteFontCollection *coll = NULL;
    HRESULT hr;
    UINT32 count, i, shown = 0;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory, (IUnknown **)&factory);
    printf("DWriteCreateFactory -> %#lx\n", hr);
    if (FAILED(hr)) return 1;

    hr = IDWriteFactory_GetSystemFontCollection(factory, &coll, FALSE);
    printf("GetSystemFontCollection -> %#lx\n", hr);
    if (FAILED(hr) || !coll) return 2;

    count = IDWriteFontCollection_GetFontFamilyCount(coll);
    printf("font families visible to DirectWrite: %u\n", count);

    for (i = 0; i < count && shown < 12; i++) {
        IDWriteFontFamily *fam = NULL;
        IDWriteLocalizedStrings *names = NULL;
        WCHAR buf[128];
        if (FAILED(IDWriteFontCollection_GetFontFamily(coll, i, &fam))) continue;
        if (SUCCEEDED(IDWriteFontFamily_GetFamilyNames(fam, &names))) {
            if (SUCCEEDED(IDWriteLocalizedStrings_GetString(names, 0, buf, ARRAYSIZE(buf))))
                printf("    [%u] %ls\n", i, buf), shown++;
            IDWriteLocalizedStrings_Release(names);
        }
        IDWriteFontFamily_Release(fam);
    }

    report(coll, L"Tahoma");
    report(coll, L"Arial");
    report(coll, L"Segoe UI");
    report(coll, L"微软雅黑");   /* Microsoft YaHei */

    /* The UI variants.  Office asks DirectWrite for exactly one family per
     * start-up and on a zh-CN install it is "Microsoft YaHei UI" -- a family
     * distinct from "Microsoft YaHei", carried in the same .ttc.  Segoe UI has
     * the same pattern on en-US.  A family that is present under one name and
     * missing under the other is invisible until something asks by name. */
    report(coll, L"Microsoft YaHei");
    report(coll, L"Microsoft YaHei UI");
    report(coll, L"Segoe UI Variable");

    /* Anything named on the command line, read wide so CJK survives argv. */
    {
        int argc = 0;
        WCHAR **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        int n;
        for (n = 1; argv && n < argc; ++n) report(coll, argv[n]);
    }

    IDWriteFontCollection_Release(coll);
    IDWriteFactory_Release(factory);
    return 0;
}
