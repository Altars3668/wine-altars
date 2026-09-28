/*
 * More of what Direct2D's gradients do: the colour spaces of the second gradient stop collections with colours
 * that show a conversion, what GetGradientStops() and GetGradientStops1() give back, straight and premultiplied
 * interpolation of translucent stops, and the odd cases -- no stops, one stop, stops out of order or at one
 * position, a gradient of no length, a radial gradient of no radius.
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
    static const D2D1_GRADIENT_STOP mid[] = {{0.0f, {0.5f, 0.25f, 0.75f, 0.5f}}, {1.0f, {0.0f, 0.0f, 0.0f, 1.0f}}};
    static const D2D1_GRADIENT_STOP translucent[] =
            {{0.0f, {1.0f, 0.5f, 0.0f, 0.25f}}, {1.0f, {0.0f, 0.5f, 1.0f, 1.0f}}};
    static const D2D1_GRADIENT_STOP red_blue[] = {{0.0f, {1.0f, 0.0f, 0.0f, 1.0f}}, {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}}};
    static const D2D1_GRADIENT_STOP one[] = {{0.5f, {0.0f, 1.0f, 0.0f, 1.0f}}};
    static const D2D1_GRADIENT_STOP reversed[] =
            {{1.0f, {0.0f, 0.0f, 1.0f, 1.0f}}, {0.0f, {1.0f, 0.0f, 0.0f, 1.0f}}};
    static const D2D1_GRADIENT_STOP unsorted[] = {{0.0f, {1.0f, 0.0f, 0.0f, 1.0f}}, {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}},
            {0.5f, {0.0f, 1.0f, 0.0f, 1.0f}}};
    static const D2D1_GRADIENT_STOP step[] = {{0.0f, {1.0f, 0.0f, 0.0f, 1.0f}}, {0.5f, {1.0f, 0.0f, 0.0f, 1.0f}},
            {0.5f, {0.0f, 0.0f, 1.0f, 1.0f}}, {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}}};
    static const D2D1_GRADIENT_STOP outside[] =
            {{-1.0f, {1.0f, 0.0f, 0.0f, 1.0f}}, {2.0f, {0.0f, 0.0f, 1.0f, 1.0f}}};
    static const D2D1_GRADIENT_STOP three_same[] = {{0.5f, {1.0f, 0.0f, 0.0f, 1.0f}},
            {0.5f, {0.0f, 1.0f, 0.0f, 1.0f}}, {0.5f, {0.0f, 0.0f, 1.0f, 1.0f}}};
    D2D1_RENDER_TARGET_PROPERTIES desc = {0};
    ID2D1GradientStopCollection *stops;
    char name[128];
    unsigned int pre, post, mode;
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

    /* What the collections give back. */
    if ((stops = create("mid 2.2", mid, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP)))
    {
        print_stops("mid 2.2", stops);
        ID2D1GradientStopCollection_Release(stops);
    }
    if ((stops = create("mid 1.0", mid, 2, D2D1_GAMMA_1_0, D2D1_EXTEND_MODE_CLAMP)))
    {
        print_stops("mid 1.0", stops);
        linear("mid 1.0 drawn", stops, 16.0f, 32.0f, 1.0f);
        ID2D1GradientStopCollection_Release(stops);
    }
    for (pre = 1; pre <= 2; ++pre)
    {
        for (post = 1; post <= 2; ++post)
        {
            for (mode = 0; mode <= 1; ++mode)
            {
                sprintf(name, "mid pre %u post %u mode %u", pre, post, mode);
                if (!(stops = create1(name, mid, 2, pre, post, D2D1_BUFFER_PRECISION_8BPC_UNORM,
                        D2D1_EXTEND_MODE_CLAMP, mode)))
                    continue;
                if (!mode)
                    print_stops(name, stops);
                linear(name, stops, 16.0f, 32.0f, 1.0f);
                ID2D1GradientStopCollection_Release(stops);
                sprintf(name, "translucent pre %u post %u mode %u", pre, post, mode);
                if (!(stops = create1(name, translucent, 2, pre, post, D2D1_BUFFER_PRECISION_32BPC_FLOAT,
                        D2D1_EXTEND_MODE_CLAMP, mode)))
                    continue;
                linear(name, stops, 16.0f, 32.0f, 1.0f);
                ID2D1GradientStopCollection_Release(stops);
            }
        }
    }
    if ((stops = create("translucent 2.2", translucent, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP)))
    {
        linear("translucent 2.2", stops, 16.0f, 32.0f, 1.0f);
        linear("translucent 2.2 opacity 0.5", stops, 16.0f, 32.0f, 0.5f);
        ID2D1GradientStopCollection_Release(stops);
    }
    if ((stops = create("translucent 1.0", translucent, 2, D2D1_GAMMA_1_0, D2D1_EXTEND_MODE_CLAMP)))
    {
        linear("translucent 1.0", stops, 16.0f, 32.0f, 1.0f);
        ID2D1GradientStopCollection_Release(stops);
    }

    /* Odd collections. */
    hr = ID2D1RenderTarget_CreateGradientStopCollection(rt, red_blue, 0, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP,
            &stops);
    printf("no stops: %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        print_stops("no stops", stops);
        linear("no stops", stops, 16.0f, 32.0f, 1.0f);
        ID2D1GradientStopCollection_Release(stops);
    }
    hr = ID2D1DeviceContext_CreateGradientStopCollection(context, red_blue, 0, D2D1_COLOR_SPACE_SRGB,
            D2D1_COLOR_SPACE_SRGB, D2D1_BUFFER_PRECISION_8BPC_UNORM, D2D1_EXTEND_MODE_CLAMP,
            D2D1_COLOR_INTERPOLATION_MODE_STRAIGHT, (ID2D1GradientStopCollection1 **)&stops);
    printf("no stops 1: %#lx\n", hr);
    if (SUCCEEDED(hr))
        ID2D1GradientStopCollection_Release(stops);
    if ((stops = create("one", one, 1, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP)))
    {
        print_stops("one", stops);
        linear("one", stops, 16.0f, 32.0f, 1.0f);
        ID2D1GradientStopCollection_Release(stops);
    }
    if ((stops = create("reversed", reversed, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP)))
    {
        print_stops("reversed", stops);
        linear("reversed", stops, 16.0f, 32.0f, 1.0f);
        ID2D1GradientStopCollection_Release(stops);
    }
    if ((stops = create("unsorted", unsorted, 3, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP)))
    {
        print_stops("unsorted", stops);
        linear("unsorted", stops, 16.0f, 32.0f, 1.0f);
        ID2D1GradientStopCollection_Release(stops);
    }
    if ((stops = create("step", step, 4, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP)))
    {
        linear("step", stops, 16.0f, 32.0f, 1.0f);
        ID2D1GradientStopCollection_Release(stops);
    }
    if ((stops = create("three same", three_same, 3, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP)))
    {
        linear("three same", stops, 16.0f, 32.0f, 1.0f);
        ID2D1GradientStopCollection_Release(stops);
    }
    if ((stops = create("outside", outside, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP)))
    {
        linear("outside", stops, 16.0f, 32.0f, 1.0f);
        ID2D1GradientStopCollection_Release(stops);
    }
    if ((stops = create("outside wrap", outside, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_WRAP)))
    {
        linear("outside wrap", stops, 16.0f, 32.0f, 1.0f);
        ID2D1GradientStopCollection_Release(stops);
    }
    if ((stops = create("red blue", red_blue, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP)))
    {
        linear("no length", stops, 20.0f, 20.0f, 1.0f);
        linear("backwards", stops, 32.0f, 16.0f, 1.0f);
        radial("no radius", stops, 0.0f, 0.0f);
        radial("offset", stops, 16.0f, 8.0f);
        radial("offset outside", stops, 16.0f, 24.0f);
        ID2D1GradientStopCollection_Release(stops);
    }
    if ((stops = create("red blue wrap", red_blue, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_WRAP)))
    {
        linear("no length wrap", stops, 20.0f, 20.0f, 1.0f);
        radial("no radius wrap", stops, 0.0f, 0.0f);
        radial("offset wrap", stops, 16.0f, 8.0f);
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
