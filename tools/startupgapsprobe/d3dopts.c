/* d3dopts: what a hardware Direct3D 11 device says to every CheckFeatureSupport question (each structure's size
 * found by asking, its values in hex), what a GDI-compatible texture's IDXGISurface1::GetDC and ReleaseDC do and give,
 * which creation flags Direct3D 10.1 and Direct2D device contexts take, Direct2D's GDI-compatible render targets, and
 * the rendering parameters DirectWrite gives for a monitor.  Prints results only. */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <d3d11.h>
#include <d3d10_1.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <stdio.h>

static ID3D11Device *device;
static ID3D11DeviceContext *context;

static void features(void)
{
    static const char *names[] =
    {
        "THREADING", "DOUBLES", "FORMAT_SUPPORT", "FORMAT_SUPPORT2", "D3D10_X_HARDWARE_OPTIONS", "D3D11_OPTIONS",
        "ARCHITECTURE_INFO", "D3D9_OPTIONS", "SHADER_MIN_PRECISION_SUPPORT", "D3D9_SHADOW_SUPPORT", "D3D11_OPTIONS1",
        "D3D9_SIMPLE_INSTANCING_SUPPORT", "MARKER_SUPPORT", "D3D9_OPTIONS1", "D3D11_OPTIONS2", "D3D11_OPTIONS3",
        "GPU_VIRTUAL_ADDRESS_SUPPORT", "D3D11_OPTIONS4", "SHADER_CACHE", "D3D11_OPTIONS5", "DISPLAYABLE",
        "D3D11_OPTIONS6",
    };
    UINT data[32], size, feature, i;
    HRESULT hr;

    for (feature = 0; feature < 28; feature++)
    {
        if (feature == D3D11_FEATURE_FORMAT_SUPPORT || feature == D3D11_FEATURE_FORMAT_SUPPORT2) continue;
        printf("feature %u %s:", feature, feature < ARRAYSIZE(names) ? names[feature] : "?");
        for (size = 4; size <= sizeof(data); size += 4)
        {
            memset(data, 0xcc, sizeof(data));
            if (SUCCEEDED(hr = ID3D11Device_CheckFeatureSupport(device, feature, data, size))) break;
        }
        if (size > sizeof(data))
        {
            printf(" no size works, last %#lx\n", hr);
            continue;
        }
        printf(" size %u:", size);
        for (i = 0; i < size / 4; i++) printf(" %x", data[i]);
        hr = ID3D11Device_CheckFeatureSupport(device, feature, data, size + 4);
        printf(" (size+4 %#lx)\n", hr);
    }
    {
        static const DXGI_FORMAT formats[] = { DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8X8_UNORM,
            DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_A8_UNORM, DXGI_FORMAT_NV12, DXGI_FORMAT_R16G16B16A16_FLOAT,
            DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_D24_UNORM_S8_UINT };
        D3D11_FEATURE_DATA_FORMAT_SUPPORT support;
        D3D11_FEATURE_DATA_FORMAT_SUPPORT2 support2;

        for (i = 0; i < ARRAYSIZE(formats); i++)
        {
            support.InFormat = formats[i];
            support.OutFormatSupport = 0xdeadbeef;
            hr = ID3D11Device_CheckFeatureSupport(device, D3D11_FEATURE_FORMAT_SUPPORT, &support, sizeof(support));
            support2.InFormat = formats[i];
            support2.OutFormatSupport2 = 0xdeadbeef;
            printf("format %u: support %#lx %#x", formats[i], hr, support.OutFormatSupport);
            hr = ID3D11Device_CheckFeatureSupport(device, D3D11_FEATURE_FORMAT_SUPPORT2, &support2, sizeof(support2));
            printf(", support2 %#lx %#x\n", hr, support2.OutFormatSupport2);
        }
    }
}

static ID3D11Texture2D *texture_array(UINT bind, UINT misc, DXGI_FORMAT format, UINT levels, UINT array,
                                      HRESULT *hr)
{
    D3D11_TEXTURE2D_DESC desc = { 64, 64, levels, array, format, { 1, 0 }, D3D11_USAGE_DEFAULT, bind, 0, misc };
    ID3D11Texture2D *texture = NULL;

    *hr = ID3D11Device_CreateTexture2D(device, &desc, NULL, &texture);
    return texture;
}

