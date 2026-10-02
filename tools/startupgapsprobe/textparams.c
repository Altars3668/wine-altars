/* textparams: the text rendering parameters DirectWrite gives (every IDWriteRenderingParams3 value, for
 * CreateRenderingParams and CreateMonitorRenderingParams), what per-monitor values in
 * HKCU\Software\Microsoft\Avalon.Graphics\DISPLAYn change and how they are scaled, and whether Direct2D draws text
 * in ClearType or in grayscale for each kind of target, alpha mode, background and antialias mode.
 *
 *   textparams.exe            the defaults and Direct2D's text
 *   textparams.exe override   the same parameters with test values under Avalon.Graphics, read from a private hive
 *                             (RegLoadAppKey) put in place of HKEY_CURRENT_USER for this process only, so the
 *                             user's own registry is never written
 *
 * Prints results only. */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <d3d11.h>
#include <d2d1.h>
#include <dwrite_3.h>
#include <wincodec.h>
#include <stdio.h>

static HRESULT (WINAPI *pDWriteCreateFactory)(DWRITE_FACTORY_TYPE, REFIID, IUnknown **);

static void show_params(const char *what, HRESULT hr, IDWriteRenderingParams *params)
{
    IDWriteRenderingParams1 *params1;
    IDWriteRenderingParams2 *params2;
    IDWriteRenderingParams3 *params3;

    printf("%s: %#lx", what, hr);
    if (FAILED(hr))
    {
        printf("\n");
        return;
    }
    printf(" gamma %.3f, enhanced contrast %.3f, cleartype level %.3f, pixel geometry %d, rendering mode %d",
           IDWriteRenderingParams_GetGamma(params), IDWriteRenderingParams_GetEnhancedContrast(params),
           IDWriteRenderingParams_GetClearTypeLevel(params), IDWriteRenderingParams_GetPixelGeometry(params),
           IDWriteRenderingParams_GetRenderingMode(params));
    if (SUCCEEDED(IDWriteRenderingParams_QueryInterface(params, &IID_IDWriteRenderingParams1, (void **)&params1)))
    {
        printf(", grayscale enhanced contrast %.3f", IDWriteRenderingParams1_GetGrayscaleEnhancedContrast(params1));
        IDWriteRenderingParams1_Release(params1);
    }
    if (SUCCEEDED(IDWriteRenderingParams_QueryInterface(params, &IID_IDWriteRenderingParams2, (void **)&params2)))
    {
        printf(", grid fit %d", IDWriteRenderingParams2_GetGridFitMode(params2));
        IDWriteRenderingParams2_Release(params2);
    }
    if (SUCCEEDED(IDWriteRenderingParams_QueryInterface(params, &IID_IDWriteRenderingParams3, (void **)&params3)))
    {
        printf(", rendering mode1 %d", IDWriteRenderingParams3_GetRenderingMode1(params3));
        IDWriteRenderingParams3_Release(params3);
    }
    printf("\n");
    IDWriteRenderingParams_Release(params);
}

static void rendering_params(DWRITE_FACTORY_TYPE type)
{
    MONITORINFOEXW info = { sizeof(info) };
    IDWriteRenderingParams *params;
    IDWriteFactory *factory;
    POINT origin = { 0, 0 };
    HMONITOR monitor;
    HRESULT hr;

    if (FAILED(hr = pDWriteCreateFactory(type, &IID_IDWriteFactory, (IUnknown **)&factory)))
    {
        printf("DWriteCreateFactory: %#lx\n", hr);
        return;
    }
    monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    GetMonitorInfoW(monitor, (MONITORINFO *)&info);
    printf("primary monitor %ls\n", info.szDevice);
    hr = IDWriteFactory_CreateRenderingParams(factory, &params);
    show_params("CreateRenderingParams", hr, params);
    hr = IDWriteFactory_CreateMonitorRenderingParams(factory, monitor, &params);
    show_params("CreateMonitorRenderingParams primary", hr, params);
    hr = IDWriteFactory_CreateMonitorRenderingParams(factory, NULL, &params);
    show_params("CreateMonitorRenderingParams NULL", hr, params);
    IDWriteFactory_Release(factory);
}

