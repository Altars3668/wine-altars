/*
 * languageprobe - what Windows.Globalization.Language's statics answer.
 *
 * Word asks the Language factory for ILanguageStatics, which Wine did not
 * have.  This asks IsWellFormed of tags of every shape, the current input
 * method's language tag against the keyboard layout, TrySetInputMethodLanguageTag,
 * and the new names of ILanguage3.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <roapi.h>
#include <winstring.h>
#include <stdio.h>

DEFINE_GUID(IID_IActivationFactory_, 0x00000035, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
DEFINE_GUID(IID_ILanguageStatics_, 0xb23cd557, 0x0865, 0x46d4, 0x89, 0xb8, 0xd5, 0x9b, 0xe8, 0x99, 0x0f, 0x0d);
DEFINE_GUID(IID_ILanguageStatics2_, 0x30199f6e, 0x914b, 0x4b2a, 0x9d, 0x6e, 0xe3, 0xb0, 0xe2, 0x7d, 0xbe, 0x4f);
DEFINE_GUID(IID_ILanguageFactory_, 0x9b0252ac, 0x0c27, 0x44f8, 0xb7, 0x92, 0x97, 0x93, 0xfb, 0x66, 0xc6, 0x3e);
DEFINE_GUID(IID_ILanguage3_, 0xc6af3d10, 0x641a, 0x5ba4, 0xbb, 0x43, 0x5e, 0x12, 0xae, 0xd7, 0x59, 0x54);
DEFINE_GUID(IID_ILanguageExtensionSubtags_, 0x7d7daf45, 0x368d, 0x4364, 0x85, 0x2b, 0xde, 0xc9, 0x27, 0x03, 0x7b, 0x85);

typedef struct { void *vtbl; } obj;
typedef struct
{
    HRESULT (WINAPI *QueryInterface)(void *, REFIID, void **);
    ULONG (WINAPI *AddRef)(void *);
    ULONG (WINAPI *Release)(void *);
    HRESULT (WINAPI *GetIids)(void *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(void *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(void *, int *);
    HRESULT (WINAPI *m1)(void *, void *, void *);
    HRESULT (WINAPI *m2)(void *, void *, void *);
} vt;

#define CALL(o, n, ...) (((vt *)((obj *)(o))->vtbl)->n((o), __VA_ARGS__))

static const WCHAR *tags[] =
{
    L"en-US", L"en", L"EN-us", L"zh-Hans-CN", L"zh-hans-cn", L"sr-Latn", L"de-DE-1996", L"en-US-u-ca-gregory",
    L"en-US-x-private", L"x-private", L"i-klingon", L"zh-min-nan", L"en_US", L"", L"e", L"abcdefghi", L"abcd",
    L"abcde", L"123", L"en--US", L"en-", L"-en", L"en-US-", L"en-Latn-Latn", L"en-USA", L"en-419", L"qaa",
    L"art-lojban", L"en-a-bbb-a-ccc", L"en-US-US", L"de-DE-1901-1901",
};

int main(void)
{
    WCHAR buf[128];
    obj *factory, *statics, *statics2, *langfactory, *lang, *lang3;
    HSTRING name, str;
    unsigned int i;
    BOOLEAN b;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    RoInitialize(RO_INIT_MULTITHREADED);
    WindowsCreateString(L"Windows.Globalization.Language", 30, &name);
    hr = RoGetActivationFactory(name, &IID_IActivationFactory_, (void **)&factory);
    printf("factory %#lx\n", hr);
    hr = CALL(factory, QueryInterface, &IID_ILanguageStatics_, (void **)&statics);
    printf("ILanguageStatics %#lx\n", hr);
    for (i = 0; i < ARRAYSIZE(tags); i++)
    {
        HSTRING tag;
        WindowsCreateString(tags[i], wcslen(tags[i]), &tag);
        b = 2;
        hr = ((HRESULT (WINAPI *)(void *, HSTRING, BOOLEAN *))((vt *)statics->vtbl)->m1)(statics, tag, &b);
        printf("  IsWellFormed(\"%ls\") %#lx %d\n", tags[i], hr, b);
        WindowsDeleteString(tag);
    }
    b = 2;
    hr = ((HRESULT (WINAPI *)(void *, HSTRING, BOOLEAN *))((vt *)statics->vtbl)->m1)(statics, NULL, &b);
    printf("  IsWellFormed(NULL) %#lx %d\n", hr, b);
    str = NULL;
    hr = ((HRESULT (WINAPI *)(void *, HSTRING *))((vt *)statics->vtbl)->m2)(statics, &str);
    printf("  CurrentInputMethodLanguageTag %#lx \"%ls\"\n", hr, WindowsGetStringRawBuffer(str, NULL));
    WindowsDeleteString(str);
    LCIDToLocaleName(MAKELCID(LOWORD(GetKeyboardLayout(0)), SORT_DEFAULT), buf, ARRAYSIZE(buf), 0);
    printf("  keyboard layout %p, its locale \"%ls\"\n", GetKeyboardLayout(0), buf);

    hr = CALL(factory, QueryInterface, &IID_ILanguageStatics2_, (void **)&statics2);
    printf("ILanguageStatics2 %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        HSTRING tag;
        WindowsCreateString(L"xx-YY", 5, &tag);
        b = 2;
        hr = ((HRESULT (WINAPI *)(void *, HSTRING, BOOLEAN *))((vt *)statics2->vtbl)->m1)(statics2, tag, &b);
        printf("  TrySetInputMethodLanguageTag(\"xx-YY\") %#lx %d\n", hr, b);
        WindowsDeleteString(tag);
    }

    hr = CALL(factory, QueryInterface, &IID_ILanguageFactory_, (void **)&langfactory);
    if (SUCCEEDED(hr))
    {
        static const WCHAR *names[] = { L"en-US", L"zh-Hans-CN", L"de", L"sr-Latn-RS" };
        for (i = 0; i < ARRAYSIZE(names); i++)
        {
            HSTRING tag;
            WindowsCreateString(names[i], wcslen(names[i]), &tag);
            lang = NULL;
            hr = ((HRESULT (WINAPI *)(void *, HSTRING, void **))((vt *)langfactory->vtbl)->m1)(langfactory, tag, (void **)&lang);
            printf("CreateLanguage(\"%ls\") %#lx\n", names[i], hr);
            if (SUCCEEDED(hr))
            {
                hr = CALL(lang, QueryInterface, &IID_ILanguage3_, (void **)&lang3);
                printf("  ILanguage3 %#lx", hr);
                if (SUCCEEDED(hr))
                {
                    str = NULL;
                    hr = ((HRESULT (WINAPI *)(void *, HSTRING *))((vt *)lang3->vtbl)->m1)(lang3, &str);
                    printf(", AbbreviatedName %#lx length %u", hr, WindowsGetStringLen(str));
                    WindowsDeleteString(str);
                    ((vt *)lang3->vtbl)->Release(lang3);
                }
                hr = CALL(lang, QueryInterface, &IID_ILanguageExtensionSubtags_, (void **)&lang3);
                printf(", ILanguageExtensionSubtags %#lx\n", hr);
                if (SUCCEEDED(hr)) ((vt *)lang3->vtbl)->Release(lang3);
                ((vt *)lang->vtbl)->Release(lang);
            }
            WindowsDeleteString(tag);
        }
    }
    printf("done\n");
    return 0;
}