static ID3D11Texture2D *texture(UINT bind, UINT misc, DXGI_FORMAT format, UINT levels, HRESULT *hr)
{
    return texture_array(bind, misc, format, levels, 1, hr);
}

static DWORD read_pixel(ID3D11Texture2D *texture, UINT x, UINT y)
{
    D3D11_TEXTURE2D_DESC desc;
    D3D11_MAPPED_SUBRESOURCE map;
    ID3D11Texture2D *staging;
    DWORD pixel = 0xdeadbeef;

    ID3D11Texture2D_GetDesc(texture, &desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(ID3D11Device_CreateTexture2D(device, &desc, NULL, &staging))) return pixel;
    ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging, (ID3D11Resource *)texture);
    if (SUCCEEDED(ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map)))
    {
        pixel = ((DWORD *)((BYTE *)map.pData + y * map.RowPitch))[x];
        ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);
    }
    ID3D11Texture2D_Release(staging);
    return pixel;
}

static void clear(ID3D11Texture2D *texture, float r, float g, float b, float a)
{
    ID3D11RenderTargetView *view;
    float colour[4] = { r, g, b, a };

    if (FAILED(ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)texture, NULL, &view))) return;
    ID3D11DeviceContext_ClearRenderTargetView(context, view, colour);
    ID3D11RenderTargetView_Release(view);
}

static void describe_dc(HDC dc)
{
    HBITMAP bitmap = GetCurrentObject(dc, OBJ_BITMAP);
    DIBSECTION dib;
    int size;
    POINT origin;
    RECT clip;

    memset(&dib, 0, sizeof(dib));
    size = GetObjectW(bitmap, sizeof(dib), &dib);
    GetViewportOrgEx(dc, &origin);
    GetClipBox(dc, &clip);
    printf("    dc: type %lu, bitmap object size %d, %ldx%ld, %u bpp, planes %u, width bytes %ld, bits %s,"
           " dib height %ld, compression %lu, map mode %d, origin %ld,%ld, clip box %ld,%ld-%ld,%ld, layout %#lx,"
           " graphics mode %d, technology %d, bitspixel %d\n",
           GetObjectType(dc), size, dib.dsBm.bmWidth, dib.dsBm.bmHeight, dib.dsBm.bmBitsPixel, dib.dsBm.bmPlanes,
           dib.dsBm.bmWidthBytes, dib.dsBm.bmBits ? "set" : "none", dib.dsBmih.biHeight, dib.dsBmih.biCompression,
           GetMapMode(dc), origin.x, origin.y, clip.left, clip.top, clip.right, clip.bottom, GetLayout(dc),
           GetGraphicsMode(dc), GetDeviceCaps(dc, TECHNOLOGY), GetDeviceCaps(dc, BITSPIXEL));
}

