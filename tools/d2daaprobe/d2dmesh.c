/*
 * What ID2D1RenderTarget::FillMesh() draws, through a half-transparent white brush onto transparent black, so that a
 * pixel filled twice shows as 191 rather than 128: two triangles sharing an edge, two overlapping, one with its edges
 * through pixel centres, both windings, a degenerate one, one under a transform, and the tessellation of a circle
 * against the circle filled as a geometry.  Then what happens when the target is not aliased, and when the mesh was
 * never closed.  Alpha is printed along a row.
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <d2d1.h>
#include <wincodec.h>
#include <stdio.h>

static ID2D1Factory *factory;
static IWICImagingFactory *wic;
static ID2D1RenderTarget *rt;
static IWICBitmap *bitmap;

static BYTE alpha(unsigned int x, unsigned int y)
{
    WICRect rect = {0, 0, 64, 64};
    IWICBitmapLock *lock;
    UINT size, stride;
    BYTE *data, a;

    IWICBitmap_Lock(bitmap, &rect, WICBitmapLockRead, &lock);
    IWICBitmapLock_GetDataPointer(lock, &size, &data);
    IWICBitmapLock_GetStride(lock, &stride);
    a = data[y * stride + x * 4 + 3];
    IWICBitmapLock_Release(lock);
    return a;
}

static void row(const char *name, unsigned int y, unsigned int x0, unsigned int x1)
{
    unsigned int x;

    printf("%s y %u x %u..%u:", name, y, x0, x1);
    for (x = x0; x <= x1; ++x)
        printf(" %u", alpha(x, y));
    printf("\n");
}

static void triangle(D2D1_TRIANGLE *t, float x1, float y1, float x2, float y2, float x3, float y3)
{
    t->point1.x = x1; t->point1.y = y1;
    t->point2.x = x2; t->point2.y = y2;
    t->point3.x = x3; t->point3.y = y3;
}

static ID2D1Mesh *create_mesh(const D2D1_TRIANGLE *triangles, unsigned int count, BOOL close)
{
    ID2D1TessellationSink *sink;
    ID2D1Mesh *mesh;

    ID2D1RenderTarget_CreateMesh(rt, &mesh);
    ID2D1Mesh_Open(mesh, &sink);
    ID2D1TessellationSink_AddTriangles(sink, triangles, count);
    if (close)
        ID2D1TessellationSink_Close(sink);
    ID2D1TessellationSink_Release(sink);
    return mesh;
}

/* Counts the triangles a tessellation produces, and keeps them. */
struct counting_sink
{
    ID2D1TessellationSink ID2D1TessellationSink_iface;
    ID2D1TessellationSink *inner;
    unsigned int count;
};

static HRESULT STDMETHODCALLTYPE counting_sink_QueryInterface(ID2D1TessellationSink *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_ID2D1TessellationSink) || IsEqualGUID(iid, &IID_IUnknown))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE counting_sink_AddRef(ID2D1TessellationSink *iface)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE counting_sink_Release(ID2D1TessellationSink *iface)
{
    return 1;
}

static void STDMETHODCALLTYPE counting_sink_AddTriangles(ID2D1TessellationSink *iface,
        const D2D1_TRIANGLE *triangles, UINT32 count)
{
    struct counting_sink *sink = CONTAINING_RECORD(iface, struct counting_sink, ID2D1TessellationSink_iface);

    sink->count += count;
    ID2D1TessellationSink_AddTriangles(sink->inner, triangles, count);
}

static HRESULT STDMETHODCALLTYPE counting_sink_Close(ID2D1TessellationSink *iface)
{
    struct counting_sink *sink = CONTAINING_RECORD(iface, struct counting_sink, ID2D1TessellationSink_iface);

    return ID2D1TessellationSink_Close(sink->inner);
}

static const ID2D1TessellationSinkVtbl counting_sink_vtbl =
{
    counting_sink_QueryInterface,
    counting_sink_AddRef,
    counting_sink_Release,
    counting_sink_AddTriangles,
    counting_sink_Close,
};

