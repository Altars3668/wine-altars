/* statustext: what wmiutils' IWbemStatusCodeText says of codes of every facility, of WMI codes it has no message
 * for, of success codes, and in English, in the user's language and by default; non-ASCII as \uXXXX.  Prints results
 * only. */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <wbemcli.h>
#include <stdio.h>

DEFINE_GUID(CLSID_WbemStatusCodeText_, 0xeb87e1bd, 0x3233, 0x11d2, 0xae, 0xc9, 0x00, 0xc0, 0x4f, 0xb6, 0x88, 0x20);
DEFINE_GUID(IID_IWbemStatusCodeText_, 0xeb87e1bc, 0x3233, 0x11d2, 0xae, 0xc9, 0x00, 0xc0, 0x4f, 0xb6, 0x88, 0x20);

static void print_w(const WCHAR *str)
{
    if (!str)
    {
        printf("(null)");
        return;
    }
    for (; *str; str++)
    {
        if (*str == '\r') printf("\\r");
        else if (*str == '\n') printf("\\n");
        else if (*str >= 0x20 && *str < 0x7f) printf("%c", (char)*str);
        else printf("\\u%04x", *str);
    }
}

int main(void)
{
    static const HRESULT codes[] =
    {
        0, 1, 0x40005, 0x40006, 0x80041001, 0x80041002, 0x80041089, 0x8004108a, 0x80041fff, 0x80042001, 0x80043001,
        0x80044001, 0x80044010, 0x8004d000, 0x80070002, 0x80070005, 0x80070057, 0x8007ffff, 0x80010105, 0x80020009,
        0x80030002, 0x80040154, 0x80080005, 0x80090308, 0x800a01a8, 0x800b0109, 0x800c0005, 0x80004005, 0x8000ffff,
        0xc0000005, 0x12345678,
    };
    static const LCID lcids[] = { 0, 0x409, 0x804, 0x407 };
    IWbemStatusCodeText *text;
    unsigned int i, j;
    HRESULT hr;
    BSTR str;

    setvbuf(stdout, NULL, _IONBF, 0);
    CoInitialize(NULL);
    hr = CoCreateInstance(&CLSID_WbemStatusCodeText_, NULL, CLSCTX_INPROC_SERVER, &IID_IWbemStatusCodeText_,
                          (void **)&text);
    if (hr != S_OK) { printf("WbemStatusCodeText %#lx\n", hr); return 1; }
    for (i = 0; i < ARRAYSIZE(codes); i++)
    {
        str = NULL;
        hr = IWbemStatusCodeText_GetErrorCodeText(text, codes[i], 0, 0, &str);
        printf("%#010lx: %#lx [", codes[i], hr);
        print_w(str);
        SysFreeString(str);
        str = NULL;
        hr = IWbemStatusCodeText_GetFacilityCodeText(text, codes[i], 0, 0, &str);
        printf("] facility %#lx [", hr);
        print_w(str);
        printf("]\n");
        SysFreeString(str);
    }
    for (j = 0; j < ARRAYSIZE(lcids); j++)
    {
        str = NULL;
        hr = IWbemStatusCodeText_GetErrorCodeText(text, 0x80041002, lcids[j], 0, &str);
        printf("lcid %#lx: %#lx [", lcids[j], hr);
        print_w(str);
        printf("]\n");
        SysFreeString(str);
    }
    str = NULL;
    hr = IWbemStatusCodeText_GetErrorCodeText(text, 0x80041002, 0, 1, &str);
    printf("flags 1: %#lx [", hr);
    print_w(str);
    printf("]\n");
    SysFreeString(str);
    IWbemStatusCodeText_Release(text);
    CoUninitialize();
    return 0;
}