/* the test values, each telling its scale apart */
static void set_test_values(HKEY root)
{
    static const struct { const WCHAR *name; DWORD value; } values[] =
    {
        { L"GammaLevel", 2200 },
        { L"EnhancedContrastLevel", 75 },
        { L"GrayscaleEnhancedContrastLevel", 150 },
        { L"ClearTypeLevel", 40 },
        { L"PixelStructure", 2 },
        { L"TextRenderingMode", 3 },
        { L"RenderingMode", 5 },
        { L"GridFitMode", 2 },
    };
    MONITORINFOEXW info = { sizeof(info) };
    POINT origin = { 0, 0 };
    WCHAR path[128];
    unsigned int i;
    HKEY key;
    LONG ret;

    GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), (MONITORINFO *)&info);
    swprintf(path, ARRAYSIZE(path), L"Software\\Microsoft\\Avalon.Graphics\\%ls",
             wcsncmp(info.szDevice, L"\\\\.\\", 4) ? info.szDevice : info.szDevice + 4);
    ret = RegCreateKeyExW(root, path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &key, NULL);
    printf("test values under %ls: %ld", path, ret);
    if (ret)
    {
        printf("\n");
        return;
    }
    for (i = 0; i < ARRAYSIZE(values); i++)
    {
        RegSetValueExW(key, values[i].name, 0, REG_DWORD, (const BYTE *)&values[i].value, sizeof(DWORD));
        printf(" %ls=%lu", values[i].name, values[i].value);
    }
    printf("\n");
    RegCloseKey(key);
}

/* tell whether any pixel of the text is coloured, as ClearType makes them; the text is black on white and white on
 * transparent, so that premultiplied colour channels can tell the subpixels apart either way */
static void count_pixels(const BYTE *bits, UINT pitch, UINT width, UINT height, const char *what)
{
    unsigned int x, y, coloured = 0, grey = 0;

    for (y = 0; y < height; y++)
    {
        const BYTE *row = bits + y * pitch;
        for (x = 0; x < width; x++)
        {
            BYTE b = row[x * 4], g = row[x * 4 + 1], r = row[x * 4 + 2], a = row[x * 4 + 3];
            if (b == g && g == r && (a == 0 || (r == 0xff && a == 0xff))) continue;   /* background */
            if (abs(r - g) > 2 || abs(g - b) > 2) coloured++;
            else grey++;
        }
    }
    printf("%s: coloured %u, grey %u -> %s\n", what, coloured, grey,
           coloured ? "cleartype" : grey ? "grayscale" : "nothing drawn");
}

