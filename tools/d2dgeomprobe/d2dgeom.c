/*
 * What Direct2D's geometry algorithms hand out: the calls a sink gets from CombineWithGeometry, Outline,
 * Widen, Simplify of a group and Stream, and the answers of CompareWithGeometry, ComputeLength,
 * ComputePointAtLength, GetWidenedBounds, ComputeArea and the contains-point tests.
 *
 * Run the same PE on Windows and under Wine and diff the two.  Nothing is read from or written to disk.
 */
#define COBJMACROS
#include <windows.h>
#include <d2d1.h>
#include <stdio.h>
#include <math.h>

static const char *num(char *buf, float v)
{
    if (fabsf(v) < 5e-4f)
        v = 0.0f;
    sprintf(buf, "%.7g", v);
    return buf;
}

static void print_point(const char *prefix, D2D1_POINT_2F p)
{
    char x[32], y[32];

    printf("%s(%s,%s)", prefix, num(x, p.x), num(y, p.y));
}

/* A sink that prints what it is handed. */
struct sink
{
    ID2D1GeometrySink ID2D1GeometrySink_iface;
    LONG refcount;
    unsigned int figures, lines, beziers;
    /* the figure being given: its signed area, from the points it goes through */
    D2D1_POINT_2F start, last;
    double area;
    BOOL quiet;
};

static struct sink *impl_from_sink(ID2D1GeometrySink *iface)
{
    return CONTAINING_RECORD(iface, struct sink, ID2D1GeometrySink_iface);
}

static HRESULT STDMETHODCALLTYPE sink_QueryInterface(ID2D1GeometrySink *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_ID2D1SimplifiedGeometrySink))
    {
        *out = iface;
        ID2D1GeometrySink_AddRef(iface);
        return S_OK;
    }
    if (IsEqualGUID(iid, &IID_ID2D1GeometrySink))
    {
        printf("  QI ID2D1GeometrySink\n");
        *out = iface;
        ID2D1GeometrySink_AddRef(iface);
        return S_OK;
    }
    printf("  QI {%08lx-%04x-%04x-...}\n", iid->Data1, iid->Data2, iid->Data3);
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE sink_AddRef(ID2D1GeometrySink *iface)
{
    return InterlockedIncrement(&impl_from_sink(iface)->refcount);
}

static ULONG STDMETHODCALLTYPE sink_Release(ID2D1GeometrySink *iface)
{
    return InterlockedDecrement(&impl_from_sink(iface)->refcount);
}

static void STDMETHODCALLTYPE sink_SetFillMode(ID2D1GeometrySink *iface, D2D1_FILL_MODE mode)
{
    printf("  SetFillMode %s\n", mode == D2D1_FILL_MODE_WINDING ? "winding" : mode == D2D1_FILL_MODE_ALTERNATE
            ? "alternate" : "?");
}

static void STDMETHODCALLTYPE sink_SetSegmentFlags(ID2D1GeometrySink *iface, D2D1_PATH_SEGMENT flags)
{
    printf("  SetSegmentFlags %#x\n", flags);
}

static void sink_to(struct sink *sink, D2D1_POINT_2F p)
{
    sink->area += (double)sink->last.x * p.y - (double)p.x * sink->last.y;
    sink->last = p;
}

static void STDMETHODCALLTYPE sink_BeginFigure(ID2D1GeometrySink *iface, D2D1_POINT_2F p, D2D1_FIGURE_BEGIN begin)
{
    struct sink *sink = impl_from_sink(iface);

    ++sink->figures;
    sink->start = sink->last = p;
    sink->area = 0.0;
    if (sink->quiet)
        return;
    print_point("  BeginFigure ", p);
    printf(" %s\n", begin == D2D1_FIGURE_BEGIN_FILLED ? "filled" : "hollow");
}

static void STDMETHODCALLTYPE sink_AddLines(ID2D1GeometrySink *iface, const D2D1_POINT_2F *points, UINT32 count)
{
    struct sink *sink = impl_from_sink(iface);
    UINT32 i;

    if (!sink->quiet)
        printf("  AddLines %u", count);
    for (i = 0; i < count; ++i)
    {
        if (!sink->quiet)
            print_point(" ", points[i]);
        sink_to(sink, points[i]);
    }
    sink->lines += count;
    if (!sink->quiet)
        printf("\n");
}

static void STDMETHODCALLTYPE sink_AddBeziers(ID2D1GeometrySink *iface, const D2D1_BEZIER_SEGMENT *b, UINT32 count)
{
    struct sink *sink = impl_from_sink(iface);
    UINT32 i;

    if (!sink->quiet)
        printf("  AddBeziers %u", count);
    for (i = 0; i < count; ++i)
    {
        if (!sink->quiet)
        {
            print_point(" [", b[i].point1);
            print_point(" ", b[i].point2);
            print_point(" ", b[i].point3);
            printf("]");
        }
        sink_to(sink, b[i].point3);
    }
    sink->beziers += count;
    if (!sink->quiet)
        printf("\n");
}

static void STDMETHODCALLTYPE sink_EndFigure(ID2D1GeometrySink *iface, D2D1_FIGURE_END end)
{
    struct sink *sink = impl_from_sink(iface);
    char a[32];

    sink_to(sink, sink->start);
    if (sink->quiet)
        return;
    printf("  EndFigure %s, signed area through its points %s\n", end == D2D1_FIGURE_END_CLOSED ? "closed" : "open",
            num(a, sink->area / 2.0));
}