static void gdi_surfaces(void)
{
    static const struct { UINT bind, misc, levels; DXGI_FORMAT format; const char *what; UINT array; } textures[] =
    {
        { D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_GDI_COMPATIBLE, 1,
          DXGI_FORMAT_B8G8R8A8_UNORM, "BGRA rt+srv gdi" },
        { D3D11_BIND_RENDER_TARGET, D3D11_RESOURCE_MISC_GDI_COMPATIBLE, 1, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
          "BGRA sRGB rt gdi" },
        { D3D11_BIND_RENDER_TARGET, D3D11_RESOURCE_MISC_GDI_COMPATIBLE, 1, DXGI_FORMAT_B8G8R8X8_UNORM,
          "BGRX rt gdi" },
        { D3D11_BIND_RENDER_TARGET, D3D11_RESOURCE_MISC_GDI_COMPATIBLE, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
          "RGBA rt gdi" },
        { D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_GDI_COMPATIBLE, 1, DXGI_FORMAT_B8G8R8A8_UNORM,
          "BGRA srv gdi" },
        { D3D11_BIND_RENDER_TARGET, D3D11_RESOURCE_MISC_GDI_COMPATIBLE, 2, DXGI_FORMAT_B8G8R8A8_UNORM,
          "BGRA rt gdi 2 levels" },
        { D3D11_BIND_RENDER_TARGET, 0, 1, DXGI_FORMAT_B8G8R8A8_UNORM, "BGRA rt" },
        { D3D11_BIND_RENDER_TARGET, D3D11_RESOURCE_MISC_GDI_COMPATIBLE, 0, DXGI_FORMAT_B8G8R8A8_UNORM,
          "BGRA rt gdi all levels" },
        { D3D11_BIND_RENDER_TARGET, D3D11_RESOURCE_MISC_GDI_COMPATIBLE, 1, DXGI_FORMAT_B8G8R8A8_UNORM,
          "BGRA rt gdi array of 2", 2 },
        { D3D11_BIND_RENDER_TARGET, D3D11_RESOURCE_MISC_GDI_COMPATIBLE, 1, DXGI_FORMAT_B8G8R8A8_TYPELESS,
          "BGRA typeless rt gdi" },
        { D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS, D3D11_RESOURCE_MISC_GDI_COMPATIBLE, 1,
          DXGI_FORMAT_B8G8R8A8_UNORM, "BGRA rt+uav gdi" },
        { D3D11_BIND_RENDER_TARGET, D3D11_RESOURCE_MISC_GDI_COMPATIBLE | D3D11_RESOURCE_MISC_SHARED, 1,
          DXGI_FORMAT_B8G8R8A8_UNORM, "BGRA rt gdi shared" },
    };
    unsigned int i;

    for (i = 0; i < ARRAYSIZE(textures); i++)
    {
        IDXGISurface1 *surface;
        ID3D11Texture2D *tex;
        RECT dirty = { 0, 0, 1, 1 };
        HBRUSH brush;
        HDC dc, dc2;
        HRESULT hr;

        tex = texture_array(textures[i].bind, textures[i].misc, textures[i].format, textures[i].levels,
                            textures[i].array ? textures[i].array : 1, &hr);
        printf("%s: create %#lx", textures[i].what, hr);
        if (!tex)
        {
            printf("\n");
            continue;
        }
        hr = ID3D11Texture2D_QueryInterface(tex, &IID_IDXGISurface1, (void **)&surface);
        printf(", IDXGISurface1 %#lx", hr);
        if (FAILED(hr))
        {
            printf("\n");
            ID3D11Texture2D_Release(tex);
            continue;
        }
        clear(tex, 0.0f, 1.0f, 0.0f, 0.5f);
        dc = (HDC)0xdeadbeef;
        hr = IDXGISurface1_GetDC(surface, FALSE, &dc);
        printf(", GetDC %#lx dc %s\n", hr, dc == (HDC)0xdeadbeef ? "untouched" : dc ? "set" : "NULL");
        if (FAILED(hr))
        {
            hr = IDXGISurface1_ReleaseDC(surface, NULL);
            printf("    ReleaseDC without a dc %#lx\n", hr);
            hr = IDXGISurface1_GetDC(surface, FALSE, NULL);
            printf("    GetDC NULL %#lx\n", hr);
            IDXGISurface1_Release(surface);
            ID3D11Texture2D_Release(tex);
            continue;
        }
        describe_dc(dc);
        printf("    pixel the clear left %#lx\n", GetPixel(dc, 1, 1));
        dc2 = (HDC)0xdeadbeef;
        hr = IDXGISurface1_GetDC(surface, FALSE, &dc2);
        printf("    GetDC again %#lx dc %s\n", hr, dc2 == (HDC)0xdeadbeef ? "untouched" : dc2 == dc ? "same" : dc2 ? "other" : "NULL");
        brush = CreateSolidBrush(RGB(255, 0, 0));
        {
            RECT all = { 0, 0, 64, 64 };
            FillRect(dc, &all, brush);
        }
        DeleteObject(brush);
        hr = IDXGISurface1_ReleaseDC(surface, &dirty);
        printf("    ReleaseDC with a dirty rectangle %#lx, pixel 0,0 %#lx, 10,10 %#lx\n", hr, read_pixel(tex, 0, 0),
               read_pixel(tex, 10, 10));
        hr = IDXGISurface1_ReleaseDC(surface, NULL);
        printf("    ReleaseDC again %#lx\n", hr);

        clear(tex, 0.0f, 0.0f, 1.0f, 1.0f);
        hr = IDXGISurface1_GetDC(surface, TRUE, &dc);
        printf("    GetDC discarding %#lx", hr);
        if (SUCCEEDED(hr))
        {
            printf(", pixel %#lx", GetPixel(dc, 1, 1));
            SetPixel(dc, 2, 2, RGB(0, 255, 255));
            hr = IDXGISurface1_ReleaseDC(surface, NULL);
            printf(", ReleaseDC %#lx, pixel 2,2 %#lx, 3,3 %#lx", hr, read_pixel(tex, 2, 2), read_pixel(tex, 3, 3));
        }
        printf("\n");
        hr = IDXGISurface1_GetDC(surface, FALSE, &dc);
        if (SUCCEEDED(hr))
        {
            D3D11_MAPPED_SUBRESOURCE map;
            ID3D11RenderTargetView *view;

            hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)tex, NULL, &view);
            printf("    while the dc is out: CreateRenderTargetView %#lx", hr);
            if (SUCCEEDED(hr)) ID3D11RenderTargetView_Release(view);
            hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)tex, 0, D3D11_MAP_READ, 0, &map);
            printf(", Map %#lx", hr);
            if (SUCCEEDED(hr)) ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)tex, 0);
            hr = IDXGISurface1_ReleaseDC(surface, NULL);
            printf(", ReleaseDC %#lx\n", hr);
        }
        IDXGISurface1_Release(surface);
        ID3D11Texture2D_Release(tex);
    }
}

