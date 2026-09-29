/*
 * emfframeprobe - how GDI+ turns a frame into an EMF's rclFrame, and an rclFrame back into bounds.
 *
 * Windows records a frame 100 pixels wide as an rclFrame 99 pixels wide, and reads an rclFrame back as one pixel
 * wider than it is; Wine did neither.  This records metafiles with frames in each MetafileFrameUnit and with no
 * frame, and prints the rclFrame and rclBounds written; then, for GDI metafiles with given frames, what
 * GdipGetMetafileHeaderFromEmf(), GdipGetImageBounds(), GdipGetImageWidth() and GdipGetImageDimension() say.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <windows.h>
#include <stdio.h>

typedef int GpStatus;
typedef void GpGraphics, GpMetafile, GpBrush, GpImage;
typedef struct { float X, Y, Width, Height; } GpRectF;
typedef struct { UINT32 GdiplusVersion; void *DebugEventCallback; BOOL SuppressBackgroundThread, SuppressExternalCodecs; } GdiplusStartupInput;
typedef struct
{
    DWORD iType, nSize;
    RECTL rclBounds, rclFrame;
    DWORD dSignature, nVersion, nBytes, nRecords;
    WORD nHandles, sReserved;
    DWORD nDescription, offDescription, nPalEntries;
    SIZEL szlDevice, szlMillimeters;
} ENHMETAHEADER3;
typedef struct
{
    UINT Type, Size, Version, EmfPlusFlags;
    float DpiX, DpiY;
    INT X, Y, Width, Height;
    ENHMETAHEADER3 EmfHeader;
    INT EmfPlusHeaderSize, LogicalDpiX, LogicalDpiY;
} MetafileHeader;

GpStatus WINAPI GdiplusStartup(ULONG_PTR *, const GdiplusStartupInput *, void *);
GpStatus WINAPI GdipRecordMetafile(HDC, int, const GpRectF *, int, const WCHAR *, GpMetafile **);
GpStatus WINAPI GdipGetImageGraphicsContext(GpImage *, GpGraphics **);
GpStatus WINAPI GdipCreateSolidFill(DWORD, GpBrush **);
GpStatus WINAPI GdipFillRectangleI(GpGraphics *, GpBrush *, INT, INT, INT, INT);
GpStatus WINAPI GdipDeleteGraphics(GpGraphics *);
GpStatus WINAPI GdipDeleteBrush(GpBrush *);
GpStatus WINAPI GdipGetHemfFromMetafile(GpMetafile *, HENHMETAFILE *);
GpStatus WINAPI GdipDisposeImage(GpImage *);
GpStatus WINAPI GdipCreateMetafileFromEmf(HENHMETAFILE, BOOL, GpMetafile **);
GpStatus WINAPI GdipGetMetafileHeaderFromEmf(HENHMETAFILE, MetafileHeader *);
GpStatus WINAPI GdipGetImageBounds(GpImage *, GpRectF *, int *);
GpStatus WINAPI GdipGetImageWidth(GpImage *, UINT *);
GpStatus WINAPI GdipGetImageHeight(GpImage *, UINT *);
GpStatus WINAPI GdipGetImageDimension(GpImage *, float *, float *);
GpStatus WINAPI GdipCreateMetafileFromWmf(HMETAFILE, BOOL, const void *, GpMetafile **);
GpStatus WINAPI GdipGetDC(GpGraphics *, HDC *);
GpStatus WINAPI GdipGetImageRawFormat(GpImage *, GUID *);
GpStatus WINAPI GdipReleaseDC(GpGraphics *, HDC);
GpStatus WINAPI GdipGetMetafileHeaderFromMetafile(GpMetafile *, MetafileHeader *);

static void describe(HENHMETAFILE hemf)
{
    MetafileHeader header;
    GpMetafile *metafile;
    GpRectF bounds;
    UINT width = 0, height = 0;
    float dim_x = 0, dim_y = 0;
    int unit = -1;

    memset(&header, 0, sizeof(header));
    GdipGetMetafileHeaderFromEmf(hemf, &header);
    printf("    rclFrame %ld,%ld-%ld,%ld, rclBounds %ld,%ld-%ld,%ld, device %ldx%ld, mm %ldx%ld\n",
           header.EmfHeader.rclFrame.left, header.EmfHeader.rclFrame.top, header.EmfHeader.rclFrame.right,
           header.EmfHeader.rclFrame.bottom, header.EmfHeader.rclBounds.left, header.EmfHeader.rclBounds.top,
           header.EmfHeader.rclBounds.right, header.EmfHeader.rclBounds.bottom, header.EmfHeader.szlDevice.cx,
           header.EmfHeader.szlDevice.cy, header.EmfHeader.szlMillimeters.cx, header.EmfHeader.szlMillimeters.cy);
    printf("    header %d,%d %dx%d, dpi %.3f,%.3f\n", header.X, header.Y, header.Width, header.Height, header.DpiX,
           header.DpiY);
    GdipCreateMetafileFromEmf(CopyEnhMetaFileW(hemf, NULL), TRUE, &metafile);
    GdipGetImageBounds(metafile, &bounds, &unit);
    GdipGetImageWidth(metafile, &width);
    GdipGetImageHeight(metafile, &height);
    GdipGetImageDimension(metafile, &dim_x, &dim_y);
    printf("    bounds %.3f,%.3f %.3fx%.3f unit %d, width %u, height %u, dimension %.3fx%.3f\n", bounds.X, bounds.Y,
           bounds.Width, bounds.Height, unit, width, height, dim_x, dim_y);
    GdipDisposeImage(metafile);
}

static void record(HDC ref, const GpRectF *frame, int unit, const char *what)
{
    GpMetafile *metafile;
    GpGraphics *graphics;
    GpBrush *brush;
    HENHMETAFILE hemf;
    GpStatus status;

    status = GdipRecordMetafile(ref, 5 /* EmfTypeEmfPlusDual */, frame, unit, L"frameprobe", &metafile);
    printf("record %s: %d\n", what, status);
    if (status) return;
    GdipGetImageGraphicsContext(metafile, &graphics);
    GdipCreateSolidFill(0xffff0000, &brush);
    GdipFillRectangleI(graphics, brush, 25, 25, 50, 50);
    GdipDeleteBrush(brush);
    GdipDeleteGraphics(graphics);
    GdipGetHemfFromMetafile(metafile, &hemf);
    GdipDisposeImage(metafile);
    describe(hemf);
    DeleteEnhMetaFile(hemf);
}

