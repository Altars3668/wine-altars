/*
 * The second half of what d2dgeom.c asks of Direct2D's geometries: how a stroke is widened with each join, cap and
 * dash, how far a mitre reaches before its limit cuts it, the bounds of ellipses and rounded rectangles turned,
 * and how Stream hands back a path made of every kind of segment.
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
    if (fabsf(v) < 5e-5f)
        v = 0.0f;
    sprintf(buf, "%.7g", v);
    return buf;
}

static ID2D1Factory *factory;

struct sink
{
    ID2D1GeometrySink ID2D1GeometrySink_iface;
    BOOL print;
};

static HRESULT STDMETHODCALLTYPE sink_QueryInterface(ID2D1GeometrySink *iface, REFIID iid, void **out)
{
    *out = iface;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE sink_AddRef(ID2D1GeometrySink *iface)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE sink_Release(ID2D1GeometrySink *iface)
{
    return 1;
}

static void STDMETHODCALLTYPE sink_SetFillMode(ID2D1GeometrySink *iface, D2D1_FILL_MODE mode)
{
    printf("  SetFillMode %u\n", mode);
}

static void STDMETHODCALLTYPE sink_SetSegmentFlags(ID2D1GeometrySink *iface, D2D1_PATH_SEGMENT flags)
{
    printf("  SetSegmentFlags %#x\n", flags);
}

static void print_point(const char *prefix, D2D1_POINT_2F p)
{
    char x[32], y[32];

    printf("%s(%s,%s)", prefix, num(x, p.x), num(y, p.y));
}

static void STDMETHODCALLTYPE sink_BeginFigure(ID2D1GeometrySink *iface, D2D1_POINT_2F p, D2D1_FIGURE_BEGIN begin)
{
    print_point("  BeginFigure ", p);
    printf(" %u\n", begin);
}

static void STDMETHODCALLTYPE sink_AddLines(ID2D1GeometrySink *iface, const D2D1_POINT_2F *points, UINT32 count)
{
    UINT32 i;

    printf("  AddLines %u", count);
    for (i = 0; i < count; ++i)
        print_point(" ", points[i]);
    printf("\n");
}

static void STDMETHODCALLTYPE sink_AddBeziers(ID2D1GeometrySink *iface, const D2D1_BEZIER_SEGMENT *b, UINT32 count)
{
    UINT32 i;

    printf("  AddBeziers %u", count);
    for (i = 0; i < count; ++i)
    {
        print_point(" [", b[i].point1);
        print_point(" ", b[i].point2);
        print_point(" ", b[i].point3);
        printf("]");
    }
    printf("\n");
}

static void STDMETHODCALLTYPE sink_EndFigure(ID2D1GeometrySink *iface, D2D1_FIGURE_END end)
{
    printf("  EndFigure %u\n", end);
}

static HRESULT STDMETHODCALLTYPE sink_Close(ID2D1GeometrySink *iface)
{
    printf("  Close\n");
    return S_OK;
}

static void STDMETHODCALLTYPE sink_AddLine(ID2D1GeometrySink *iface, D2D1_POINT_2F p)
{
    print_point("  AddLine ", p);
    printf("\n");
}

static void STDMETHODCALLTYPE sink_AddBezier(ID2D1GeometrySink *iface, const D2D1_BEZIER_SEGMENT *b)
{
    print_point("  AddBezier ", b->point1);
    print_point(" ", b->point2);
    print_point(" ", b->point3);
    printf("\n");
}

static void STDMETHODCALLTYPE sink_AddQuadraticBezier(ID2D1GeometrySink *iface, const D2D1_QUADRATIC_BEZIER_SEGMENT *b)
{
    print_point("  AddQuadraticBezier ", b->point1);
    print_point(" ", b->point2);
    printf("\n");
}

static void STDMETHODCALLTYPE sink_AddQuadraticBeziers(ID2D1GeometrySink *iface,
        const D2D1_QUADRATIC_BEZIER_SEGMENT *b, UINT32 count)
{
    UINT32 i;

    printf("  AddQuadraticBeziers %u", count);
    for (i = 0; i < count; ++i)
    {
        print_point(" [", b[i].point1);
        print_point(" ", b[i].point2);
        printf("]");
    }
    printf("\n");
}

static void STDMETHODCALLTYPE sink_AddArc(ID2D1GeometrySink *iface, const D2D1_ARC_SEGMENT *arc)
{
    char a[32], b[32], c[32];

    print_point("  AddArc ", arc->point);
    printf(" size %s,%s rotation %s sweep %u arc %u\n", num(a, arc->size.width), num(b, arc->size.height),
            num(c, arc->rotationAngle), arc->sweepDirection, arc->arcSize);
}

static const ID2D1GeometrySinkVtbl sink_vtbl =
{
    sink_QueryInterface, sink_AddRef, sink_Release, sink_SetFillMode, sink_SetSegmentFlags, sink_BeginFigure,
    sink_AddLines, sink_AddBeziers, sink_EndFigure, sink_Close, sink_AddLine, sink_AddBezier,
    sink_AddQuadraticBezier, sink_AddQuadraticBeziers, sink_AddArc,
};

static ID2D1Geometry *polyline(const D2D1_POINT_2F *points, unsigned int count, BOOL closed)
{
    ID2D1PathGeometry *path;
    ID2D1GeometrySink *sink;

    ID2D1Factory_CreatePathGeometry(factory, &path);
    ID2D1PathGeometry_Open(path, &sink);
    ID2D1GeometrySink_BeginFigure(sink, points[0], closed ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
    ID2D1GeometrySink_AddLines(sink, points + 1, count - 1);
    ID2D1GeometrySink_EndFigure(sink, closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    ID2D1GeometrySink_Close(sink);
    ID2D1GeometrySink_Release(sink);
    return (ID2D1Geometry *)path;
}

/* The area a stroke covers, and its bounds, and a few points in it or not. */
static void widen(const char *name, ID2D1Geometry *g, float width, const D2D1_STROKE_STYLE_PROPERTIES *props,
        const float *dashes, unsigned int dash_count, const D2D1_POINT_2F *points, unsigned int count)
{
    D2D1_RECT_F bounds = {-1, -1, -1, -1};
    ID2D1StrokeStyle *style = NULL;
    ID2D1GeometrySink *path_sink;
    char a[32], b[32], c[32], d[32];
    ID2D1PathGeometry *path;
    BOOL fill, stroke;
    unsigned int i;
    float area = -1.0f;
    HRESULT hr;

    if (props)
        ID2D1Factory_CreateStrokeStyle(factory, props, dashes, dash_count, &style);
    hr = ID2D1Geometry_GetWidenedBounds(g, width, style, NULL, 0.25f, &bounds);
    printf("%s: bounds hr %#lx {%s, %s, %s, %s}", name, hr, num(a, bounds.left), num(b, bounds.top),
            num(c, bounds.right), num(d, bounds.bottom));
    ID2D1Factory_CreatePathGeometry(factory, &path);
    ID2D1PathGeometry_Open(path, &path_sink);
    hr = ID2D1Geometry_Widen(g, width, style, NULL, 0.25f, (ID2D1SimplifiedGeometrySink *)path_sink);
    ID2D1GeometrySink_Close(path_sink);
    ID2D1GeometrySink_Release(path_sink);
    ID2D1PathGeometry_ComputeArea(path, NULL, 0.25f, &area);
    printf(" widen hr %#lx area %s;", hr, num(a, area));
    for (i = 0; i < count; ++i)
    {
        fill = stroke = -1;
        ID2D1PathGeometry_FillContainsPoint(path, points[i], NULL, 0.01f, &fill);
        ID2D1Geometry_StrokeContainsPoint(g, points[i], width, style, NULL, 0.01f, &stroke);
        print_point(" ", points[i]);
        printf("=%d/%d", fill, stroke);
    }
    printf("\n");
    ID2D1PathGeometry_Release(path);
    if (style)
        ID2D1StrokeStyle_Release(style);
}

