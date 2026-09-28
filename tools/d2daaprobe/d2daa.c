/*
 * What Direct2D's per-primitive antialiasing draws: the coverage along a row across the edges of filled and
 * stroked geometry -- a circle, a triangle, a rectangle turned, a path with a curve, lines of one and two pixels,
 * an ellipse's outline -- and across an axis-aligned clip that does not fall on pixels; and the same with
 * D2D1_ANTIALIAS_MODE_ALIASED.  Opaque white is drawn on transparent black, so alpha is coverage.
 *
 * Drawn into a WIC bitmap in memory, so no device or desktop is needed.
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <d2d1_1.h>
#include <wincodec.h>
#include <stdio.h>
#include <math.h>

static ID2D1Factory *factory;
static IWICImagingFactory *wic;
static ID2D1RenderTarget *rt;
static IWICBitmap *bitmap;
static ID2D1SolidColorBrush *brush;

static void begin(void)
{
    D2D1_COLOR_F clear = {0.0f, 0.0f, 0.0f, 0.0f};

    ID2D1RenderTarget_BeginDraw(rt);
    ID2D1RenderTarget_Clear(rt, &clear);
}

static void end(const char *name)
{
    HRESULT hr;

    if (FAILED(hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL)))
        printf("%s: EndDraw %#lx\n", name, hr);
}

/* Alpha along row "y" from "x0" to "x1". */
static void row(const char *name, unsigned int y, unsigned int x0, unsigned int x1)
{
    WICRect rect = {0, 0, 64, 64};
    IWICBitmapLock *lock;
    UINT size, stride;
    unsigned int x;
    BYTE *data;

    IWICBitmap_Lock(bitmap, &rect, WICBitmapLockRead, &lock);
    IWICBitmapLock_GetDataPointer(lock, &size, &data);
    IWICBitmapLock_GetStride(lock, &stride);
    printf("%s y %u x %u..%u:", name, y, x0, x1);
    for (x = x0; x <= x1; ++x)
        printf(" %u", data[y * stride + x * 4 + 3]);
    printf("\n");
    IWICBitmapLock_Release(lock);
}

static ID2D1PathGeometry *path(const D2D1_POINT_2F *points, unsigned int count, const D2D1_BEZIER_SEGMENT *bezier)
{
    ID2D1PathGeometry *geometry;
    ID2D1GeometrySink *sink;
    unsigned int i;

    ID2D1Factory_CreatePathGeometry(factory, &geometry);
    ID2D1PathGeometry_Open(geometry, &sink);
    ID2D1GeometrySink_BeginFigure(sink, points[0], D2D1_FIGURE_BEGIN_FILLED);
    for (i = 1; i < count; ++i)
        ID2D1GeometrySink_AddLine(sink, points[i]);
    if (bezier)
        ID2D1GeometrySink_AddBezier(sink, bezier);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);
    ID2D1GeometrySink_Close(sink);
    ID2D1GeometrySink_Release(sink);
    return geometry;
}

