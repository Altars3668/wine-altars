/*
 * boolstrprobe: what oleaut32 turns a boolean into, as a string, for each locale and flag: VarBstrFromBool,
 * VariantChangeTypeEx and VarCat, printed as UTF-8.
 */
#include <windows.h>
#include <oleauto.h>
#include <stdio.h>

static void print(const char *what, HRESULT hr, BSTR str)
{
    char buf[256];

    if (FAILED(hr))
    {
        printf("%s: hr %#lx\n", what, hr);
        return;
    }
    WideCharToMultiByte(CP_UTF8, 0, str, -1, buf, sizeof(buf), NULL, NULL);
    printf("%s: \"%s\"\n", what, buf);
}

int main(void)
{
    static const struct { LCID lcid; const char *name; } locales[] =
    {
        { MAKELCID(0x0804, SORT_DEFAULT), "zh-CN" },
        { MAKELCID(0x0404, SORT_DEFAULT), "zh-TW" },
        { MAKELCID(0x0409, SORT_DEFAULT), "en-US" },
        { MAKELCID(0x0407, SORT_DEFAULT), "de-DE" },
        { MAKELCID(0x040c, SORT_DEFAULT), "fr-FR" },
        { MAKELCID(0x0411, SORT_DEFAULT), "ja-JP" },
        { MAKELCID(0x0419, SORT_DEFAULT), "ru-RU" },
        { LOCALE_USER_DEFAULT, "user" },
        { LOCALE_SYSTEM_DEFAULT, "system" },
        { 0, "0" },
    };
    static const struct { ULONG flags; const char *name; } flags[] =
    {
        { 0, "0" },
        { VARIANT_LOCALBOOL, "LOCALBOOL" },
        { VARIANT_ALPHABOOL, "ALPHABOOL" },
        { VARIANT_ALPHABOOL | VARIANT_LOCALBOOL, "ALPHA|LOCAL" },
    };
    unsigned int i, j, k;
    char what[128];
    VARIANT v, dst, l, r;
    HRESULT hr;
    BSTR str;

    printf("user default lcid %#lx, ui language %#x\n", GetUserDefaultLCID(), GetUserDefaultUILanguage());
    for (i = 0; i < ARRAY_SIZE(locales); i++)
    {
        for (j = 0; j < ARRAY_SIZE(flags); j++)
        {
            for (k = 0; k < 2; k++)
            {
                str = NULL;
                hr = VarBstrFromBool(k ? VARIANT_TRUE : VARIANT_FALSE, locales[i].lcid, flags[j].flags, &str);
                sprintf(what, "VarBstrFromBool(%s, %s, %s)", k ? "TRUE" : "FALSE", locales[i].name, flags[j].name);
                print(what, hr, str);
                SysFreeString(str);
            }
            V_VT(&v) = VT_BOOL;
            V_BOOL(&v) = VARIANT_TRUE;
            VariantInit(&dst);
            hr = VariantChangeTypeEx(&dst, &v, locales[i].lcid, flags[j].flags, VT_BSTR);
            sprintf(what, "VariantChangeTypeEx(TRUE, %s, %s)", locales[i].name, flags[j].name);
            print(what, hr, V_VT(&dst) == VT_BSTR ? V_BSTR(&dst) : NULL);
            VariantClear(&dst);
        }
    }
    V_VT(&l) = VT_BSTR;
    V_BSTR(&l) = SysAllocString(L"x");
    V_VT(&r) = VT_BOOL;
    V_BOOL(&r) = VARIANT_TRUE;
    VariantInit(&dst);
    hr = VarCat(&l, &r, &dst);
    print("VarCat(\"x\", TRUE)", hr, V_VT(&dst) == VT_BSTR ? V_BSTR(&dst) : NULL);
    VariantClear(&dst);
    VariantClear(&l);
    return 0;
}