static void bounds(const char *name, ID2D1Geometry *g, const D2D1_MATRIX_3X2_F *m)
{
    D2D1_RECT_F r = {-1, -1, -1, -1};
    char a[32], b[32], c[32], d[32];
    HRESULT hr;

    hr = ID2D1Geometry_GetBounds(g, m, &r);
    printf("%s: bounds hr %#lx {%s, %s, %s, %s}\n", name, hr, num(a, r.left), num(b, r.top), num(c, r.right),
            num(d, r.bottom));
}

int main(void)
{
    static const D2D1_POINT_2F sharp[] = {{0.0f, 0.0f}, {20.0f, 0.0f}, {0.0f, 4.0f}};
    static const D2D1_POINT_2F right[] = {{0.0f, 0.0f}, {10.0f, 0.0f}, {10.0f, 10.0f}};
    static const D2D1_POINT_2F back[] = {{0.0f, 0.0f}, {10.0f, 0.0f}, {5.0f, 0.0f}};
    static const D2D1_POINT_2F square[] = {{0.0f, 0.0f}, {10.0f, 0.0f}, {10.0f, 10.0f}, {0.0f, 10.0f}};
    static const D2D1_POINT_2F line[] = {{0.0f, 0.0f}, {10.0f, 0.0f}};
    static const D2D1_POINT_2F sharp_points[] = {{21.0f, 0.0f}, {22.0f, 0.2f}, {25.0f, 0.0f}, {10.0f, 1.5f}};
    static const D2D1_POINT_2F right_points[] = {{10.8f, -0.8f}, {10.95f, -0.95f}, {5.0f, 0.0f}, {9.5f, 0.5f}};
    static const D2D1_POINT_2F line_points[] = {{-0.5f, 0.0f}, {10.5f, 0.0f}, {2.5f, 0.0f}, {5.5f, 0.0f}};
    static const float dashes[] = {2.0f, 1.0f};
    D2D1_STROKE_STYLE_PROPERTIES props = {0};
    ID2D1Geometry *g;
    D2D1_MATRIX_3X2_F rotate = {{{0.8660254f, 0.5f, -0.5f, 0.8660254f, 100.0f, 50.0f}}};
    D2D1_ELLIPSE ellipse = {{10.0f, 20.0f}, 30.0f, 10.0f};
    D2D1_ROUNDED_RECT rounded = {{0.0f, 0.0f, 40.0f, 20.0f}, 8.0f, 5.0f};
    HRESULT hr;

    if (FAILED(hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory, NULL, (void **)&factory)))
        return 1;

    g = polyline(sharp, ARRAY_SIZE(sharp), FALSE);
    widen("sharp, no style", g, 2.0f, NULL, NULL, 0, sharp_points, ARRAY_SIZE(sharp_points));
    props.lineJoin = D2D1_LINE_JOIN_MITER;
    props.miterLimit = 10.0f;
    widen("sharp, miter 10", g, 2.0f, &props, NULL, 0, sharp_points, ARRAY_SIZE(sharp_points));
    props.miterLimit = 2.0f;
    widen("sharp, miter 2", g, 2.0f, &props, NULL, 0, sharp_points, ARRAY_SIZE(sharp_points));
    props.miterLimit = 0.5f;
    widen("sharp, miter 0.5", g, 2.0f, &props, NULL, 0, sharp_points, ARRAY_SIZE(sharp_points));
    props.lineJoin = D2D1_LINE_JOIN_MITER_OR_BEVEL;
    props.miterLimit = 2.0f;
    widen("sharp, miter or bevel 2", g, 2.0f, &props, NULL, 0, sharp_points, ARRAY_SIZE(sharp_points));
    props.miterLimit = 100.0f;
    widen("sharp, miter or bevel 100", g, 2.0f, &props, NULL, 0, sharp_points, ARRAY_SIZE(sharp_points));
    props.lineJoin = D2D1_LINE_JOIN_BEVEL;
    widen("sharp, bevel", g, 2.0f, &props, NULL, 0, sharp_points, ARRAY_SIZE(sharp_points));
    props.lineJoin = D2D1_LINE_JOIN_ROUND;
    widen("sharp, round", g, 2.0f, &props, NULL, 0, sharp_points, ARRAY_SIZE(sharp_points));
    ID2D1Geometry_Release(g);

    g = polyline(right, ARRAY_SIZE(right), FALSE);
    props.lineJoin = D2D1_LINE_JOIN_MITER;
    props.miterLimit = 1.0f;
    widen("right angle, miter 1", g, 2.0f, &props, NULL, 0, right_points, ARRAY_SIZE(right_points));
    props.lineJoin = D2D1_LINE_JOIN_ROUND;
    widen("right angle, round", g, 2.0f, &props, NULL, 0, right_points, ARRAY_SIZE(right_points));
    props.lineJoin = D2D1_LINE_JOIN_MITER;
    props.miterLimit = 10.0f;
    props.startCap = D2D1_CAP_STYLE_ROUND;
    props.endCap = D2D1_CAP_STYLE_TRIANGLE;
    widen("right angle, round start, triangle end", g, 2.0f, &props, NULL, 0, right_points, ARRAY_SIZE(right_points));
    props.startCap = props.endCap = D2D1_CAP_STYLE_FLAT;
    ID2D1Geometry_Release(g);

    g = polyline(back, ARRAY_SIZE(back), FALSE);
    widen("back on itself, no style", g, 2.0f, NULL, NULL, 0, line_points, ARRAY_SIZE(line_points));
    props.lineJoin = D2D1_LINE_JOIN_ROUND;
    widen("back on itself, round", g, 2.0f, &props, NULL, 0, line_points, ARRAY_SIZE(line_points));
    ID2D1Geometry_Release(g);

    g = polyline(line, ARRAY_SIZE(line), FALSE);
    props.lineJoin = D2D1_LINE_JOIN_MITER;
    props.dashStyle = D2D1_DASH_STYLE_CUSTOM;
    widen("line, dashed 2 1", g, 2.0f, &props, dashes, ARRAY_SIZE(dashes), line_points, ARRAY_SIZE(line_points));
    props.dashCap = D2D1_CAP_STYLE_ROUND;
    widen("line, dashed 2 1, round dash caps", g, 2.0f, &props, dashes, ARRAY_SIZE(dashes), line_points,
            ARRAY_SIZE(line_points));
    props.dashStyle = D2D1_DASH_STYLE_SOLID;
    props.dashCap = D2D1_CAP_STYLE_FLAT;
    ID2D1Geometry_Release(g);

    g = polyline(square, ARRAY_SIZE(square), TRUE);
    widen("square, no style", g, 2.0f, NULL, NULL, 0, right_points, ARRAY_SIZE(right_points));
    widen("square, wider than itself", g, 30.0f, NULL, NULL, 0, right_points, ARRAY_SIZE(right_points));
    ID2D1Geometry_Release(g);

    ID2D1Factory_CreateEllipseGeometry(factory, &ellipse, (ID2D1EllipseGeometry **)&g);
    bounds("ellipse", g, NULL);
    bounds("ellipse turned", g, &rotate);
    widen("ellipse, no style", g, 4.0f, NULL, NULL, 0, line_points, ARRAY_SIZE(line_points));
    ID2D1Geometry_Release(g);
    ID2D1Factory_CreateRoundedRectangleGeometry(factory, &rounded, (ID2D1RoundedRectangleGeometry **)&g);
    bounds("rounded rect", g, NULL);
    bounds("rounded rect turned", g, &rotate);
    ID2D1Geometry_Release(g);

    {
        D2D1_BEZIER_SEGMENT cubics[2] = {{{1, 5}, {9, 5}, {10, 0}}, {{11, -5}, {19, -5}, {20, 0}}};
        D2D1_QUADRATIC_BEZIER_SEGMENT quads[2] = {{{25, 5}, {30, 0}}, {{35, -5}, {40, 0}}};
        D2D1_ARC_SEGMENT arc = {{50, 0}, {5, 5}, 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL};
        D2D1_POINT_2F p = {0, 0}, lines[2] = {{50, 10}, {0, 10}};
        struct sink sink = {{&sink_vtbl}};
        ID2D1PathGeometry *path;
        ID2D1GeometrySink *geometry_sink;

        ID2D1Factory_CreatePathGeometry(factory, &path);
        ID2D1PathGeometry_Open(path, &geometry_sink);
        ID2D1GeometrySink_SetFillMode(geometry_sink, D2D1_FILL_MODE_WINDING);
        ID2D1GeometrySink_BeginFigure(geometry_sink, p, D2D1_FIGURE_BEGIN_FILLED);
        ID2D1GeometrySink_AddBezier(geometry_sink, &cubics[0]);
        ID2D1GeometrySink_AddBezier(geometry_sink, &cubics[1]);
        ID2D1GeometrySink_AddQuadraticBeziers(geometry_sink, quads, 2);
        ID2D1GeometrySink_SetSegmentFlags(geometry_sink, D2D1_PATH_SEGMENT_FORCE_UNSTROKED);
        ID2D1GeometrySink_AddArc(geometry_sink, &arc);
        ID2D1GeometrySink_AddArc(geometry_sink, &arc);
        ID2D1GeometrySink_SetSegmentFlags(geometry_sink, D2D1_PATH_SEGMENT_NONE);
        ID2D1GeometrySink_AddLine(geometry_sink, lines[0]);
        ID2D1GeometrySink_AddLines(geometry_sink, &lines[1], 1);
        ID2D1GeometrySink_EndFigure(geometry_sink, D2D1_FIGURE_END_CLOSED);
        p.x = 100.0f;
        ID2D1GeometrySink_BeginFigure(geometry_sink, p, D2D1_FIGURE_BEGIN_HOLLOW);
        ID2D1GeometrySink_AddQuadraticBezier(geometry_sink, &quads[0]);
        ID2D1GeometrySink_AddBeziers(geometry_sink, cubics, 2);
        ID2D1GeometrySink_EndFigure(geometry_sink, D2D1_FIGURE_END_OPEN);
        hr = ID2D1GeometrySink_Close(geometry_sink);
        ID2D1GeometrySink_Release(geometry_sink);
        printf("stream (close hr %#lx):\n", hr);
        hr = ID2D1PathGeometry_Stream(path, &sink.ID2D1GeometrySink_iface);
        printf("  hr %#lx\n", hr);
        ID2D1PathGeometry_Release(path);
    }

    ID2D1Factory_Release(factory);
    printf("done\n");
    return 0;
}