static void creation_flags(void)
{
    ID3D10Device1 *device10;
    IDXGIDevice *dxgi_device;
    ID2D1Factory1 *factory;
    ID2D1Device *d2d_device;
    ID2D1DeviceContext *d2d_context;
    unsigned int i;
    HRESULT hr;

    static const UINT flags10[] = { 0, D3D10_CREATE_DEVICE_BGRA_SUPPORT, D3D10_CREATE_DEVICE_BGRA_SUPPORT | 0x1,
        0x40, 0x80, 0x100 };
    HRESULT (WINAPI *create10)(IDXGIAdapter *, D3D10_DRIVER_TYPE, HMODULE, UINT, D3D10_FEATURE_LEVEL1, UINT,
                               ID3D10Device1 **);

    create10 = (void *)GetProcAddress(LoadLibraryW(L"d3d10_1.dll"), "D3D10CreateDevice1");
    for (i = 0; create10 && i < ARRAYSIZE(flags10); i++)
    {
        device10 = NULL;
        hr = create10(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, flags10[i], D3D10_FEATURE_LEVEL_10_1,
                                D3D10_1_SDK_VERSION, &device10);
        printf("D3D10CreateDevice1 flags %#x: %#lx\n", flags10[i], hr);
        if (device10) ID3D10Device1_Release(device10);
    }

    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory1, NULL, (void **)&factory);
    printf("D2D1CreateFactory: %#lx\n", hr);
    if (FAILED(hr)) return;
    ID3D11Device_QueryInterface(device, &IID_IDXGIDevice, (void **)&dxgi_device);
    hr = ID2D1Factory1_CreateDevice(factory, dxgi_device, &d2d_device);
    printf("CreateDevice: %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        static const UINT options[] = { 0, 1, 2, 3, 0x80000000 };
        for (i = 0; i < ARRAYSIZE(options); i++)
        {
            d2d_context = NULL;
            hr = ID2D1Device_CreateDeviceContext(d2d_device, options[i], &d2d_context);
            printf("CreateDeviceContext options %#x: %#lx\n", options[i], hr);
            if (d2d_context) IUnknown_Release((IUnknown *)d2d_context);
        }
        IUnknown_Release((IUnknown *)d2d_device);
    }

    {
        static const struct { D2D1_RENDER_TARGET_USAGE usage; UINT misc; DXGI_FORMAT format; D2D1_ALPHA_MODE alpha;
                              const char *what; } targets[] =
        {
            { D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE, D3D11_RESOURCE_MISC_GDI_COMPATIBLE, DXGI_FORMAT_B8G8R8A8_UNORM,
              D2D1_ALPHA_MODE_PREMULTIPLIED, "gdi usage, gdi texture" },
            { D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE, 0, DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED,
              "gdi usage, plain texture" },
            { D2D1_RENDER_TARGET_USAGE_NONE, D3D11_RESOURCE_MISC_GDI_COMPATIBLE, DXGI_FORMAT_B8G8R8A8_UNORM,
              D2D1_ALPHA_MODE_PREMULTIPLIED, "no usage, gdi texture" },
            { D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE, D3D11_RESOURCE_MISC_GDI_COMPATIBLE, DXGI_FORMAT_B8G8R8A8_UNORM,
              D2D1_ALPHA_MODE_STRAIGHT, "gdi usage, gdi texture, straight alpha" },
            { D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE, D3D11_RESOURCE_MISC_GDI_COMPATIBLE, DXGI_FORMAT_B8G8R8A8_UNORM,
              D2D1_ALPHA_MODE_IGNORE, "gdi usage, gdi texture, ignore alpha" },
        };
        for (i = 0; i < ARRAYSIZE(targets); i++)
        {
            D2D1_RENDER_TARGET_PROPERTIES props = { D2D1_RENDER_TARGET_TYPE_DEFAULT,
                { targets[i].format, targets[i].alpha }, 0.0f, 0.0f, targets[i].usage, D2D1_FEATURE_LEVEL_DEFAULT };
            ID2D1GdiInteropRenderTarget *interop;
            ID2D1RenderTarget *target;
            IDXGISurface *surface;
            ID3D11Texture2D *tex;
            HDC dc;

            tex = texture(D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, targets[i].misc,
                          DXGI_FORMAT_B8G8R8A8_UNORM, 1, &hr);
            if (!tex) continue;
            ID3D11Texture2D_QueryInterface(tex, &IID_IDXGISurface, (void **)&surface);
            hr = ID2D1Factory_CreateDxgiSurfaceRenderTarget((ID2D1Factory *)factory, surface, &props, &target);
            printf("CreateDxgiSurfaceRenderTarget %s: %#lx", targets[i].what, hr);
            if (SUCCEEDED(hr))
            {
                hr = ID2D1RenderTarget_QueryInterface(target, &IID_ID2D1GdiInteropRenderTarget, (void **)&interop);
                printf(", interop %#lx", hr);
                if (SUCCEEDED(hr))
                {
                    hr = ID2D1GdiInteropRenderTarget_GetDC(interop, D2D1_DC_INITIALIZE_MODE_COPY, &dc);
                    printf(", GetDC outside drawing %#lx", hr);
                    ID2D1RenderTarget_BeginDraw(target);
                    dc = NULL;
                    hr = ID2D1GdiInteropRenderTarget_GetDC(interop, D2D1_DC_INITIALIZE_MODE_COPY, &dc);
                    printf(", GetDC %#lx", hr);
                    if (SUCCEEDED(hr))
                    {
                        hr = ID2D1GdiInteropRenderTarget_ReleaseDC(interop, NULL);
                        printf(", ReleaseDC %#lx", hr);
                    }
                    hr = ID2D1RenderTarget_EndDraw(target, NULL, NULL);
                    printf(", EndDraw %#lx", hr);
                    ID2D1GdiInteropRenderTarget_Release(interop);
                }
                ID2D1RenderTarget_Release(target);
            }
            printf("\n");
            IDXGISurface_Release(surface);
            ID3D11Texture2D_Release(tex);
        }
    }
    IDXGIDevice_Release(dxgi_device);
    ID2D1Factory1_Release(factory);
}