static HRESULT STDMETHODCALLTYPE sink_Close(ID2D1GeometrySink *iface)
{
    printf("  Close\n");
    return S_OK;
}

static void STDMETHODCALLTYPE sink_AddLine(ID2D1GeometrySink *iface, D2D1_POINT_2F p)
{
    struct sink *sink = impl_from_sink(iface);

    print_point("  AddLine ", p);
    printf("\n");
    sink_to(sink, p);
}

static void STDMETHODCALLTYPE sink_AddBezier(ID2D1GeometrySink *iface, const D2D1_BEZIER_SEGMENT *b)
{
    struct sink *sink = impl_from_sink(iface);

    print_point("  AddBezier ", b->point1);
    print_point(" ", b->point2);
    print_point(" ", b->point3);
    printf("\n");
    sink_to(sink, b->point3);
}

static void STDMETHODCALLTYPE sink_AddQuadraticBezier(ID2D1GeometrySink *iface, const D2D1_QUADRATIC_BEZIER_SEGMENT *b)
{
    struct sink *sink = impl_from_sink(iface);

    print_point("  AddQuadraticBezier ", b->point1);
    print_point(" ", b->point2);
    printf("\n");
    sink_to(sink, b->point2);
}

static void STDMETHODCALLTYPE sink_AddQuadraticBeziers(ID2D1GeometrySink *iface,
        const D2D1_QUADRATIC_BEZIER_SEGMENT *b, UINT32 count)
{
    struct sink *sink = impl_from_sink(iface);
    UINT32 i;

    printf("  AddQuadraticBeziers %u", count);
    for (i = 0; i < count; ++i)
    {
        print_point(" [", b[i].point1);
        print_point(" ", b[i].point2);
        printf("]");
        sink_to(sink, b[i].point2);
    }
    printf("\n");
}

static void STDMETHODCALLTYPE sink_AddArc(ID2D1GeometrySink *iface, const D2D1_ARC_SEGMENT *arc)
{
    struct sink *sink = impl_from_sink(iface);
    char a[32], b[32], c[32];

    print_point("  AddArc ", arc->point);
    printf(" size %s,%s rotation %s sweep %u arc %u\n", num(a, arc->size.width), num(b, arc->size.height),
            num(c, arc->rotationAngle), arc->sweepDirection, arc->arcSize);
    sink_to(sink, arc->point);
}

static const ID2D1GeometrySinkVtbl sink_vtbl =
{
    sink_QueryInterface,
    sink_AddRef,
    sink_Release,
    sink_SetFillMode,
    sink_SetSegmentFlags,
    sink_BeginFigure,
    sink_AddLines,
    sink_AddBeziers,
    sink_EndFigure,
    sink_Close,
    sink_AddLine,
    sink_AddBezier,
    sink_AddQuadraticBezier,
    sink_AddQuadraticBeziers,
    sink_AddArc,
};

static void sink_init(struct sink *sink, BOOL quiet)
{
    memset(sink, 0, sizeof(*sink));
    sink->ID2D1GeometrySink_iface.lpVtbl = &sink_vtbl;
    sink->refcount = 1;
    sink->quiet = quiet;
}

static ID2D1SimplifiedGeometrySink *simplified(struct sink *sink)
{
    return (ID2D1SimplifiedGeometrySink *)&sink->ID2D1GeometrySink_iface;
}

static void sink_summary(struct sink *sink)
{
    printf("  -> %u figures, %u line points, %u beziers, refcount %ld\n", sink->figures, sink->lines,
            sink->beziers, sink->refcount);
}

static ID2D1Factory *factory;

static ID2D1Geometry *rect_geometry(float l, float t, float r, float b)
{
    D2D1_RECT_F rect = {l, t, r, b};
    ID2D1RectangleGeometry *g;
    HRESULT hr;

    if (FAILED(hr = ID2D1Factory_CreateRectangleGeometry(factory, &rect, &g)))
    {
        printf("CreateRectangleGeometry %#lx\n", hr);
        exit(1);
    }
    return (ID2D1Geometry *)g;
}

static ID2D1Geometry *ellipse_geometry(float x, float y, float rx, float ry)
{
    D2D1_ELLIPSE ellipse = {{x, y}, rx, ry};
    ID2D1EllipseGeometry *g;
    HRESULT hr;

    if (FAILED(hr = ID2D1Factory_CreateEllipseGeometry(factory, &ellipse, &g)))
    {
        printf("CreateEllipseGeometry %#lx\n", hr);
        exit(1);
    }
    return (ID2D1Geometry *)g;
}

static ID2D1Geometry *rounded_rect_geometry(float l, float t, float r, float b, float rx, float ry)
{
    D2D1_ROUNDED_RECT rect = {{l, t, r, b}, rx, ry};
    ID2D1RoundedRectangleGeometry *g;
    HRESULT hr;

    if (FAILED(hr = ID2D1Factory_CreateRoundedRectangleGeometry(factory, &rect, &g)))
    {
        printf("CreateRoundedRectangleGeometry %#lx\n", hr);
        exit(1);
    }
    return (ID2D1Geometry *)g;
}