int main(void)
{
    static const D2D1_MATRIX_3X2_F identity = {{{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f}}};
    static const D2D1_MATRIX_3X2_F scale = {{{2.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f}}};
    D2D1_RENDER_TARGET_PROPERTIES desc = {0};
    D2D1_COLOR_F white = {1.0f, 1.0f, 1.0f, 0.5f}, clear = {0.0f, 0.0f, 0.0f, 0.0f};
    D2D1_ELLIPSE circle = {{48.0f, 48.0f}, 10.3f, 10.3f};
    struct counting_sink counting = {{&counting_sink_vtbl}};
    unsigned int x, y, sum_geometry, sum_mesh, differ;
    ID2D1EllipseGeometry *ellipse;
    ID2D1SolidColorBrush *brush;
    BYTE geometry_alpha[24][24];
    D2D1_TRIANGLE t[12];
    ID2D1Mesh *mesh;
    HRESULT hr;

    CoInitialize(NULL);
    CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&wic);
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory, NULL, (void **)&factory);
    IWICImagingFactory_CreateBitmap(wic, 64, 64, &GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &bitmap);
    desc.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    if (FAILED(hr = ID2D1Factory_CreateWicBitmapRenderTarget(factory, bitmap, &desc, &rt)))
    {
        printf("CreateWicBitmapRenderTarget %#lx\n", hr);
        return 1;
    }
    ID2D1RenderTarget_CreateSolidColorBrush(rt, &white, NULL, &brush);
    ID2D1RenderTarget_SetAntialiasMode(rt, D2D1_ANTIALIAS_MODE_ALIASED);

    /* A square split along its diagonal; two triangles crossing; a triangle with its edges through pixel
     * centres; a triangle each way round; a degenerate triangle. */
    triangle(&t[0], 4.0f, 4.0f, 20.0f, 4.0f, 20.0f, 20.0f);
    triangle(&t[1], 4.0f, 4.0f, 20.0f, 20.0f, 4.0f, 20.0f);
    triangle(&t[2], 24.0f, 4.0f, 40.0f, 4.0f, 32.0f, 20.0f);
    triangle(&t[3], 24.0f, 20.0f, 40.0f, 20.0f, 32.0f, 4.0f);
    triangle(&t[4], 44.5f, 4.5f, 60.5f, 4.5f, 44.5f, 20.5f);
    triangle(&t[5], 4.0f, 24.0f, 4.0f, 40.0f, 20.0f, 24.0f);
    triangle(&t[6], 24.0f, 24.0f, 40.0f, 24.0f, 24.0f, 40.0f);
    triangle(&t[7], 44.0f, 24.0f, 52.0f, 32.0f, 60.0f, 40.0f);
    mesh = create_mesh(t, 8, TRUE);
    ID2D1RenderTarget_BeginDraw(rt);
    ID2D1RenderTarget_Clear(rt, &clear);
    ID2D1RenderTarget_FillMesh(rt, mesh, (ID2D1Brush *)brush);
    /* The same shape again, under a transform. */
    ID2D1RenderTarget_SetTransform(rt, &scale);
    ID2D1Mesh_Release(mesh);
    triangle(&t[0], 2.0f, 22.0f, 10.0f, 22.0f, 2.0f, 30.0f);
    mesh = create_mesh(t, 1, TRUE);
    ID2D1RenderTarget_FillMesh(rt, mesh, (ID2D1Brush *)brush);
    ID2D1RenderTarget_SetTransform(rt, &identity);
    hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL);
    ID2D1Mesh_Release(mesh);
    printf("triangles: %#lx\n", hr);
    row("shared edge", 10, 2, 21);
    row("shared edge", 4, 2, 21);
    row("shared edge", 19, 2, 21);
    row("crossing", 12, 22, 41);
    row("crossing", 6, 22, 41);
    row("centres", 4, 42, 61);
    row("centres", 12, 42, 61);
    row("centres", 20, 42, 61);
    row("windings", 26, 2, 41);
    row("degenerate", 32, 42, 61);
    row("transformed", 45, 2, 21);
    row("transformed", 50, 2, 21);

    /* A circle as a geometry, and as its tessellation. */
    ID2D1Factory_CreateEllipseGeometry(factory, &circle, &ellipse);
    ID2D1RenderTarget_BeginDraw(rt);
    ID2D1RenderTarget_Clear(rt, &clear);
    ID2D1RenderTarget_FillGeometry(rt, (ID2D1Geometry *)ellipse, (ID2D1Brush *)brush, NULL);
    hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL);
    for (y = 0, sum_geometry = 0; y < 24; ++y)
    {
        for (x = 0; x < 24; ++x)
            sum_geometry += geometry_alpha[y][x] = alpha(36 + x, 36 + y);
    }
    ID2D1RenderTarget_CreateMesh(rt, &mesh);
    ID2D1Mesh_Open(mesh, &counting.inner);
    hr = ID2D1EllipseGeometry_Tessellate(ellipse, NULL, D2D1_DEFAULT_FLATTENING_TOLERANCE,
            &counting.ID2D1TessellationSink_iface);
    printf("tessellate: %#lx, %u triangles\n", hr, counting.count);
    ID2D1TessellationSink_Close(counting.inner);
    ID2D1TessellationSink_Release(counting.inner);
    ID2D1RenderTarget_BeginDraw(rt);
    ID2D1RenderTarget_Clear(rt, &clear);
    ID2D1RenderTarget_FillMesh(rt, mesh, (ID2D1Brush *)brush);
    hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL);
    for (y = 0, sum_mesh = differ = 0; y < 24; ++y)
    {
        for (x = 0; x < 24; ++x)
        {
            sum_mesh += alpha(36 + x, 36 + y);
            differ += alpha(36 + x, 36 + y) != geometry_alpha[y][x];
        }
    }
    printf("circle: %#lx, geometry %u, mesh %u, %u pixels differ\n", hr, sum_geometry, sum_mesh, differ);
    row("circle mesh", 48, 36, 59);
    ID2D1Mesh_Release(mesh);
    ID2D1EllipseGeometry_Release(ellipse);

    /* Not aliased. */
    triangle(&t[0], 4.0f, 4.0f, 20.0f, 4.0f, 20.0f, 20.0f);
    mesh = create_mesh(t, 1, TRUE);
    ID2D1RenderTarget_SetAntialiasMode(rt, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    ID2D1RenderTarget_BeginDraw(rt);
    ID2D1RenderTarget_Clear(rt, &clear);
    ID2D1RenderTarget_FillMesh(rt, mesh, (ID2D1Brush *)brush);
    hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL);
    printf("antialiased: %#lx\n", hr);
    row("antialiased", 10, 2, 21);
    ID2D1Mesh_Release(mesh);

    /* Never closed. */
    mesh = create_mesh(t, 1, FALSE);
    ID2D1RenderTarget_SetAntialiasMode(rt, D2D1_ANTIALIAS_MODE_ALIASED);
    ID2D1RenderTarget_BeginDraw(rt);
    ID2D1RenderTarget_Clear(rt, &clear);
    ID2D1RenderTarget_FillMesh(rt, mesh, (ID2D1Brush *)brush);
    hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL);
    printf("open: %#lx\n", hr);
    row("open", 10, 2, 21);
    ID2D1Mesh_Release(mesh);

    /* Empty. */
    mesh = create_mesh(t, 0, TRUE);
    ID2D1RenderTarget_BeginDraw(rt);
    ID2D1RenderTarget_Clear(rt, &clear);
    ID2D1RenderTarget_FillMesh(rt, mesh, (ID2D1Brush *)brush);
    hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL);
    printf("empty: %#lx\n", hr);
    ID2D1Mesh_Release(mesh);

    ID2D1SolidColorBrush_Release(brush);
    ID2D1RenderTarget_Release(rt);
    IWICBitmap_Release(bitmap);
    ID2D1Factory_Release(factory);
    IWICImagingFactory_Release(wic);
    printf("done\n");
    return 0;
}
