/*
 * What Direct2D's gradients draw where there is no position to speak of: a linear gradient of no length, a
 * radial one of no radius, the part of a radial gradient its origin, outside the ellipse, does not see; and
 * gradients repeating more often than once a pixel.
 *
 * Drawn into a WIC bitmap in memory, like d2dgrad.c.
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <d2d1_1.h>
#include <wincodec.h>
#include <stdio.h>

static ID2D1Factory1 *factory;
static IWICImagingFactory *wic;
static ID2D1RenderTarget *rt;
static ID2D1DeviceContext *context;
static IWICBitmap *bitmap;

static void print_row(const char *name)
{
    static const unsigned int xs[] = {0, 8, 15, 16, 18, 20, 22, 24, 26, 28, 30, 31, 32, 40, 63};
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

static void fill(const char *name, ID2D1Brush *brush)
{
    D2D1_RECT_F rect = {0.0f, 0.0f, 64.0f, 4.0f};
    D2D1_COLOR_F clear = {0.0f, 0.0f, 0.0f, 0.0f};
    HRESULT hr;

    ID2D1RenderTarget_BeginDraw(rt);
    ID2D1RenderTarget_Clear(rt, &clear);
    ID2D1RenderTarget_FillRectangle(rt, &rect, brush);
    if (FAILED(hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL)))
        printf("%s: EndDraw %#lx\n", name, hr);
    print_row(name);
}

static void linear(const char *name, ID2D1GradientStopCollection *stops, float start, float end, float opacity)
{
    D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props = {{start, 0.0f}, {end, 0.0f}};
    D2D1_BRUSH_PROPERTIES brush_props = {opacity, {{{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f}}}};
    ID2D1LinearGradientBrush *brush;
    HRESULT hr;

    if (FAILED(hr = ID2D1RenderTarget_CreateLinearGradientBrush(rt, &props, &brush_props, stops, &brush)))
    {
        printf("%s: CreateLinearGradientBrush %#lx\n", name, hr);
        return;
    }
    fill(name, (ID2D1Brush *)brush);
    ID2D1LinearGradientBrush_Release(brush);
}

static void radial(const char *name, ID2D1GradientStopCollection *stops, float rx, float ox)
{
    D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES props = {{0.0f, 2.0f}, {ox, 0.0f}, rx, rx};
    ID2D1RadialGradientBrush *brush;
    HRESULT hr;

    if (FAILED(hr = ID2D1RenderTarget_CreateRadialGradientBrush(rt, &props, NULL, stops, &brush)))
    {
        printf("%s: CreateRadialGradientBrush %#lx\n", name, hr);
        return;
    }
    fill(name, (ID2D1Brush *)brush);
    ID2D1RadialGradientBrush_Release(brush);
}

static void print_stops(const char *name, ID2D1GradientStopCollection *stops)
{
    ID2D1GradientStopCollection1 *stops1;
    D2D1_GRADIENT_STOP got[4];
    unsigned int i, count;

    count = ID2D1GradientStopCollection_GetGradientStopCount(stops);
    memset(got, 0xcc, sizeof(got));
    ID2D1GradientStopCollection_GetGradientStops(stops, got, min(count, 4));
    printf("%s: count %u stops", name, count);
    for (i = 0; i < min(count, 4); ++i)
        printf(" %.4g:%.4g,%.4g,%.4g,%.4g", got[i].position, got[i].color.r, got[i].color.g, got[i].color.b,
                got[i].color.a);
    if (SUCCEEDED(ID2D1GradientStopCollection_QueryInterface(stops, &IID_ID2D1GradientStopCollection1,
            (void **)&stops1)))
    {
        memset(got, 0xcc, sizeof(got));
        ID2D1GradientStopCollection1_GetGradientStops1(stops1, got, min(count, 4));
        printf(" stops1");
        for (i = 0; i < min(count, 4); ++i)
            printf(" %.4g:%.4g,%.4g,%.4g,%.4g", got[i].position, got[i].color.r, got[i].color.g, got[i].color.b,
                    got[i].color.a);
        printf(" pre %u post %u", ID2D1GradientStopCollection1_GetPreInterpolationSpace(stops1),
                ID2D1GradientStopCollection1_GetPostInterpolationSpace(stops1));
        ID2D1GradientStopCollection1_Release(stops1);
    }
    printf("\n");
}

static ID2D1GradientStopCollection *create(const char *name, const D2D1_GRADIENT_STOP *stops, UINT32 count,
        D2D1_GAMMA gamma, D2D1_EXTEND_MODE extend)
{
    ID2D1GradientStopCollection *collection;
    HRESULT hr;

    if (FAILED(hr = ID2D1RenderTarget_CreateGradientStopCollection(rt, stops, count, gamma, extend, &collection)))
    {
        printf("%s: %#lx\n", name, hr);
        return NULL;
    }
    return collection;
}

static ID2D1GradientStopCollection *create1(const char *name, const D2D1_GRADIENT_STOP *stops, UINT32 count,
        D2D1_COLOR_SPACE pre, D2D1_COLOR_SPACE post, D2D1_BUFFER_PRECISION precision, D2D1_EXTEND_MODE extend,
        D2D1_COLOR_INTERPOLATION_MODE mode)
{
    ID2D1GradientStopCollection1 *collection;
    HRESULT hr;

    if (FAILED(hr = ID2D1DeviceContext_CreateGradientStopCollection(context, stops, count, pre, post, precision,
            extend, mode, &collection)))
    {
        printf("%s: %#lx\n", name, hr);
        return NULL;
    }
    return (ID2D1GradientStopCollection *)collection;
}

int main(void)
{
    static const D2D1_GRADIENT_STOP three[] = {{0.0f, {1.0f, 0.0f, 0.0f, 1.0f}}, {0.25f, {0.0f, 1.0f, 0.0f, 1.0f}},
            {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}}};
    static const char *extend_names[] = {"clamp", "wrap", "mirror"};
    D2D1_RENDER_TARGET_PROPERTIES desc = {0};
    ID2D1GradientStopCollection *stops;
    unsigned int extend;
    char name[128];
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
    IWICImagingFactory_CreateBitmap(wic, 64, 4, &GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &bitmap);
    desc.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    if (FAILED(hr = ID2D1Factory1_CreateWicBitmapRenderTarget(factory, bitmap, &desc, &rt)))
    {
        printf("CreateWicBitmapRenderTarget %#lx\n", hr);
        return 1;
    }
    if (FAILED(hr = ID2D1RenderTarget_QueryInterface(rt, &IID_ID2D1DeviceContext, (void **)&context)))
    {
        printf("no device context %#lx\n", hr);
        return 1;
    }

    for (extend = 0; extend < 3; ++extend)
    {
        if (!(stops = create("three", three, 3, D2D1_GAMMA_2_2, extend)))
            continue;
        sprintf(name, "no length %s", extend_names[extend]);
        linear(name, stops, 20.0f, 20.0f, 1.0f);
        sprintf(name, "no radius %s", extend_names[extend]);
        radial(name, stops, 0.0f, 0.0f);
        sprintf(name, "origin outside %s", extend_names[extend]);
        radial(name, stops, 16.0f, 24.0f);
        sprintf(name, "origin on edge %s", extend_names[extend]);
        radial(name, stops, 16.0f, 16.0f);
        sprintf(name, "period 0.25 %s", extend_names[extend]);
        linear(name, stops, 16.0f, 16.25f, 1.0f);
        sprintf(name, "period 1 %s", extend_names[extend]);
        linear(name, stops, 16.0f, 17.0f, 1.0f);
        sprintf(name, "period 4 %s", extend_names[extend]);
        linear(name, stops, 16.0f, 20.0f, 1.0f);
        ID2D1GradientStopCollection_Release(stops);
    }

    ID2D1DeviceContext_Release(context);
    ID2D1RenderTarget_Release(rt);
    IWICBitmap_Release(bitmap);
    ID2D1Factory1_Release(factory);
    IWICImagingFactory_Release(wic);
    printf("done\n");
    return 0;
}