/* figures: count of points, then the points; a count of 0 ends; flags per figure: 1 closed, 2 hollow */
static ID2D1Geometry *path_geometry(D2D1_FILL_MODE mode, const float *data, const unsigned int *flags)
{
    ID2D1PathGeometry *path;
    ID2D1GeometrySink *sink;
    unsigned int count, i, figure = 0;
    D2D1_POINT_2F p;
    HRESULT hr;

    ID2D1Factory_CreatePathGeometry(factory, &path);
    ID2D1PathGeometry_Open(path, &sink);
    ID2D1GeometrySink_SetFillMode(sink, mode);
    while ((count = (unsigned int)*data++))
    {
        p.x = *data++;
        p.y = *data++;
        ID2D1GeometrySink_BeginFigure(sink, p, flags[figure] & 2 ? D2D1_FIGURE_BEGIN_HOLLOW : D2D1_FIGURE_BEGIN_FILLED);
        for (i = 1; i < count; ++i)
        {
            p.x = *data++;
            p.y = *data++;
            ID2D1GeometrySink_AddLine(sink, p);
        }
        ID2D1GeometrySink_EndFigure(sink, flags[figure] & 1 ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
        ++figure;
    }
    if (FAILED(hr = ID2D1GeometrySink_Close(sink)))
        printf("path Close %#lx\n", hr);
    ID2D1GeometrySink_Release(sink);
    return (ID2D1Geometry *)path;
}

/* the result of an operation as a path, to ask it about its area and what it covers */
static ID2D1Geometry *open_result(ID2D1GeometrySink **sink)
{
    ID2D1PathGeometry *path;

    ID2D1Factory_CreatePathGeometry(factory, &path);
    ID2D1PathGeometry_Open(path, sink);
    return (ID2D1Geometry *)path;
}

static void describe(ID2D1Geometry *g, const D2D1_POINT_2F *points, unsigned int count)
{
    char a[32];
    float area = -1.0f;
    BOOL contains;
    unsigned int i;
    HRESULT hr;

    hr = ID2D1Geometry_ComputeArea(g, NULL, 0.25f, &area);
    printf("  area hr %#lx %s; covers", hr, num(a, area));
    for (i = 0; i < count; ++i)
    {
        contains = -1;
        hr = ID2D1Geometry_FillContainsPoint(g, points[i], NULL, 0.25f, &contains);
        print_point(" ", points[i]);
        printf("=%d", SUCCEEDED(hr) ? contains : -(int)hr);
    }
    printf("\n");
}

static const char *mode_name(D2D1_COMBINE_MODE mode)
{
    static const char *names[] = {"union", "intersect", "xor", "exclude"};
    return mode < 4 ? names[mode] : "?";
}

static void combine(const char *name, ID2D1Geometry *a, ID2D1Geometry *b, D2D1_COMBINE_MODE mode,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, const D2D1_POINT_2F *points, unsigned int count)
{
    ID2D1GeometrySink *path_sink;
    ID2D1Geometry *result;
    struct sink sink;
    HRESULT hr;

    printf("%s %s:\n", name, mode_name(mode));
    sink_init(&sink, FALSE);
    hr = ID2D1Geometry_CombineWithGeometry(a, b, mode, transform, tolerance, simplified(&sink));
    printf("  hr %#lx\n", hr);
    sink_summary(&sink);

    result = open_result(&path_sink);
    hr = ID2D1Geometry_CombineWithGeometry(a, b, mode, transform, tolerance, (ID2D1SimplifiedGeometrySink *)path_sink);
    if (SUCCEEDED(hr))
        hr = ID2D1GeometrySink_Close(path_sink);
    ID2D1GeometrySink_Release(path_sink);
    if (SUCCEEDED(hr))
        describe(result, points, count);
    else
        printf("  as path hr %#lx\n", hr);
    ID2D1Geometry_Release(result);
}

static void outline(const char *name, ID2D1Geometry *g, const D2D1_MATRIX_3X2_F *transform,
        const D2D1_POINT_2F *points, unsigned int count)
{
    ID2D1GeometrySink *path_sink;
    ID2D1Geometry *result;
    struct sink sink;
    HRESULT hr;

    printf("outline %s:\n", name);
    sink_init(&sink, FALSE);
    hr = ID2D1Geometry_Outline(g, transform, 0.25f, simplified(&sink));
    printf("  hr %#lx\n", hr);
    sink_summary(&sink);

    result = open_result(&path_sink);
    hr = ID2D1Geometry_Outline(g, transform, 0.25f, (ID2D1SimplifiedGeometrySink *)path_sink);
    if (SUCCEEDED(hr))
        hr = ID2D1GeometrySink_Close(path_sink);
    ID2D1GeometrySink_Release(path_sink);
    if (SUCCEEDED(hr))
        describe(result, points, count);
    ID2D1Geometry_Release(result);
}

static void compare(const char *name, ID2D1Geometry *a, ID2D1Geometry *b, const D2D1_MATRIX_3X2_F *transform,
        float tolerance)
{
    D2D1_GEOMETRY_RELATION relation = 0xdead;
    HRESULT hr;

    hr = ID2D1Geometry_CompareWithGeometry(a, b, transform, tolerance, &relation);
    printf("compare %s: hr %#lx relation %d\n", name, hr, relation);
}

static void widen(const char *name, ID2D1Geometry *g, float width, ID2D1StrokeStyle *style,
        const D2D1_MATRIX_3X2_F *transform, const D2D1_POINT_2F *points, unsigned int count)
{
    ID2D1GeometrySink *path_sink;
    ID2D1Geometry *result;
    D2D1_RECT_F bounds = {-1, -1, -1, -1};
    char a[32], b[32], c[32], d[32];
    struct sink sink;
    HRESULT hr;

    printf("widen %s:\n", name);
    sink_init(&sink, FALSE);
    hr = ID2D1Geometry_Widen(g, width, style, transform, 0.25f, simplified(&sink));
    printf("  hr %#lx\n", hr);
    sink_summary(&sink);

    result = open_result(&path_sink);
    hr = ID2D1Geometry_Widen(g, width, style, transform, 0.25f, (ID2D1SimplifiedGeometrySink *)path_sink);
    if (SUCCEEDED(hr))
        hr = ID2D1GeometrySink_Close(path_sink);
    ID2D1GeometrySink_Release(path_sink);
    if (SUCCEEDED(hr))
        describe(result, points, count);
    ID2D1Geometry_Release(result);

    hr = ID2D1Geometry_GetWidenedBounds(g, width, style, transform, 0.25f, &bounds);
    printf("  widened bounds hr %#lx {%s, %s, %s, %s}\n", hr, num(a, bounds.left), num(b, bounds.top),
            num(c, bounds.right), num(d, bounds.bottom));
}

static void measure(const char *name, ID2D1Geometry *g, const D2D1_MATRIX_3X2_F *transform,
        const float *lengths, unsigned int count)
{
    D2D1_POINT_2F point, tangent;
    float length = -1.0f;
    unsigned int i;
    char a[32];
    HRESULT hr;

    hr = ID2D1Geometry_ComputeLength(g, transform, 0.25f, &length);
    printf("length %s: hr %#lx %s\n", name, hr, num(a, length));
    for (i = 0; i < count; ++i)
    {
        point.x = point.y = tangent.x = tangent.y = -99.0f;
        hr = ID2D1Geometry_ComputePointAtLength(g, lengths[i], transform, 0.25f, &point, &tangent);
        printf("  at %s: hr %#lx", num(a, lengths[i]), hr);
        print_point(" point ", point);
        print_point(" tangent ", tangent);
        printf("\n");
    }
    point.x = -99.0f;
    hr = ID2D1Geometry_ComputePointAtLength(g, 1.0f, transform, 0.25f, &point, NULL);
    print_point("  no tangent: ", point);
    printf(" hr %#lx\n", hr);
    tangent.x = -99.0f;
    hr = ID2D1Geometry_ComputePointAtLength(g, 1.0f, transform, 0.25f, NULL, &tangent);
    print_point("  no point: ", tangent);
    printf(" hr %#lx\n", hr);
}

static void stream(const char *name, ID2D1Geometry *g)
{
    ID2D1PathGeometry *path;
    struct sink sink;
    HRESULT hr;

    if (FAILED(ID2D1Geometry_QueryInterface(g, &IID_ID2D1PathGeometry, (void **)&path)))
        return;
    printf("stream %s:\n", name);
    sink_init(&sink, FALSE);
    hr = ID2D1PathGeometry_Stream(path, &sink.ID2D1GeometrySink_iface);
    printf("  hr %#lx\n", hr);
    sink_summary(&sink);
    ID2D1PathGeometry_Release(path);
}

static void simplify(const char *name, ID2D1Geometry *g, D2D1_GEOMETRY_SIMPLIFICATION_OPTION option)
{
    struct sink sink;
    HRESULT hr;

    printf("simplify %s %s:\n", name, option == D2D1_GEOMETRY_SIMPLIFICATION_OPTION_LINES ? "lines" : "cubics");
    sink_init(&sink, FALSE);
    hr = ID2D1Geometry_Simplify(g, option, NULL, 0.25f, simplified(&sink));
    printf("  hr %#lx\n", hr);
    sink_summary(&sink);
}

static void contains(const char *name, ID2D1Geometry *g, float width, const D2D1_POINT_2F *points, unsigned int count)
{
    BOOL fill, stroke;
    HRESULT hr, hr2;
    unsigned int i;

    printf("contains %s:", name);
    for (i = 0; i < count; ++i)
    {
        fill = stroke = -1;
        hr = ID2D1Geometry_FillContainsPoint(g, points[i], NULL, 0.25f, &fill);
        hr2 = ID2D1Geometry_StrokeContainsPoint(g, points[i], width, NULL, NULL, 0.25f, &stroke);
        print_point(" ", points[i]);
        printf(" fill %d stroke %d", SUCCEEDED(hr) ? fill : -(int)hr, SUCCEEDED(hr2) ? stroke : -(int)hr2);
    }
    printf("\n");
}

struct tessellation_sink
{
    ID2D1TessellationSink ID2D1TessellationSink_iface;
    unsigned int triangles;
    double area;
};

static HRESULT STDMETHODCALLTYPE tessellation_sink_QueryInterface(ID2D1TessellationSink *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_ID2D1TessellationSink))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE tessellation_sink_AddRef(ID2D1TessellationSink *iface)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE tessellation_sink_Release(ID2D1TessellationSink *iface)
{
    return 1;
}

