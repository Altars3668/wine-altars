/* Every WIC pixel format the system registers, with what IWICPixelFormatInfo2 says about it.
 *
 * One line per format, sorted by GUID: the GUID, bits per pixel, channel count, numeric
 * representation, transparency support, every channel's mask (hex, lowest byte first), then vendor,
 * author, friendly name, version and spec version.  Wine registers only part of Windows' list
 * (dlls/windowscodecs/regsvr.c); diffing this output between the two gives the missing formats and
 * the data to register them with.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror wicformatprobe.c -o wicformatprobe.exe -lole32 -luuid -lwindowscodecs
 */
#define COBJMACROS
#include <windows.h>
#include <wincodec.h>
#include <stdio.h>
#include <stdlib.h>

struct row
{
    GUID guid;
    char text[2048];
};

static int compare_rows(const void *a, const void *b)
{
    return memcmp(&((const struct row *)a)->guid, &((const struct row *)b)->guid, sizeof(GUID));
}

static void guid_text(const GUID *g, char *out)
{
    sprintf(out, "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", g->Data1, g->Data2, g->Data3,
            g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
}

static void append_string(char *text, const char *label, HRESULT (STDMETHODCALLTYPE *get)(IWICComponentInfo *, UINT, WCHAR *, UINT *),
        IWICComponentInfo *info)
{
    WCHAR buffer[256];
    char narrow[512];
    UINT len = 0;
    HRESULT hr;

    buffer[0] = 0;
    hr = get(info, ARRAYSIZE(buffer), buffer, &len);
    WideCharToMultiByte(CP_UTF8, 0, buffer, -1, narrow, sizeof(narrow), NULL, NULL);
    if (SUCCEEDED(hr))
        sprintf(text + strlen(text), " %s=\"%s\"", label, narrow);
    else
        sprintf(text + strlen(text), " %s=%#lx", label, hr);
}

static HRESULT STDMETHODCALLTYPE get_author(IWICComponentInfo *i, UINT n, WCHAR *b, UINT *l) { return IWICComponentInfo_GetAuthor(i, n, b, l); }
static HRESULT STDMETHODCALLTYPE get_name(IWICComponentInfo *i, UINT n, WCHAR *b, UINT *l) { return IWICComponentInfo_GetFriendlyName(i, n, b, l); }
static HRESULT STDMETHODCALLTYPE get_version(IWICComponentInfo *i, UINT n, WCHAR *b, UINT *l) { return IWICComponentInfo_GetVersion(i, n, b, l); }
static HRESULT STDMETHODCALLTYPE get_spec(IWICComponentInfo *i, UINT n, WCHAR *b, UINT *l) { return IWICComponentInfo_GetSpecVersion(i, n, b, l); }

int main(void)
{
    struct row *rows = calloc(512, sizeof(*rows));
    IWICImagingFactory *factory;
    IEnumUnknown *enumerator;
    IUnknown *unk;
    ULONG fetched;
    int count = 0, i;
    HRESULT hr;

    CoInitialize(NULL);
    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&factory);
    if (FAILED(hr)) { printf("CoCreateInstance: %#lx\n", hr); return 1; }
    hr = IWICImagingFactory_CreateComponentEnumerator(factory, WICPixelFormat, WICComponentEnumerateDefault, &enumerator);
    if (FAILED(hr)) { printf("CreateComponentEnumerator: %#lx\n", hr); return 1; }

    while (count < 512 && IEnumUnknown_Next(enumerator, 1, &unk, &fetched) == S_OK)
    {
        IWICPixelFormatInfo2 *info2 = NULL;
        IWICPixelFormatInfo *info;
        IWICComponentInfo *component;
        struct row *row = &rows[count];
        WICPixelFormatNumericRepresentation repr = 0;
        BOOL transparency = FALSE;
        UINT bpp = 0, channels = 0, c;
        GUID vendor = {0};
        char guid[64];
        HRESULT hr2;

        if (FAILED(IUnknown_QueryInterface(unk, &IID_IWICPixelFormatInfo, (void **)&info)))
        {
            IUnknown_Release(unk);
            continue;
        }
        IWICPixelFormatInfo_GetFormatGUID(info, &row->guid);
        IWICPixelFormatInfo_GetBitsPerPixel(info, &bpp);
        IWICPixelFormatInfo_GetChannelCount(info, &channels);
        guid_text(&row->guid, guid);
        sprintf(row->text, "%s bpp=%u channels=%u", guid, bpp, channels);
        if (SUCCEEDED(IUnknown_QueryInterface(unk, &IID_IWICPixelFormatInfo2, (void **)&info2)))
        {
            hr2 = IWICPixelFormatInfo2_GetNumericRepresentation(info2, &repr);
            sprintf(row->text + strlen(row->text), " repr=%d", SUCCEEDED(hr2) ? (int)repr : -1);
            hr2 = IWICPixelFormatInfo2_SupportsTransparency(info2, &transparency);
            sprintf(row->text + strlen(row->text), " transparency=%d", SUCCEEDED(hr2) ? transparency : -1);
            IWICPixelFormatInfo2_Release(info2);
        }
        else
            strcat(row->text, " (no IWICPixelFormatInfo2)");
        strcat(row->text, " masks=");
        for (c = 0; c < channels; c++)
        {
            BYTE mask[64];
            UINT size = 0, k;

            hr2 = IWICPixelFormatInfo_GetChannelMask(info, c, sizeof(mask), mask, &size);
            if (c) strcat(row->text, ",");
            if (FAILED(hr2)) { sprintf(row->text + strlen(row->text), "%#lx", hr2); continue; }
            for (k = 0; k < size && k < sizeof(mask); k++)
                sprintf(row->text + strlen(row->text), "%02x", mask[k]);
        }
        component = (IWICComponentInfo *)info;
        IWICComponentInfo_GetVendorGUID(component, &vendor);
        guid_text(&vendor, guid);
        sprintf(row->text + strlen(row->text), " vendor=%s", guid);
        append_string(row->text, "author", get_author, component);
        append_string(row->text, "name", get_name, component);
        append_string(row->text, "version", get_version, component);
        append_string(row->text, "spec", get_spec, component);
        IWICPixelFormatInfo_Release(info);
        IUnknown_Release(unk);
        count++;
    }
    IEnumUnknown_Release(enumerator);

    qsort(rows, count, sizeof(*rows), compare_rows);
    printf("%d pixel formats\n", count);
    for (i = 0; i < count; i++)
        printf("%s\n", rows[i].text);

    IWICImagingFactory_Release(factory);
    CoUninitialize();
    return 0;
}
