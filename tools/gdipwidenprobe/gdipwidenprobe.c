/* What GdipWidenPath makes of a corner for each line join.
 *
 * Wine's gdiplus had no round join (it drew a bevel) and its tests fix only straight lines and caps.
 * This widens an open polyline with a right angle and a sharp one, and a closed triangle, with a
 * 10-unit pen and LineJoinMiter, LineJoinBevel, LineJoinRound and LineJoinMiterClipped, and prints
 * every point of the result with its type.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror gdipwidenprobe.c -o gdipwidenprobe.exe -lgdiplus
 */
#include <windows.h>
#include <stdio.h>

typedef struct { float X, Y; } PointF;
typedef void GpPath, GpPen, GpMatrix;
typedef struct { UINT32 GdiplusVersion; void *DebugEventCallback; BOOL SuppressBackgroundThread, SuppressExternalCodecs; } StartupInput;

int WINAPI GdiplusStartup( ULONG_PTR *, const StartupInput *, void * );
int WINAPI GdipCreatePath( int, GpPath ** );
int WINAPI GdipAddPathLine2( GpPath *, const PointF *, int );
int WINAPI GdipClosePathFigure( GpPath * );
int WINAPI GdipCreatePen1( UINT32, float, int, GpPen ** );
int WINAPI GdipSetPenLineJoin( GpPen *, int );
int WINAPI GdipWidenPath( GpPath *, GpPen *, GpMatrix *, float );
int WINAPI GdipGetPointCount( GpPath *, int * );
int WINAPI GdipGetPathPoints( GpPath *, PointF *, int );
int WINAPI GdipGetPathTypes( GpPath *, BYTE *, int );
int WINAPI GdipDeletePath( GpPath * );
int WINAPI GdipDeletePen( GpPen * );

static void widen( const char *what, const PointF *points, int count, BOOL closed, int join, float flatness )
{
    static const char *joins[] = { "Miter", "Bevel", "Round", "MiterClipped" };
    PointF out[512];
    BYTE types[512];
    GpPath *path;
    GpPen *pen;
    int status, n = 0, i;

    GdipCreatePath( 0, &path );
    GdipAddPathLine2( path, points, count );
    if (closed) GdipClosePathFigure( path );
    GdipCreatePen1( 0xff000000, 10.0f, 2 /* UnitPixel */, &pen );
    GdipSetPenLineJoin( pen, join );
    status = GdipWidenPath( path, pen, NULL, flatness );
    GdipGetPointCount( path, &n );
    printf( "%s, join %s, flatness %.2f: status %d, %d points\n", what, joins[join], flatness, status, n );
    if (n > 0 && n <= 512)
    {
        GdipGetPathPoints( path, out, n );
        GdipGetPathTypes( path, types, n );
        for (i = 0; i < n; i++) printf( "  %2d: %8.3f %8.3f  type %#04x\n", i, out[i].X, out[i].Y, types[i] );
    }
    GdipDeletePen( pen );
    GdipDeletePath( path );
}

int main(void)
{
    static const PointF corner[] = { {10, 10}, {60, 10}, {60, 60} };
    static const PointF sharp[] = { {10, 10}, {60, 10}, {15, 25} };
    static const PointF triangle[] = { {10, 10}, {60, 10}, {35, 50} };
    StartupInput input = { 1, NULL, FALSE, FALSE };
    ULONG_PTR token;
    int join;

    GdiplusStartup( &token, &input, NULL );
    for (join = 0; join < 4; join++)
    {
        widen( "right angle", corner, 3, FALSE, join, 0.25f );
        widen( "sharp angle", sharp, 3, FALSE, join, 0.25f );
        widen( "closed triangle", triangle, 3, TRUE, join, 0.25f );
    }
    widen( "right angle", corner, 3, FALSE, 2, 1.0f );
    return 0;
}