static void STDMETHODCALLTYPE tessellation_sink_AddTriangles(ID2D1TessellationSink *iface,
        const D2D1_TRIANGLE *triangles, UINT32 count)
{
    struct tessellation_sink *sink = CONTAINING_RECORD(iface, struct tessellation_sink, ID2D1TessellationSink_iface);
    const D2D1_TRIANGLE *t;
    UINT32 i;

    for (i = 0; i < count; ++i)
    {
        t = &triangles[i];
        sink->area += fabs(((double)t->point2.x - t->point1.x) * ((double)t->point3.y - t->point1.y)
                - ((double)t->point3.x - t->point1.x) * ((double)t->point2.y - t->point1.y)) / 2.0;
    }
    sink->triangles += count;
}

static HRESULT STDMETHODCALLTYPE tessellation_sink_Close(ID2D1TessellationSink *iface)
{
    return S_OK;
}

static const ID2D1TessellationSinkVtbl tessellation_sink_vtbl =
{
    tessellation_sink_QueryInterface,
    tessellation_sink_AddRef,
    tessellation_sink_Release,
    tessellation_sink_AddTriangles,
    tessellation_sink_Close,
};

static void tessellate(const char *name, ID2D1Geometry *g)
{
    struct tessellation_sink sink = {{&tessellation_sink_vtbl}};
    char a[32];
    HRESULT hr;

    hr = ID2D1Geometry_Tessellate(g, NULL, 0.25f, &sink.ID2D1TessellationSink_iface);
    printf("tessellate %s: hr %#lx, area %s\n", name, hr, num(a, sink.area));
}