static void show_params(const char *what, HRESULT hr, IDWriteRenderingParams *params)
{
    printf("%s: %#lx", what, hr);
    if (SUCCEEDED(hr))
    {
        printf(" gamma %.3f, enhanced contrast %.3f, cleartype level %.3f, pixel geometry %d, rendering mode %d",
               IDWriteRenderingParams_GetGamma(params), IDWriteRenderingParams_GetEnhancedContrast(params),
               IDWriteRenderingParams_GetClearTypeLevel(params), IDWriteRenderingParams_GetPixelGeometry(params),
               IDWriteRenderingParams_GetRenderingMode(params));
        IDWriteRenderingParams_Release(params);
    }
    printf("\n");
}

static void rendering_params(void)
{
    IDWriteRenderingParams *params;
    IDWriteFactory *factory;
    POINT origin = { 0, 0 };
    HRESULT hr;
    BOOL smoothing = FALSE;
    UINT type = 0, contrast = 0, orientation = 0;

    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory, (IUnknown **)&factory))) return;
    hr = IDWriteFactory_CreateRenderingParams(factory, &params);
    show_params("CreateRenderingParams", hr, params);
    hr = IDWriteFactory_CreateMonitorRenderingParams(factory, MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &params);
    show_params("CreateMonitorRenderingParams primary", hr, params);
    hr = IDWriteFactory_CreateMonitorRenderingParams(factory, NULL, &params);
    show_params("CreateMonitorRenderingParams NULL", hr, params);
    hr = IDWriteFactory_CreateMonitorRenderingParams(factory, (HMONITOR)0xdead, &params);
    show_params("CreateMonitorRenderingParams bad", hr, params);
    IDWriteFactory_Release(factory);
    SystemParametersInfoW(SPI_GETFONTSMOOTHING, 0, &smoothing, 0);
    SystemParametersInfoW(SPI_GETFONTSMOOTHINGTYPE, 0, &type, 0);
    SystemParametersInfoW(SPI_GETFONTSMOOTHINGCONTRAST, 0, &contrast, 0);
    SystemParametersInfoW(SPI_GETFONTSMOOTHINGORIENTATION, 0, &orientation, 0);
    printf("font smoothing %d, type %u, contrast %u, orientation %u\n", smoothing, type, contrast, orientation);
    {
        static const WCHAR *names[] = { L"GammaLevel", L"EnhancedContrastLevel", L"ClearTypeLevel", L"PixelStructure",
                                        L"TextContrastLevel", L"GrayscaleEnhancedContrastLevel" };
        WCHAR path[64];
        unsigned int d, n;
        DWORD value, size;

        for (d = 1; d <= 4; d++)
        {
            swprintf(path, ARRAYSIZE(path), L"Software\\Microsoft\\Avalon.Graphics\\DISPLAY%u", d);
            printf("DISPLAY%u:", d);
            for (n = 0; n < ARRAYSIZE(names); n++)
            {
                size = sizeof(value);
                if (!RegGetValueW(HKEY_CURRENT_USER, path, names[n], RRF_RT_REG_DWORD, NULL, &value, &size))
                    printf(" %ls=%lu", names[n], value);
            }
            printf("\n");
        }
    }
}

int main(void)
{
    static const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL level;
    IDXGIDevice *dxgi_device;
    IDXGIAdapter *adapter;
    DXGI_ADAPTER_DESC desc;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                           ARRAYSIZE(levels), D3D11_SDK_VERSION, &device, &level, &context);
    printf("D3D11CreateDevice: %#lx, feature level %#x\n", hr, level);
    if (FAILED(hr)) return 1;
    ID3D11Device_QueryInterface(device, &IID_IDXGIDevice, (void **)&dxgi_device);
    IDXGIDevice_GetAdapter(dxgi_device, &adapter);
    IDXGIAdapter_GetDesc(adapter, &desc);
    printf("adapter: %ls, vendor %#x, device %#x\n", desc.Description, desc.VendorId, desc.DeviceId);
    IDXGIAdapter_Release(adapter);
    IDXGIDevice_Release(dxgi_device);

    features();
    gdi_surfaces();
    creation_flags();
    rendering_params();

    ID3D11DeviceContext_Release(context);
    ID3D11Device_Release(device);
    return 0;
}