int main(void)
{
    static const struct { GpRectF frame; int unit; const char *what; } frames[] =
    {
        { { 0, 0, 100, 100 }, 2, "pixel 0,0 100x100" },
        { { 10, 20, 100, 50 }, 2, "pixel 10,20 100x50" },
        { { 0, 0, 1, 1 }, 4, "inch 0,0 1x1" },
        { { 0, 0, 72, 72 }, 3, "point 0,0 72x72" },
        { { 0, 0, 300, 300 }, 5, "document 0,0 300x300" },
        { { 0, 0, 10, 10 }, 6, "millimeter 0,0 10x10" },
        { { 0, 0, 2540, 2540 }, 7, "gdi 0,0 2540x2540" },
        { { 100, 200, 2540, 2540 }, 7, "gdi 100,200 2540x2540" },
    };
    static const RECT gdi_frames[] = { { 0, 0, 2540, 2540 }, { 0, 0, 2620, 2617 }, { 100, 200, 2640, 2740 },
                                       { 0, 0, 10000, 5000 }, { 0, 0, 2646, 2646 } };
    GdiplusStartupInput input = { 1 };
    ULONG_PTR token;
    HDC screen = GetDC(NULL), mfdc;
    HENHMETAFILE hemf;
    unsigned int i;

    GdiplusStartup(&token, &input, NULL);
    printf("screen %dx%d px, %dx%d mm, logpixels %d\n", GetDeviceCaps(screen, HORZRES), GetDeviceCaps(screen, VERTRES),
           GetDeviceCaps(screen, HORZSIZE), GetDeviceCaps(screen, VERTSIZE), GetDeviceCaps(screen, LOGPIXELSX));
    for (i = 0; i < ARRAY_SIZE(frames); i++)
        record(screen, &frames[i].frame, frames[i].unit, frames[i].what);
    record(screen, NULL, 2, "no frame");
    {
        /* no frame, and GDI records through GdipGetDC() */
        GpMetafile *metafile;
        GpGraphics *graphics;
        HENHMETAFILE hemf2;
        HDC gdc;

        printf("record no frame, GDI rectangle 25,25-75,75: %d\n",
               GdipRecordMetafile(screen, 5, NULL, 2, L"frameprobe", &metafile));
        GdipGetImageGraphicsContext(metafile, &graphics);
        GdipGetDC(graphics, &gdc);
        Rectangle(gdc, 25, 25, 75, 75);
        GdipReleaseDC(graphics, gdc);
        GdipDeleteGraphics(graphics);
        GdipGetHemfFromMetafile(metafile, &hemf2);
        GdipDisposeImage(metafile);
        describe(hemf2);
        DeleteEnhMetaFile(hemf2);
    }
    for (i = 0; i < ARRAY_SIZE(gdi_frames); i++)
    {
        mfdc = CreateEnhMetaFileW(screen, NULL, &gdi_frames[i], NULL);
        Rectangle(mfdc, 10, 10, 60, 50);
        hemf = CloseEnhMetaFile(mfdc);
        printf("GDI frame %ld,%ld-%ld,%ld:\n", gdi_frames[i].left, gdi_frames[i].top, gdi_frames[i].right,
               gdi_frames[i].bottom);
        describe(hemf);
        DeleteEnhMetaFile(hemf);
    }
    {
        /* a WMF, through a placeable header of 1000 units an inch */
        static const struct { SHORT left, top, right, bottom; WORD inch; } boxes[] =
            { { 0, 0, 1000, 1000, 1000 }, { 100, 200, 1100, 700, 1000 }, { 0, 0, 1440, 720, 1440 },
              { 100, 200, 1100, 700, 1000 } /* no checksum */, { 0 } /* none */ };
        for (i = 0; i < ARRAY_SIZE(boxes); i++)
        {
            struct { DWORD key; WORD hmf; SHORT left, top, right, bottom; WORD inch; DWORD reserved; WORD checksum; }
                __attribute__((packed)) placeable = { 0x9ac6cdd7, 0, boxes[i].left, boxes[i].top, boxes[i].right,
                                                      boxes[i].bottom, boxes[i].inch, 0, 0 };
            MetafileHeader header;
            GpMetafile *metafile;
            GpRectF bounds;
            UINT width = 0, height = 0;
            float dim_x = 0, dim_y = 0;
            int unit = -1;
            HMETAFILE hwmf;
            HDC wdc = CreateMetaFileW(NULL);

            if (i < 3)
            {
                const WORD *words = (const WORD *)&placeable;
                unsigned int k;
                for (k = 0; k < 10; k++) placeable.checksum ^= words[k];
            }
            Rectangle(wdc, 10, 10, 60, 50);
            hwmf = CloseMetaFile(wdc);
            printf("WMF box %d,%d-%d,%d, %u an inch%s: %d\n", boxes[i].left, boxes[i].top, boxes[i].right,
                   boxes[i].bottom, boxes[i].inch, i == 3 ? ", checksum 0" : i == 4 ? ", no placeable header" : "",
                   GdipCreateMetafileFromWmf(hwmf, TRUE, i == 4 ? NULL : (void *)&placeable, &metafile));
            memset(&header, 0, sizeof(header));
            GdipGetMetafileHeaderFromMetafile(metafile, &header);
            {
                GUID format = { 0 };
                GdipGetImageRawFormat(metafile, &format);
                printf("    header %d,%d %dx%d, dpi %.3f,%.3f, type %u, raw format %08lx (%s)\n", header.X, header.Y,
                       header.Width, header.Height, header.DpiX, header.DpiY, header.Type, format.Data1,
                       format.Data1 == 0xb96b3cad ? "WMF" : format.Data1 == 0xb96b3cac ? "EMF" : "?");
            }
            GdipGetImageBounds(metafile, &bounds, &unit);
            GdipGetImageWidth(metafile, &width);
            GdipGetImageHeight(metafile, &height);
            GdipGetImageDimension(metafile, &dim_x, &dim_y);
            printf("    bounds %.3f,%.3f %.3fx%.3f unit %d, width %u, height %u, dimension %.3fx%.3f\n", bounds.X,
                   bounds.Y, bounds.Width, bounds.Height, unit, width, height, dim_x, dim_y);
            GdipDisposeImage(metafile);
        }
    }
    ReleaseDC(NULL, screen);
    printf("done\n");
    return 0;
}