static void draw_text(ID2D1RenderTarget *target, IDWriteFactory *dwrite, BOOL opaque, D2D1_TEXT_ANTIALIAS_MODE mode)
{
    static const WCHAR text[] = L"Wgy Wgy";
    D2D1_COLOR_F white = { 1.0f, 1.0f, 1.0f, 1.0f }, clear = { 0.0f, 0.0f, 0.0f, 0.0f }, black = { 0.0f, 0.0f, 0.0f, 1.0f };
    D2D1_RECT_F rect = { 2.0f, 2.0f, 126.0f, 30.0f };
    IDWriteTextFormat *format;
    ID2D1SolidColorBrush *brush;

    IDWriteFactory_CreateTextFormat(dwrite, L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                    DWRITE_FONT_STRETCH_NORMAL, 18.0f, L"en-us", &format);
    ID2D1RenderTarget_CreateSolidColorBrush(target, opaque ? &black : &white, NULL, &brush);
    ID2D1RenderTarget_BeginDraw(target);
    ID2D1RenderTarget_Clear(target, opaque ? &white : &clear);
    ID2D1RenderTarget_SetTextAntialiasMode(target, mode);
    ID2D1RenderTarget_DrawText(target, text, ARRAYSIZE(text) - 1, format, &rect, (ID2D1Brush *)brush,
                               D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
    ID2D1RenderTarget_EndDraw(target, NULL, NULL);
    ID2D1SolidColorBrush_Release(brush);
    IDWriteTextFormat_Release(format);
}

static const char *mode_name(D2D1_TEXT_ANTIALIAS_MODE mode)
{
    return mode == D2D1_TEXT_ANTIALIAS_MODE_DEFAULT ? "default" : mode == D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE ?
           "cleartype" : mode == D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE ? "grayscale" : "aliased";
}

static void wic_cases(ID2D1Factory *factory, IDWriteFactory *dwrite)
{
    static const struct { const GUID *format; D2D1_ALPHA_MODE alpha; BOOL opaque; D2D1_TEXT_ANTIALIAS_MODE mode;
                          const char *what; } cases[] =
    {
        { &GUID_WICPixelFormat32bppPBGRA, D2D1_ALPHA_MODE_PREMULTIPLIED, FALSE, D2D1_TEXT_ANTIALIAS_MODE_DEFAULT,
          "premultiplied on transparent" },
        { &GUID_WICPixelFormat32bppPBGRA, D2D1_ALPHA_MODE_PREMULTIPLIED, TRUE, D2D1_TEXT_ANTIALIAS_MODE_DEFAULT,
          "premultiplied on white" },
        { &GUID_WICPixelFormat32bppBGR, D2D1_ALPHA_MODE_IGNORE, TRUE, D2D1_TEXT_ANTIALIAS_MODE_DEFAULT,
          "ignore on white" },
        { &GUID_WICPixelFormat32bppPBGRA, D2D1_ALPHA_MODE_PREMULTIPLIED, FALSE, D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE,
          "premultiplied on transparent" },
        { &GUID_WICPixelFormat32bppPBGRA, D2D1_ALPHA_MODE_PREMULTIPLIED, TRUE, D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE,
          "premultiplied on white" },
        { &GUID_WICPixelFormat32bppBGR, D2D1_ALPHA_MODE_IGNORE, TRUE, D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE,
          "ignore on white" },
    };
    IWICImagingFactory *wic;
    unsigned int i;
    HRESULT hr;

    if (FAILED(hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
                                     (void **)&wic)))
    {
        printf("WIC: %#lx\n", hr);
        return;
    }
    for (i = 0; i < ARRAYSIZE(cases); i++)
    {
        D2D1_RENDER_TARGET_PROPERTIES props = { D2D1_RENDER_TARGET_TYPE_DEFAULT,
            { DXGI_FORMAT_B8G8R8A8_UNORM, cases[i].alpha }, 0.0f, 0.0f, D2D1_RENDER_TARGET_USAGE_NONE,
            D2D1_FEATURE_LEVEL_DEFAULT };
        WICRect all = { 0, 0, 128, 32 };
        IWICBitmapLock *lock;
        ID2D1RenderTarget *target;
        IWICBitmap *bitmap;
        char what[128];
        UINT pitch, size;
        BYTE *bits;

        IWICImagingFactory_CreateBitmap(wic, 128, 32, cases[i].format, WICBitmapCacheOnLoad, &bitmap);
        hr = ID2D1Factory_CreateWicBitmapRenderTarget(factory, bitmap, &props, &target);
        sprintf(what, "wic %s, %s", cases[i].what, mode_name(cases[i].mode));
        if (FAILED(hr))
        {
            printf("%s: target %#lx\n", what, hr);
            IWICBitmap_Release(bitmap);
            continue;
        }
        if (!i) printf("wic default antialias mode %d, text params %s\n", ID2D1RenderTarget_GetTextAntialiasMode(target),
                       "(set by none)");
        draw_text(target, dwrite, cases[i].opaque, cases[i].mode);
        ID2D1RenderTarget_Release(target);
        if (SUCCEEDED(IWICBitmap_Lock(bitmap, &all, WICBitmapLockRead, &lock)))
        {
            IWICBitmapLock_GetStride(lock, &pitch);
            IWICBitmapLock_GetDataPointer(lock, &size, &bits);
            count_pixels(bits, pitch, 128, 32, what);
            IWICBitmapLock_Release(lock);
        }
        IWICBitmap_Release(bitmap);
    }
    IWICImagingFactory_Release(wic);
}

