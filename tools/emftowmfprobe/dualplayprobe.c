/* dualplayprobe - does GDI draw what GDI+ recorded in a dual metafile?  Records a red rectangle, plays the EMF with
 * PlayEnhMetaFile into a white DIB, and prints pixels inside and outside it.  Copyright 2026 AltarsCN, LGPL 2.1+. */
#include <windows.h>
#include <stdio.h>

typedef int GpStatus;
typedef void GpGraphics, GpMetafile, GpBrush, GpImage;
typedef struct { float X, Y, Width, Height; } GpRectF;
typedef struct { UINT32 GdiplusVersion; void *DebugEventCallback; BOOL SuppressBackgroundThread, SuppressExternalCodecs; } GdiplusStartupInput;
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
GpStatus WINAPI GdipGetImageBounds(GpImage *, GpRectF *, int *);
GpStatus WINAPI GdipGetImageHorizontalResolution(GpImage *, float *);
GpStatus WINAPI GdipCreateBitmapFromScan0(INT, INT, INT, INT, BYTE *, GpImage **);
GpStatus WINAPI GdipDrawImageRectI(GpGraphics *, GpImage *, INT, INT, INT, INT);
GpStatus WINAPI GdipBitmapGetPixel(GpImage *, INT, INT, DWORD *);

int main(void)
{
    GdiplusStartupInput input = { 1 };
    GpRectF frame = { 0, 0, 100, 100 };
    BITMAPINFO bmi = {{ sizeof(bmi.bmiHeader), 100, -100, 1, 32, BI_RGB }};
    ULONG_PTR token;
    GpMetafile *metafile;
    GpGraphics *graphics;
    GpBrush *brush;
    HENHMETAFILE hemf;
    ENHMETAHEADER header;
    DWORD *pixels;
    HBITMAP dib;
    RECT rect = { 0, 0, 100, 100 };
    HDC hdc, mem;

    GdiplusStartup(&token, &input, NULL);
    hdc = CreateCompatibleDC(0);
    printf("record %d\n", GdipRecordMetafile(hdc, 5 /* EmfTypeEmfPlusDual */, &frame, 2, L"probe", &metafile));
    DeleteDC(hdc);
    GdipGetImageGraphicsContext(metafile, &graphics);
    GdipCreateSolidFill(0xffff0000, &brush);
    GdipFillRectangleI(graphics, brush, 25, 25, 50, 50);
    GdipDeleteBrush(brush);
    GdipDeleteGraphics(graphics);
    GdipGetHemfFromMetafile(metafile, &hemf);
    GdipDisposeImage(metafile);
    GetEnhMetaFileHeader(hemf, sizeof(header), &header);
    printf("EMF %lu bytes, %lu records\n", header.nBytes, header.nRecords);
    mem = CreateCompatibleDC(0);
    dib = CreateDIBSection(mem, &bmi, DIB_RGB_COLORS, (void **)&pixels, NULL, 0);
    SelectObject(mem, dib);
    memset(pixels, 0xff, 100 * 100 * 4);
    printf("play %d\n", PlayEnhMetaFile(mem, hemf, &rect));
    printf("inside %#lx, outside %#lx %#lx, edge %#lx %#lx\n", pixels[50 * 100 + 50] & 0xffffff,
           pixels[5 * 100 + 5] & 0xffffff, pixels[95 * 100 + 95] & 0xffffff, pixels[25 * 100 + 25] & 0xffffff,
           pixels[74 * 100 + 74] & 0xffffff);
    printf("bounds %ld,%ld-%ld,%ld, frame %ld,%ld-%ld,%ld, device %ldx%ld, mm %ldx%ld\n", header.rclBounds.left,
           header.rclBounds.top, header.rclBounds.right, header.rclBounds.bottom, header.rclFrame.left,
           header.rclFrame.top, header.rclFrame.right, header.rclFrame.bottom, header.szlDevice.cx,
           header.szlDevice.cy, header.szlMillimeters.cx, header.szlMillimeters.cy);
    {
        int i, first = -1, last = -1;
        for (i = 0; i < 100; i++)
            if ((pixels[50 * 100 + i] & 0xffffff) != 0xffffff) { if (first < 0) first = i; last = i; }
        printf("row 50 drawn from %d to %d\n", first, last);
        first = last = -1;
        for (i = 0; i < 100; i++)
            if ((pixels[i * 100 + 50] & 0xffffff) != 0xffffff) { if (first < 0) first = i; last = i; }
        printf("column 50 drawn from %d to %d\n", first, last);
    }
    {
        /* GDI+ drawing the same metafile into a 100x100 bitmap */
        GpMetafile *copy;
        GpImage *bitmap;
        GpRectF bounds;
        float dpi = 0;
        int unit, i, first = -1, last = -1;
        DWORD argb;

        GdipCreateMetafileFromEmf(CopyEnhMetaFileW(hemf, NULL), TRUE, &copy);
        GdipGetImageBounds(copy, &bounds, &unit);
        GdipGetImageHorizontalResolution(copy, &dpi);
        printf("GDI+ bounds %.2f,%.2f %.2fx%.2f unit %d, dpi %.2f\n", bounds.X, bounds.Y, bounds.Width, bounds.Height,
               unit, dpi);
        GdipCreateBitmapFromScan0(100, 100, 0, 0xe200b /* PixelFormat32bppPARGB */, NULL, &bitmap);
        GdipGetImageGraphicsContext(bitmap, &graphics);
        GdipDrawImageRectI(graphics, copy, 0, 0, 100, 100);
        GdipDeleteGraphics(graphics);
        for (i = 0; i < 100; i++)
        {
            GdipBitmapGetPixel(bitmap, i, 50, &argb);
            if (argb >> 24) { if (first < 0) first = i; last = i; }
        }
        printf("GDI+ row 50 drawn from %d to %d\n", first, last);
        GdipDisposeImage(bitmap);
        GdipDisposeImage(copy);
    }
    {
        /* the records, by type */
        UINT size = GetEnhMetaFileBits(hemf, 0, NULL), pos = 0;
        BYTE *bits = malloc(size);
        GetEnhMetaFileBits(hemf, size, bits);
        printf("records:");
        while (pos + 8 <= size)
        {
            const EMR *emr = (const EMR *)(bits + pos);
            printf(" %lu", emr->iType);
            if (emr->iType == EMR_STRETCHDIBITS)
            {
                const EMRSTRETCHDIBITS *s = (const EMRSTRETCHDIBITS *)emr;
                printf("(dest %ld,%ld %ldx%ld src %ld,%ld %ldx%ld rop %#lx)", s->xDest, s->yDest, s->cxDest, s->cyDest,
                       s->xSrc, s->ySrc, s->cxSrc, s->cySrc, s->dwRop);
            }
            else if (emr->iType == EMR_FILLRGN || emr->iType == EMR_POLYGON16 || emr->iType == EMR_POLYPOLYGON16)
            {
                const RECTL *b = (const RECTL *)(bits + pos + 8);
                printf("(bounds %ld,%ld-%ld,%ld)", b->left, b->top, b->right, b->bottom);
            }
            if (!emr->nSize) break;
            pos += emr->nSize;
        }
        printf("\n");
        free(bits);
    }
    return 0;
}
