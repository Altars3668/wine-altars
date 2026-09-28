/*
 * Where Direct2D ends a stroke's join: a V whose tip points right, stroked 4 wide with each kind of join, and a
 * line that turns straight back, both aliased so that each pixel is in or out.  The row along the V's bisector
 * shows how far the join reaches past the point it joins at, 40.
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <d2d1.h>
#include <wincodec.h>
#include <math.h>
#include <stdio.h>

static ID2D1Factory *factory;
static IWICImagingFactory *wic;
static ID2D1RenderTarget *rt;
static ID2D1SolidColorBrush *brush;
static IWICBitmap *bitmap;

static void row(const char *name, unsigned int y, unsigned int x0, unsigned int x1)
{
    WICRect rect = {0, 0, 128, 128};
    IWICBitmapLock *lock;
    UINT size, stride;
    unsigned int x;
    BYTE *data;

    IWICBitmap_Lock(bitmap, &rect, WICBitmapLockRead, &lock);
    IWICBitmapLock_GetDataPointer(lock, &size, &data);
    IWICBitmapLock_GetStride(lock, &stride);
    printf("%s y %u x %u..%u:", name, y, x0, x1);
    for (x = x0; x <= x1; ++x)
        printf("%c", data[y * stride + x * 4 + 3] ? '#' : '.');
    printf("\n");
    IWICBitmapLock_Release(lock);
}

/* The last pixel along row y that is drawn, or -1. */
static int reach(unsigned int y)
{
    WICRect rect = {0, 0, 128, 128};
    IWICBitmapLock *lock;
    UINT size, stride;
    int x, last = -1;
    BYTE *data;

    IWICBitmap_Lock(bitmap, &rect, WICBitmapLockRead, &lock);
    IWICBitmapLock_GetDataPointer(lock, &size, &data);
    IWICBitmapLock_GetStride(lock, &stride);
    for (x = 0; x < 128; ++x)
    {
        if (data[y * stride + x * 4 + 3])
            last = x;
    }
    IWICBitmapLock_Release(lock);
    return last;
}

static ID2D1StrokeStyle *style(D2D1_LINE_JOIN join, float limit)
{
    D2D1_STROKE_STYLE_PROPERTIES desc = {0};
    ID2D1StrokeStyle *style;

    desc.lineJoin = join;
    desc.miterLimit = limit;
    ID2D1Factory_CreateStrokeStyle(factory, &desc, NULL, 0, &style);
    return style;
}

/* A V with its tip at (40, 20.5), opening left by twice half_angle degrees; or, with half_angle 0, a line from
 * (10, 20.5) to the tip and straight back. */
static void stroke(const char *name, float half_angle, ID2D1StrokeStyle *style)
{
    D2D1_COLOR_F clear = {0.0f, 0.0f, 0.0f, 0.0f};
    D2D1_POINT_2F p = {40.0f, 20.5f}, q;
    ID2D1GeometrySink *sink;
    ID2D1PathGeometry *path;
    float a = half_angle * 3.14159265f / 180.0f;
    HRESULT hr;

    ID2D1Factory_CreatePathGeometry(factory, &path);
    ID2D1PathGeometry_Open(path, &sink);
    q.x = p.x - 30.0f * cosf(a);
    q.y = p.y - 30.0f * sinf(a);
    ID2D1GeometrySink_BeginFigure(sink, q, D2D1_FIGURE_BEGIN_HOLLOW);
    ID2D1GeometrySink_AddLine(sink, p);
    q.y = p.y + 30.0f * sinf(a);
    ID2D1GeometrySink_AddLine(sink, q);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_OPEN);
    ID2D1GeometrySink_Close(sink);
    ID2D1GeometrySink_Release(sink);

    ID2D1RenderTarget_BeginDraw(rt);
    ID2D1RenderTarget_Clear(rt, &clear);
    ID2D1RenderTarget_DrawGeometry(rt, (ID2D1Geometry *)path, (ID2D1Brush *)brush, 4.0f, style);
    hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL);
    printf("%s: %#lx, reaches %d, %d, %d\n", name, hr, reach(19), reach(20), reach(21));
    row(name, 20, 36, 70);
    row(name, 18, 36, 70);
    row(name, 23, 36, 70);
    ID2D1PathGeometry_Release(path);
}

int main(void)
{
    D2D1_RENDER_TARGET_PROPERTIES desc = {0};
    D2D1_COLOR_F white = {1.0f, 1.0f, 1.0f, 1.0f};
    ID2D1StrokeStyle *miter2, *bevel2, *round, *bevel, *miter10;
    HRESULT hr;

    CoInitialize(NULL);
    CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&wic);
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory, NULL, (void **)&factory);
    IWICImagingFactory_CreateBitmap(wic, 128, 128, &GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &bitmap);
    desc.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    if (FAILED(hr = ID2D1Factory_CreateWicBitmapRenderTarget(factory, bitmap, &desc, &rt)))
    {
        printf("CreateWicBitmapRenderTarget %#lx\n", hr);
        return 1;
    }
    ID2D1RenderTarget_CreateSolidColorBrush(rt, &white, NULL, &brush);
    ID2D1RenderTarget_SetAntialiasMode(rt, D2D1_ANTIALIAS_MODE_ALIASED);
    miter2 = style(D2D1_LINE_JOIN_MITER, 2.0f);
    bevel2 = style(D2D1_LINE_JOIN_MITER_OR_BEVEL, 2.0f);
    round = style(D2D1_LINE_JOIN_ROUND, 10.0f);
    bevel = style(D2D1_LINE_JOIN_BEVEL, 10.0f);
    miter10 = style(D2D1_LINE_JOIN_MITER, 10.0f);

    /* The mitre of a 30 degree V is 3.86 half widths long. */
    stroke("miter 2, 30", 15.0f, miter2);
    stroke("miter or bevel 2, 30", 15.0f, bevel2);
    stroke("miter 10, 30", 15.0f, miter10);
    stroke("round, 30", 15.0f, round);
    stroke("bevel, 30", 15.0f, bevel);
    /* That of an 8 degree V 14.3. */
    stroke("default, 8", 4.0f, NULL);
    stroke("miter 10, 8", 4.0f, miter10);
    /* Straight back. */
    stroke("default, back", 0.0f, NULL);
    stroke("miter 2, back", 0.0f, miter2);
    stroke("miter or bevel 2, back", 0.0f, bevel2);
    stroke("round, back", 0.0f, round);
    stroke("bevel, back", 0.0f, bevel);

    ID2D1StrokeStyle_Release(miter10);
    ID2D1StrokeStyle_Release(bevel);
    ID2D1StrokeStyle_Release(round);
    ID2D1StrokeStyle_Release(bevel2);
    ID2D1StrokeStyle_Release(miter2);
    ID2D1SolidColorBrush_Release(brush);
    ID2D1RenderTarget_Release(rt);
    IWICBitmap_Release(bitmap);
    ID2D1Factory_Release(factory);
    IWICImagingFactory_Release(wic);
    printf("done\n");
    return 0;
}