static void dxgi_cases(ID2D1Factory *factory, IDWriteFactory *dwrite)
{
    static const struct { D2D1_ALPHA_MODE alpha; BOOL opaque; D2D1_TEXT_ANTIALIAS_MODE mode; const char *what; } cases[] =
    {
        { D2D1_ALPHA_MODE_PREMULTIPLIED, FALSE, D2D1_TEXT_ANTIALIAS_MODE_DEFAULT, "premultiplied on transparent" },
        { D2D1_ALPHA_MODE_PREMULTIPLIED, TRUE, D2D1_TEXT_ANTIALIAS_MODE_DEFAULT, "premultiplied on white" },
        { D2D1_ALPHA_MODE_IGNORE, TRUE, D2D1_TEXT_ANTIALIAS_MODE_DEFAULT, "ignore on white" },
        { D2D1_ALPHA_MODE_PREMULTIPLIED, FALSE, D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE, "premultiplied on transparent" },
        { D2D1_ALPHA_MODE_PREMULTIPLIED, TRUE, D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE, "premultiplied on white" },
    };
    D3D11_TEXTURE2D_DESC desc = { 128, 32, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT,
                                  D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    ID3D11DeviceContext *context;
    ID3D11Device *device;
    unsigned int i;
    HRESULT hr;

    if (FAILED(hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                                      D3D11_SDK_VERSION, &device, NULL, &context)))
    {
        printf("D3D11CreateDevice: %#lx\n", hr);
        return;
    }
    for (i = 0; i < ARRAYSIZE(cases); i++)
    {
        D2D1_RENDER_TARGET_PROPERTIES props = { D2D1_RENDER_TARGET_TYPE_DEFAULT,
            { DXGI_FORMAT_B8G8R8A8_UNORM, cases[i].alpha }, 0.0f, 0.0f, D2D1_RENDER_TARGET_USAGE_NONE,
            D2D1_FEATURE_LEVEL_DEFAULT };
        D3D11_TEXTURE2D_DESC staging_desc = desc;
        D3D11_MAPPED_SUBRESOURCE map;
        ID3D11Texture2D *texture, *staging;
        ID2D1RenderTarget *target;
        IDXGISurface *surface;
        char what[128];

        ID3D11Device_CreateTexture2D(device, &desc, NULL, &texture);
        ID3D11Texture2D_QueryInterface(texture, &IID_IDXGISurface, (void **)&surface);
        hr = ID2D1Factory_CreateDxgiSurfaceRenderTarget(factory, surface, &props, &target);
        sprintf(what, "dxgi %s, %s", cases[i].what, mode_name(cases[i].mode));
        if (SUCCEEDED(hr))
        {
            if (!i) printf("dxgi default antialias mode %d\n", ID2D1RenderTarget_GetTextAntialiasMode(target));
            draw_text(target, dwrite, cases[i].opaque, cases[i].mode);
            ID2D1RenderTarget_Release(target);
            staging_desc.Usage = D3D11_USAGE_STAGING;
            staging_desc.BindFlags = 0;
            staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ID3D11Device_CreateTexture2D(device, &staging_desc, NULL, &staging);
            ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging, (ID3D11Resource *)texture);
            if (SUCCEEDED(ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map)))
            {
                count_pixels(map.pData, map.RowPitch, 128, 32, what);
                ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);
            }
            ID3D11Texture2D_Release(staging);
        }
        else printf("%s: target %#lx\n", what, hr);
        IDXGISurface_Release(surface);
        ID3D11Texture2D_Release(texture);
    }
    ID3D11DeviceContext_Release(context);
    ID3D11Device_Release(device);
}

int main(int argc, char **argv)
{
    ID2D1Factory *factory;
    IDWriteFactory *dwrite;
    HMODULE module;

    setvbuf(stdout, NULL, _IONBF, 0);
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    if (argc > 1 && !strcmp(argv[1], "override"))
    {
        WCHAR hive[MAX_PATH];
        HKEY root;
        LONG ret;

        /* a private hive in the current directory, never the user's registry, before DirectWrite is loaded */
        GetFullPathNameW(L"textparams.hiv", ARRAYSIZE(hive), hive, NULL);
        DeleteFileW(hive);
        ret = RegLoadAppKeyW(hive, &root, KEY_ALL_ACCESS, 0, 0);
        printf("RegLoadAppKey: %ld\n", ret);
        if (ret) return 1;
        set_test_values(root);
        ret = RegOverridePredefKey(HKEY_CURRENT_USER, root);
        printf("RegOverridePredefKey: %ld\n", ret);
        module = LoadLibraryW(L"dwrite.dll");
        pDWriteCreateFactory = (void *)GetProcAddress(module, "DWriteCreateFactory");
        printf("shared factory:\n");
        rendering_params(DWRITE_FACTORY_TYPE_SHARED);
        printf("isolated factory:\n");
        rendering_params(DWRITE_FACTORY_TYPE_ISOLATED);
        RegOverridePredefKey(HKEY_CURRENT_USER, NULL);
        RegCloseKey(root);
        return 0;
    }

    module = LoadLibraryW(L"dwrite.dll");
    pDWriteCreateFactory = (void *)GetProcAddress(module, "DWriteCreateFactory");
    {
        BOOL smoothing = FALSE;
        UINT type = 0, contrast = 0, orientation = 0;

        SystemParametersInfoW(SPI_GETFONTSMOOTHING, 0, &smoothing, 0);
        SystemParametersInfoW(SPI_GETFONTSMOOTHINGTYPE, 0, &type, 0);
        SystemParametersInfoW(SPI_GETFONTSMOOTHINGCONTRAST, 0, &contrast, 0);
        SystemParametersInfoW(SPI_GETFONTSMOOTHINGORIENTATION, 0, &orientation, 0);
        printf("font smoothing %d, type %u, contrast %u, orientation %u\n", smoothing, type, contrast, orientation);
    }
    rendering_params(DWRITE_FACTORY_TYPE_SHARED);

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory, NULL, (void **)&factory)))
        return 1;
    pDWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory, (IUnknown **)&dwrite);
    wic_cases(factory, dwrite);
    dxgi_cases(factory, dwrite);
    IDWriteFactory_Release(dwrite);
    ID2D1Factory_Release(factory);
    return 0;
}
