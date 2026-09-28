/*
 * What Direct2D's gradients draw: linear and radial brushes with each extend mode, both gammas of the first
 * gradient stop collections, and the colour spaces, precisions and interpolation modes of the second; what
 * the collections say about themselves; which creations fail.
 *
 * Everything is drawn into a WIC bitmap in memory, so no device or desktop is needed.  Run the same PE on
 * Windows and under Wine and diff the two.
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <d2d1_1.h>
#include <wincodec.h>
#include <stdio.h>

static ID2D1Factory1 *factory;
static IWICImagingFactory *wic;

static ID2D1RenderTarget *target(IWICBitmap **bitmap)
{
    D2D1_RENDER_TARGET_PROPERTIES desc = {0};
    ID2D1RenderTarget *rt;
    HRESULT hr;

    IWICImagingFactory_CreateBitmap(wic, 64, 4, &GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, bitmap);
    desc.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
    desc.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    if (FAILED(hr = ID2D1Factory1_CreateWicBitmapRenderTarget(factory, *bitmap, &desc, &rt)))
    {
        printf("CreateWicBitmapRenderTarget %#lx\n", hr);
        exit(1);
    }
    return rt;
}

/* The pixels of row 1 at a few places. */
static void print_row(const char *name, IWICBitmap *bitmap)
{
    static const unsigned int xs[] = {0, 4, 8, 12, 15, 16, 20, 24, 28, 31, 32, 36, 40, 44, 47, 48, 52, 56, 63};
    WICRect rect = {0, 0, 64, 4};
    IWICBitmapLock *lock;
    UINT size, stride;
    BYTE *data;
    unsigned int i;

    IWICBitmap_Lock(bitmap, &rect, WICBitmapLockRead, &lock);
    IWICBitmapLock_GetDataPointer(lock, &size, &data);
    IWICBitmapLock_GetStride(lock, &stride);
    printf("%s:", name);
    for (i = 0; i < ARRAY_SIZE(xs); ++i)
    {
        const BYTE *p = data + stride + xs[i] * 4;
        printf(" %u=%02x%02x%02x%02x", xs[i], p[3], p[2], p[1], p[0]);
    }
    printf("\n");
    IWICBitmapLock_Release(lock);
}

static void linear(const char *name, ID2D1GradientStopCollection *stops)
{
    D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props = {{16.0f, 0.0f}, {32.0f, 0.0f}};
    ID2D1LinearGradientBrush *brush;
    D2D1_RECT_F rect = {0.0f, 0.0f, 64.0f, 4.0f};
    ID2D1RenderTarget *rt;
    IWICBitmap *bitmap;
    HRESULT hr;

    rt = target(&bitmap);
    ID2D1RenderTarget_CreateLinearGradientBrush(rt, &props, NULL, stops, &brush);
    ID2D1RenderTarget_BeginDraw(rt);
    ID2D1RenderTarget_FillRectangle(rt, &rect, (ID2D1Brush *)brush);
    hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL);
    if (FAILED(hr))
        printf("%s: EndDraw %#lx\n", name, hr);
    print_row(name, bitmap);
    ID2D1LinearGradientBrush_Release(brush);
    ID2D1RenderTarget_Release(rt);
    IWICBitmap_Release(bitmap);
}

static void radial(const char *name, ID2D1GradientStopCollection *stops)
{
    D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES props = {{0.0f, 2.0f}, {0.0f, 0.0f}, 16.0f, 16.0f};
    ID2D1RadialGradientBrush *brush;
    D2D1_RECT_F rect = {0.0f, 0.0f, 64.0f, 4.0f};
    ID2D1RenderTarget *rt;
    IWICBitmap *bitmap;

    rt = target(&bitmap);
    ID2D1RenderTarget_CreateRadialGradientBrush(rt, &props, NULL, stops, &brush);
    ID2D1RenderTarget_BeginDraw(rt);
    ID2D1RenderTarget_FillRectangle(rt, &rect, (ID2D1Brush *)brush);
    ID2D1RenderTarget_EndDraw(rt, NULL, NULL);
    print_row(name, bitmap);
    ID2D1RadialGradientBrush_Release(brush);
    ID2D1RenderTarget_Release(rt);
    IWICBitmap_Release(bitmap);
}