static void run(D2D1_ANTIALIAS_MODE mode)
{
    static const D2D1_POINT_2F triangle[] = {{4.0f, 4.3f}, {60.0f, 12.7f}, {10.0f, 58.0f}};
    static const D2D1_POINT_2F curve_start[] = {{4.0f, 60.0f}, {4.0f, 20.0f}};
    static const D2D1_BEZIER_SEGMENT curve = {{30.0f, 0.0f}, {60.0f, 30.0f}, {40.0f, 60.0f}};
    const char *prefix = mode == D2D1_ANTIALIAS_MODE_ALIASED ? "aliased" : "aa";
    D2D1_ELLIPSE ellipse = {{32.3f, 32.6f}, 20.2f, 20.2f};
    D2D1_MATRIX_3X2_F m, identity = {{{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f}}};
    D2D1_RECT_F rect;
    ID2D1PathGeometry *geometry;
    D2D1_POINT_2F p0, p1;
    char name[64];
    float c, s;

    ID2D1RenderTarget_SetAntialiasMode(rt, mode);

    begin();
    ID2D1RenderTarget_FillEllipse(rt, &ellipse, (ID2D1Brush *)brush);
    sprintf(name, "%s circle", prefix);
    end(name);
    row(name, 32, 8, 16);
    row(name, 20, 10, 20);

    begin();
    geometry = path(triangle, 3, NULL);
    ID2D1RenderTarget_FillGeometry(rt, (ID2D1Geometry *)geometry, (ID2D1Brush *)brush, NULL);
    ID2D1PathGeometry_Release(geometry);
    sprintf(name, "%s triangle", prefix);
    end(name);
    row(name, 20, 2, 12);
    row(name, 20, 40, 60);

    begin();
    c = cosf(0.3f);
    s = sinf(0.3f);
    m._11 = c; m._12 = s; m._21 = -s; m._22 = c; m._31 = 32.0f; m._32 = 32.0f;
    ID2D1RenderTarget_SetTransform(rt, &m);
    rect.left = -20.0f; rect.top = -8.0f; rect.right = 20.0f; rect.bottom = 8.0f;
    ID2D1RenderTarget_FillRectangle(rt, &rect, (ID2D1Brush *)brush);
    ID2D1RenderTarget_SetTransform(rt, &identity);
    sprintf(name, "%s turned rectangle", prefix);
    end(name);
    row(name, 32, 8, 20);
    row(name, 24, 20, 40);

    begin();
    geometry = path(curve_start, 2, &curve);
    ID2D1RenderTarget_FillGeometry(rt, (ID2D1Geometry *)geometry, (ID2D1Brush *)brush, NULL);
    ID2D1PathGeometry_Release(geometry);
    sprintf(name, "%s curve", prefix);
    end(name);
    row(name, 16, 10, 40);
    row(name, 40, 36, 52);

    begin();
    p0.x = 4.0f; p0.y = 10.0f; p1.x = 60.0f; p1.y = 40.0f;
    ID2D1RenderTarget_DrawLine(rt, p0, p1, (ID2D1Brush *)brush, 1.0f, NULL);
    sprintf(name, "%s line 1", prefix);
    end(name);
    row(name, 25, 26, 38);

    begin();
    ID2D1RenderTarget_DrawLine(rt, p0, p1, (ID2D1Brush *)brush, 2.0f, NULL);
    sprintf(name, "%s line 2", prefix);
    end(name);
    row(name, 25, 26, 38);

    begin();
    ID2D1RenderTarget_DrawEllipse(rt, &ellipse, (ID2D1Brush *)brush, 2.0f, NULL);
    sprintf(name, "%s ellipse outline", prefix);
    end(name);
    row(name, 32, 8, 16);
    row(name, 20, 10, 20);

    begin();
    rect.left = 10.3f; rect.top = 10.6f; rect.right = 40.7f; rect.bottom = 50.2f;
    ID2D1RenderTarget_PushAxisAlignedClip(rt, &rect, mode);
    rect.left = 0.0f; rect.top = 0.0f; rect.right = 64.0f; rect.bottom = 64.0f;
    ID2D1RenderTarget_FillRectangle(rt, &rect, (ID2D1Brush *)brush);
    ID2D1RenderTarget_PopAxisAlignedClip(rt);
    sprintf(name, "%s clip", prefix);
    end(name);
    row(name, 30, 8, 13);
    row(name, 30, 38, 43);
    row(name, 50, 20, 22);
    row(name, 10, 20, 22);

    begin();
    rect.left = 10.25f; rect.top = 10.5f; rect.right = 40.75f; rect.bottom = 50.5f;
    ID2D1RenderTarget_FillRectangle(rt, &rect, (ID2D1Brush *)brush);
    sprintf(name, "%s rectangle", prefix);
    end(name);
    row(name, 30, 8, 13);
    row(name, 30, 38, 43);
    row(name, 10, 20, 22);
}

int main(void)
{
    D2D1_RENDER_TARGET_PROPERTIES desc = {0};
    D2D1_COLOR_F white = {1.0f, 1.0f, 1.0f, 1.0f};
    HRESULT hr;

    CoInitialize(NULL);
    if (FAILED(hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
            (void **)&wic)))
    {
        printf("WIC %#lx\n", hr);
        return 1;
    }
    if (FAILED(hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory, NULL, (void **)&factory)))
    {
        printf("D2D1CreateFactory %#lx\n", hr);
        return 1;
    }
    IWICImagingFactory_CreateBitmap(wic, 64, 64, &GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &bitmap);
    desc.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    if (FAILED(hr = ID2D1Factory_CreateWicBitmapRenderTarget(factory, bitmap, &desc, &rt)))
    {
        printf("CreateWicBitmapRenderTarget %#lx\n", hr);
        return 1;
    }
    ID2D1RenderTarget_CreateSolidColorBrush(rt, &white, NULL, &brush);

    run(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    run(D2D1_ANTIALIAS_MODE_ALIASED);

    ID2D1SolidColorBrush_Release(brush);
    ID2D1RenderTarget_Release(rt);
    IWICBitmap_Release(bitmap);
    ID2D1Factory_Release(factory);
    IWICImagingFactory_Release(wic);
    printf("done\n");
    return 0;
}