int main(void)
{
    static const D2D1_POINT_2F rect_points[] =
    {
        {2.0f, 2.0f}, {7.0f, 7.0f}, {12.0f, 12.0f}, {12.0f, 2.0f}, {2.0f, 12.0f}, {20.0f, 20.0f},
    };
    static const D2D1_POINT_2F star_points[] =
    {
        {50.0f, 30.0f}, {50.0f, 50.0f}, {50.0f, 10.0f}, {20.0f, 45.0f}, {0.0f, 0.0f},
    };
    static const D2D1_POINT_2F ring_points[] = {{5.0f, 5.0f}, {15.0f, 15.0f}, {25.0f, 25.0f}};
    static const D2D1_POINT_2F line_points[] = {{5.0f, 0.0f}, {5.0f, 0.9f}, {5.0f, 1.1f}, {-0.5f, 0.0f}, {10.5f, 0.0f}};
    static const float star[] = {5, 50,10, 61.76f,46.18f, 31,23.5f, 69,23.5f, 38.24f,46.18f, 0};
    static const float bowtie[] = {4, 0,0, 10,10, 10,0, 0,10, 0};
    static const float nested[] = {4, 0,0, 30,0, 30,30, 0,30, 4, 10,10, 20,10, 20,20, 10,20, 0};
    static const float nested_reversed[] = {4, 0,0, 30,0, 30,30, 0,30, 4, 10,10, 10,20, 20,20, 20,10, 0};
    static const float hollow[] = {4, 0,0, 10,0, 10,10, 0,10, 3, 20,0, 30,0, 30,10, 0};
    static const float open_triangle[] = {3, 0,0, 10,0, 10,10, 0};
    static const float line[] = {2, 0,0, 10,0, 0};
    static const float polyline[] = {3, 0,0, 10,0, 10,10, 0};
    static const unsigned int closed[] = {1, 1}, open[] = {0, 0}, hollow_flags[] = {1, 2};
    static const float rect_lengths[] = {0.0f, 5.0f, 15.0f, 39.0f, 40.0f, 45.0f, -1.0f};
    static const float ellipse_lengths[] = {0.0f, 10.0f, 62.0f, 70.0f};
    static const float line_lengths[] = {0.0f, 2.5f, 10.0f, 12.0f, -3.0f};
    static const float polyline_lengths[] = {5.0f, 10.0f, 15.0f, 25.0f};
    D2D1_MATRIX_3X2_F translate = {{{1.0f, 0.0f, 0.0f, 1.0f, 20.0f, 0.0f}}};
    D2D1_MATRIX_3X2_F scale = {{{2.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f}}};
    D2D1_MATRIX_3X2_F shift = {{{1.0f, 0.0f, 0.0f, 1.0f, 5.0f, 5.0f}}};
    D2D1_STROKE_STYLE_PROPERTIES props = {0};
    ID2D1Geometry *a, *b, *c, *e, *group, *groups[2], *transformed;
    ID2D1StrokeStyle *square_caps, *round_joins;
    D2D1_COMBINE_MODE mode;
    HRESULT hr;

    if (FAILED(hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory, NULL, (void **)&factory)))
    {
        printf("D2D1CreateFactory %#lx\n", hr);
        return 1;
    }

    a = rect_geometry(0.0f, 0.0f, 10.0f, 10.0f);
    b = rect_geometry(5.0f, 5.0f, 15.0f, 15.0f);
    for (mode = D2D1_COMBINE_MODE_UNION; mode <= D2D1_COMBINE_MODE_EXCLUDE; ++mode)
        combine("rects", a, b, mode, NULL, 0.25f, rect_points, ARRAY_SIZE(rect_points));
    combine("rects", a, b, 4, NULL, 0.25f, rect_points, ARRAY_SIZE(rect_points));
    combine("rects, b moved clear", a, b, D2D1_COMBINE_MODE_UNION, &translate, 0.25f, rect_points,
            ARRAY_SIZE(rect_points));
    combine("rects, b moved clear", a, b, D2D1_COMBINE_MODE_INTERSECT, &translate, 0.25f, rect_points,
            ARRAY_SIZE(rect_points));
    combine("rects, b scaled", a, b, D2D1_COMBINE_MODE_INTERSECT, &scale, 0.25f, rect_points,
            ARRAY_SIZE(rect_points));
    combine("rects, tolerance 0", a, b, D2D1_COMBINE_MODE_UNION, NULL, 0.0f, rect_points, ARRAY_SIZE(rect_points));
    combine("rect with itself", a, a, D2D1_COMBINE_MODE_UNION, NULL, 0.25f, rect_points, ARRAY_SIZE(rect_points));
    combine("rect with itself", a, a, D2D1_COMBINE_MODE_XOR, NULL, 0.25f, rect_points, ARRAY_SIZE(rect_points));

    e = ellipse_geometry(0.0f, 0.0f, 10.0f, 10.0f);
    combine("ellipse and rect", e, a, D2D1_COMBINE_MODE_UNION, NULL, 0.25f, rect_points, ARRAY_SIZE(rect_points));
    combine("ellipse and rect", e, a, D2D1_COMBINE_MODE_INTERSECT, NULL, 0.25f, rect_points, ARRAY_SIZE(rect_points));
    combine("ellipse and rect, big tolerance", e, a, D2D1_COMBINE_MODE_EXCLUDE, NULL, 5.0f, rect_points,
            ARRAY_SIZE(rect_points));

    c = path_geometry(D2D1_FILL_MODE_ALTERNATE, nested, closed);
    combine("nested alternate and rect", c, b, D2D1_COMBINE_MODE_UNION, NULL, 0.25f, ring_points,
            ARRAY_SIZE(ring_points));
    ID2D1Geometry_Release(c);
    c = path_geometry(D2D1_FILL_MODE_ALTERNATE, hollow, hollow_flags);
    combine("hollow figure and rect", c, b, D2D1_COMBINE_MODE_UNION, NULL, 0.25f, rect_points,
            ARRAY_SIZE(rect_points));
    ID2D1Geometry_Release(c);
    c = path_geometry(D2D1_FILL_MODE_ALTERNATE, open_triangle, open);
    combine("open triangle and rect", c, b, D2D1_COMBINE_MODE_UNION, NULL, 0.25f, rect_points,
            ARRAY_SIZE(rect_points));
    ID2D1Geometry_Release(c);

    outline("rect", a, NULL, rect_points, ARRAY_SIZE(rect_points));
    outline("rect scaled", a, &scale, rect_points, ARRAY_SIZE(rect_points));
    outline("ellipse", e, NULL, rect_points, ARRAY_SIZE(rect_points));
    c = path_geometry(D2D1_FILL_MODE_ALTERNATE, star, closed);
    outline("star alternate", c, NULL, star_points, ARRAY_SIZE(star_points));
    ID2D1Geometry_Release(c);
    c = path_geometry(D2D1_FILL_MODE_WINDING, star, closed);
    outline("star winding", c, NULL, star_points, ARRAY_SIZE(star_points));
    ID2D1Geometry_Release(c);
    c = path_geometry(D2D1_FILL_MODE_ALTERNATE, bowtie, closed);
    outline("bowtie", c, NULL, rect_points, ARRAY_SIZE(rect_points));
    ID2D1Geometry_Release(c);
    c = path_geometry(D2D1_FILL_MODE_WINDING, nested, closed);
    outline("nested winding", c, NULL, ring_points, ARRAY_SIZE(ring_points));
    ID2D1Geometry_Release(c);
    c = path_geometry(D2D1_FILL_MODE_WINDING, nested_reversed, closed);
    outline("nested reversed winding", c, NULL, ring_points, ARRAY_SIZE(ring_points));
    ID2D1Geometry_Release(c);
    c = path_geometry(D2D1_FILL_MODE_ALTERNATE, hollow, hollow_flags);
    outline("hollow figure", c, NULL, rect_points, ARRAY_SIZE(rect_points));
    ID2D1Geometry_Release(c);

    compare("identical", a, a, NULL, 0.25f);
    compare("overlap", a, b, NULL, 0.25f);
    compare("clear", a, b, &translate, 0.25f);
    c = rect_geometry(2.0f, 2.0f, 8.0f, 8.0f);
    compare("contains", a, c, NULL, 0.25f);
    compare("is contained", c, a, NULL, 0.25f);
    ID2D1Geometry_Release(c);
    c = rect_geometry(10.0f, 0.0f, 20.0f, 10.0f);
    compare("edge touching", a, c, NULL, 0.25f);
    ID2D1Geometry_Release(c);
    c = rect_geometry(10.0f, 10.0f, 20.0f, 20.0f);
    compare("corner touching", a, c, NULL, 0.25f);
    ID2D1Geometry_Release(c);
    c = rect_geometry(0.0f, 0.0f, 10.0f, 10.1f);
    compare("almost identical", a, c, NULL, 0.25f);
    compare("almost identical, tolerance 1", a, c, NULL, 1.0f);
    ID2D1Geometry_Release(c);
    c = ellipse_geometry(5.0f, 5.0f, 5.0f, 5.0f);
    compare("inscribed ellipse", a, c, NULL, 0.25f);
    compare("rect around ellipse", c, a, NULL, 0.25f);
    ID2D1Geometry_Release(c);
    compare("rect and itself moved half", a, a, &shift, 0.25f);

    groups[0] = a;
    groups[1] = b;
    ID2D1Factory_CreateGeometryGroup(factory, D2D1_FILL_MODE_ALTERNATE, groups, 2, (ID2D1GeometryGroup **)&group);
    simplify("group alternate", group, D2D1_GEOMETRY_SIMPLIFICATION_OPTION_LINES);
    outline("group alternate", group, NULL, rect_points, ARRAY_SIZE(rect_points));
    contains("group alternate", group, 1.0f, rect_points, ARRAY_SIZE(rect_points));
    tessellate("group alternate", group);
    ID2D1Geometry_Release(group);
    groups[1] = e;
    ID2D1Factory_CreateGeometryGroup(factory, D2D1_FILL_MODE_WINDING, groups, 2, (ID2D1GeometryGroup **)&group);
    simplify("group winding, rect and ellipse", group, D2D1_GEOMETRY_SIMPLIFICATION_OPTION_CUBICS_AND_LINES);
    outline("group winding, rect and ellipse", group, NULL, rect_points, ARRAY_SIZE(rect_points));
    combine("group and rect", group, b, D2D1_COMBINE_MODE_INTERSECT, NULL, 0.25f, rect_points,
            ARRAY_SIZE(rect_points));
    compare("group and rect", group, b, NULL, 0.25f);
    measure("group winding", group, NULL, rect_lengths, ARRAY_SIZE(rect_lengths));
    ID2D1Geometry_Release(group);

    measure("rect", a, NULL, rect_lengths, ARRAY_SIZE(rect_lengths));
    measure("rect scaled", a, &scale, rect_lengths, ARRAY_SIZE(rect_lengths));
    measure("ellipse", e, NULL, ellipse_lengths, ARRAY_SIZE(ellipse_lengths));
    c = path_geometry(D2D1_FILL_MODE_ALTERNATE, line, open);
    measure("line", c, NULL, line_lengths, ARRAY_SIZE(line_lengths));
    ID2D1Geometry_Release(c);
    c = path_geometry(D2D1_FILL_MODE_ALTERNATE, polyline, open);
    measure("open polyline", c, NULL, polyline_lengths, ARRAY_SIZE(polyline_lengths));
    ID2D1Geometry_Release(c);
    c = path_geometry(D2D1_FILL_MODE_ALTERNATE, hollow, hollow_flags);
    measure("filled and hollow", c, NULL, rect_lengths, ARRAY_SIZE(rect_lengths));
    ID2D1Geometry_Release(c);
    c = rounded_rect_geometry(0.0f, 0.0f, 20.0f, 10.0f, 2.0f, 2.0f);
    measure("rounded rect", c, NULL, rect_lengths, ARRAY_SIZE(rect_lengths));
    contains("rounded rect", c, 2.0f, rect_points, ARRAY_SIZE(rect_points));
    ID2D1Geometry_Release(c);
    ID2D1Factory_CreateTransformedGeometry(factory, a, &scale, (ID2D1TransformedGeometry **)&transformed);
    measure("transformed rect", transformed, NULL, rect_lengths, ARRAY_SIZE(rect_lengths));
    combine("transformed rect and rect", transformed, b, D2D1_COMBINE_MODE_EXCLUDE, NULL, 0.25f, rect_points,
            ARRAY_SIZE(rect_points));
    ID2D1Geometry_Release(transformed);

    contains("ellipse", e, 2.0f, rect_points, ARRAY_SIZE(rect_points));

    props.startCap = props.endCap = D2D1_CAP_STYLE_SQUARE;
    ID2D1Factory_CreateStrokeStyle(factory, &props, NULL, 0, &square_caps);
    props.startCap = props.endCap = D2D1_CAP_STYLE_FLAT;
    props.lineJoin = D2D1_LINE_JOIN_ROUND;
    ID2D1Factory_CreateStrokeStyle(factory, &props, NULL, 0, &round_joins);
    c = path_geometry(D2D1_FILL_MODE_ALTERNATE, line, open);
    widen("line", c, 2.0f, NULL, NULL, line_points, ARRAY_SIZE(line_points));
    widen("line, square caps", c, 2.0f, square_caps, NULL, line_points, ARRAY_SIZE(line_points));
    widen("line scaled", c, 2.0f, NULL, &scale, line_points, ARRAY_SIZE(line_points));
    ID2D1Geometry_Release(c);
    widen("rect", a, 2.0f, NULL, NULL, rect_points, ARRAY_SIZE(rect_points));
    widen("rect, round joins", a, 2.0f, round_joins, NULL, rect_points, ARRAY_SIZE(rect_points));
    c = path_geometry(D2D1_FILL_MODE_ALTERNATE, polyline, open);
    widen("open polyline", c, 2.0f, NULL, NULL, rect_points, ARRAY_SIZE(rect_points));
    ID2D1Geometry_Release(c);
    ID2D1StrokeStyle_Release(square_caps);
    ID2D1StrokeStyle_Release(round_joins);

    c = path_geometry(D2D1_FILL_MODE_WINDING, hollow, hollow_flags);
    stream("filled and hollow", c);
    ID2D1Geometry_Release(c);
    {
        ID2D1PathGeometry *path;
        ID2D1GeometrySink *sink;
        D2D1_POINT_2F p = {0.0f, 0.0f};
        D2D1_BEZIER_SEGMENT bezier = {{1.0f, 5.0f}, {9.0f, 5.0f}, {10.0f, 0.0f}};
        D2D1_QUADRATIC_BEZIER_SEGMENT quad = {{15.0f, -5.0f}, {20.0f, 0.0f}};
        D2D1_ARC_SEGMENT arc = {{30.0f, 0.0f}, {5.0f, 5.0f}, 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL};

        ID2D1Factory_CreatePathGeometry(factory, &path);
        ID2D1PathGeometry_Open(path, &sink);
        ID2D1GeometrySink_BeginFigure(sink, p, D2D1_FIGURE_BEGIN_HOLLOW);
        ID2D1GeometrySink_AddBezier(sink, &bezier);
        ID2D1GeometrySink_AddQuadraticBezier(sink, &quad);
        ID2D1GeometrySink_AddArc(sink, &arc);
        p.x = 30.0f;
        p.y = 10.0f;
        ID2D1GeometrySink_AddLine(sink, p);
        ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_OPEN);
        ID2D1GeometrySink_Close(sink);
        ID2D1GeometrySink_Release(sink);
        stream("curves", (ID2D1Geometry *)path);
        measure("curves", (ID2D1Geometry *)path, NULL, line_lengths, ARRAY_SIZE(line_lengths));
        ID2D1PathGeometry_Release(path);

        {
            static const struct
            {
                const char *name;
                D2D1_POINT_2F start;
                D2D1_ARC_SEGMENT arc;
            }
            arcs[] =
            {
                {"half circle", {100.0f, 100.0f},
                        {{200.0f, 100.0f}, {50.0f, 50.0f}, 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL}},
                {"three quarters", {100.0f, 300.0f},
                        {{150.0f, 350.0f}, {50.0f, 50.0f}, 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_LARGE}},
                {"quarter", {0.0f, 0.0f},
                        {{10.0f, 10.0f}, {10.0f, 10.0f}, 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL}},
                {"tenth of an ellipse, turned", {0.0f, 0.0f},
                        {{10.0f, 3.0f}, {20.0f, 10.0f}, 30.0f, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                        D2D1_ARC_SIZE_SMALL}},
            };
            unsigned int i;

            for (i = 0; i < ARRAY_SIZE(arcs); ++i)
            {
                D2D1_RECT_F bounds;
                char a[32], b[32], c[32], d[32];

                ID2D1Factory_CreatePathGeometry(factory, &path);
                ID2D1PathGeometry_Open(path, &sink);
                ID2D1GeometrySink_BeginFigure(sink, arcs[i].start, D2D1_FIGURE_BEGIN_FILLED);
                ID2D1GeometrySink_AddArc(sink, &arcs[i].arc);
                ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_OPEN);
                ID2D1GeometrySink_Close(sink);
                ID2D1GeometrySink_Release(sink);
                simplify(arcs[i].name, (ID2D1Geometry *)path, D2D1_GEOMETRY_SIMPLIFICATION_OPTION_CUBICS_AND_LINES);
                hr = ID2D1PathGeometry_GetBounds(path, NULL, &bounds);
                printf("  bounds hr %#lx {%s, %s, %s, %s}\n", hr, num(a, bounds.left), num(b, bounds.top),
                        num(c, bounds.right), num(d, bounds.bottom));
                ID2D1PathGeometry_Release(path);
            }
        }

        {
            /* a quarter of a circle, cut into chords of different lengths depending on its size */
            static const struct {float r, l[3];} quarters[] =
            {
                {0.5f, {0.176777f, 0.353553f, 0.53033f}},
                {1.0f, {0.191342f, 0.382683f, 1.14805f}},
                {3.0f, {0.297824f, 0.595648f, 4.087098f}},
                {10.0f, {0.506391f, 1.012782f, 14.636445f}},
                {40.0f, {1.023745f, 2.047489f, 60.712794f}},
                {160.0f, {2.059053f, 4.118105f, 247.16416f}},
            };
            static const float k = 0.5522847498f;
            D2D1_POINT_2F point, tangent;
            char a[32], b[32];
            unsigned int i, j;

            for (i = 0; i < ARRAY_SIZE(quarters); ++i)
            {
                float r = quarters[i].r;
                D2D1_BEZIER_SEGMENT q = {{0.0f, r - r * k}, {r - r * k, 0.0f}, {r, 0.0f}};

                ID2D1Factory_CreatePathGeometry(factory, &path);
                ID2D1PathGeometry_Open(path, &sink);
                p.x = 0.0f;
                p.y = r;
                ID2D1GeometrySink_BeginFigure(sink, p, D2D1_FIGURE_BEGIN_HOLLOW);
                ID2D1GeometrySink_AddBezier(sink, &q);
                ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_OPEN);
                ID2D1GeometrySink_Close(sink);
                ID2D1GeometrySink_Release(sink);
                printf("quarter circle %s:", num(a, r));
                for (j = 0; j < 3; ++j)
                {
                    hr = ID2D1PathGeometry_ComputePointAtLength(path, quarters[i].l[j], NULL, 0.25f, &point, &tangent);
                    printf(" at %s hr %#lx", num(a, quarters[i].l[j]), hr);
                    print_point(" point ", point);
                    print_point(" tangent ", tangent);
                }
                printf("\n");
                (void)b;
                ID2D1PathGeometry_Release(path);
            }
        }

        ID2D1Factory_CreatePathGeometry(factory, &path);
        stream("empty, never opened", (ID2D1Geometry *)path);
        ID2D1PathGeometry_Open(path, &sink);
        stream("empty, open", (ID2D1Geometry *)path);
        ID2D1GeometrySink_Close(sink);
        ID2D1GeometrySink_Release(sink);
        stream("empty, closed", (ID2D1Geometry *)path);
        measure("empty", (ID2D1Geometry *)path, NULL, line_lengths, 1);
        ID2D1PathGeometry_Release(path);
    }

    ID2D1Geometry_Release(e);
    ID2D1Geometry_Release(b);
    ID2D1Geometry_Release(a);
    ID2D1Factory_Release(factory);
    printf("done\n");
    return 0;
}