static void describe(const char *name, ID2D1GradientStopCollection *stops)
{
    ID2D1GradientStopCollection1 *stops1;

    printf("%s: gamma %u extend %u count %u", name, ID2D1GradientStopCollection_GetColorInterpolationGamma(stops),
            ID2D1GradientStopCollection_GetExtendMode(stops), ID2D1GradientStopCollection_GetGradientStopCount(stops));
    if (SUCCEEDED(ID2D1GradientStopCollection_QueryInterface(stops, &IID_ID2D1GradientStopCollection1,
            (void **)&stops1)))
    {
        D2D1_GRADIENT_STOP got[2];

        printf(" pre %u post %u precision %u interpolation %u",
                ID2D1GradientStopCollection1_GetPreInterpolationSpace(stops1),
                ID2D1GradientStopCollection1_GetPostInterpolationSpace(stops1),
                ID2D1GradientStopCollection1_GetBufferPrecision(stops1),
                ID2D1GradientStopCollection1_GetColorInterpolationMode(stops1));
        ID2D1GradientStopCollection1_GetGradientStops1(stops1, got, 2);
        printf(" stops1 %.3g:%.3g,%.3g,%.3g,%.3g", got[0].position, got[0].color.r, got[0].color.g, got[0].color.b,
                got[0].color.a);
        ID2D1GradientStopCollection1_GetGradientStops(stops1, got, 2);
        printf(" stops %.3g:%.3g,%.3g,%.3g,%.3g", got[0].position, got[0].color.r, got[0].color.g, got[0].color.b,
                got[0].color.a);
        ID2D1GradientStopCollection1_Release(stops1);
    }
    printf("\n");
}

int main(void)
{
    static const D2D1_GRADIENT_STOP opaque[] = {{0.0f, {1.0f, 0.0f, 0.0f, 1.0f}}, {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}}};
    static const D2D1_GRADIENT_STOP clear[] = {{0.0f, {1.0f, 1.0f, 1.0f, 1.0f}}, {1.0f, {0.0f, 1.0f, 0.0f, 0.0f}}};
    static const D2D1_GRADIENT_STOP inside[] = {{0.25f, {1.0f, 0.0f, 0.0f, 1.0f}}, {0.75f, {0.0f, 0.0f, 1.0f, 1.0f}}};
    static const D2D1_GRADIENT_STOP bright[] = {{0.0f, {2.0f, 0.5f, -0.5f, 1.0f}}, {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}}};
    static const char *extend_names[] = {"clamp", "wrap", "mirror"};
    ID2D1GradientStopCollection *stops;
    ID2D1GradientStopCollection1 *stops1;
    ID2D1DeviceContext *context;
    ID2D1RenderTarget *rt;
    IWICBitmap *bitmap;
    char name[128];
    unsigned int extend, gamma, pre, post, mode, precision;
    HRESULT hr;

    CoInitialize(NULL);
    if (FAILED(hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
            (void **)&wic)))
    {
        printf("WIC %#lx\n", hr);
        return 1;
    }
    if (FAILED(hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory1, NULL, (void **)&factory)))
    {
        printf("D2D1CreateFactory %#lx\n", hr);
        return 1;
    }

    rt = target(&bitmap);
    for (gamma = 0; gamma < 2; ++gamma)
    {
        for (extend = 0; extend < 3; ++extend)
        {
            ID2D1RenderTarget_CreateGradientStopCollection(rt, opaque, 2, gamma, extend, &stops);
            sprintf(name, "linear %s gamma %s", extend_names[extend], gamma ? "1.0" : "2.2");
            describe(name, stops);
            linear(name, stops);
            sprintf(name, "radial %s gamma %s", extend_names[extend], gamma ? "1.0" : "2.2");
            radial(name, stops);
            ID2D1GradientStopCollection_Release(stops);
        }
        ID2D1RenderTarget_CreateGradientStopCollection(rt, clear, 2, gamma, D2D1_EXTEND_MODE_CLAMP, &stops);
        sprintf(name, "to clear gamma %s", gamma ? "1.0" : "2.2");
        linear(name, stops);
        ID2D1GradientStopCollection_Release(stops);
        ID2D1RenderTarget_CreateGradientStopCollection(rt, inside, 2, gamma, D2D1_EXTEND_MODE_MIRROR, &stops);
        sprintf(name, "stops inside, mirror gamma %s", gamma ? "1.0" : "2.2");
        linear(name, stops);
        ID2D1GradientStopCollection_Release(stops);
    }
    hr = ID2D1RenderTarget_CreateGradientStopCollection(rt, opaque, 2, 2, D2D1_EXTEND_MODE_CLAMP, &stops);
    printf("gamma 2: %#lx\n", hr);
    if (SUCCEEDED(hr))
        ID2D1GradientStopCollection_Release(stops);
    hr = ID2D1RenderTarget_CreateGradientStopCollection(rt, opaque, 2, 0, 3, &stops);
    printf("extend 3: %#lx\n", hr);
    if (SUCCEEDED(hr))
        ID2D1GradientStopCollection_Release(stops);

    if (FAILED(hr = ID2D1RenderTarget_QueryInterface(rt, &IID_ID2D1DeviceContext, (void **)&context)))
    {
        printf("no device context %#lx\n", hr);
        return 0;
    }
    for (pre = 0; pre < 2; ++pre)
    {
        for (post = 0; post < 2; ++post)
        {
            for (mode = 0; mode < 2; ++mode)
            {
                hr = ID2D1DeviceContext_CreateGradientStopCollection(context, clear, 2, pre, post,
                        D2D1_BUFFER_PRECISION_8BPC_UNORM, D2D1_EXTEND_MODE_CLAMP, mode, &stops1);
                sprintf(name, "collection1 pre %u post %u mode %u", pre, post, mode);
                if (FAILED(hr))
                {
                    printf("%s: %#lx\n", name, hr);
                    continue;
                }
                describe(name, (ID2D1GradientStopCollection *)stops1);
                linear(name, (ID2D1GradientStopCollection *)stops1);
                ID2D1GradientStopCollection1_Release(stops1);
            }
        }
    }
    for (precision = 0; precision <= 5; ++precision)
    {
        hr = ID2D1DeviceContext_CreateGradientStopCollection(context, bright, 2, D2D1_COLOR_SPACE_SCRGB,
                D2D1_COLOR_SPACE_SRGB, precision, D2D1_EXTEND_MODE_MIRROR, D2D1_COLOR_INTERPOLATION_MODE_STRAIGHT,
                &stops1);
        sprintf(name, "collection1 precision %u", precision);
        if (FAILED(hr))
        {
            printf("%s: %#lx\n", name, hr);
            continue;
        }
        describe(name, (ID2D1GradientStopCollection *)stops1);
        linear(name, (ID2D1GradientStopCollection *)stops1);
        ID2D1GradientStopCollection1_Release(stops1);
    }
    hr = ID2D1DeviceContext_CreateGradientStopCollection(context, clear, 2, D2D1_COLOR_SPACE_CUSTOM,
            D2D1_COLOR_SPACE_SRGB, D2D1_BUFFER_PRECISION_8BPC_UNORM, D2D1_EXTEND_MODE_CLAMP,
            D2D1_COLOR_INTERPOLATION_MODE_STRAIGHT, &stops1);
    printf("custom pre space: %#lx\n", hr);
    if (SUCCEEDED(hr))
        ID2D1GradientStopCollection1_Release(stops1);
    hr = ID2D1DeviceContext_CreateGradientStopCollection(context, clear, 2, D2D1_COLOR_SPACE_SRGB,
            D2D1_COLOR_SPACE_SRGB, D2D1_BUFFER_PRECISION_8BPC_UNORM, D2D1_EXTEND_MODE_CLAMP, 2, &stops1);
    printf("interpolation mode 2: %#lx\n", hr);
    if (SUCCEEDED(hr))
        ID2D1GradientStopCollection1_Release(stops1);

    ID2D1DeviceContext_Release(context);
    ID2D1RenderTarget_Release(rt);
    IWICBitmap_Release(bitmap);
    ID2D1Factory1_Release(factory);
    IWICImagingFactory_Release(wic);
    printf("done\n");
    return 0;
}
