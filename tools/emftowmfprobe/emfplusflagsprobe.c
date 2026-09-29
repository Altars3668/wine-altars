/*
 * emfplusflagsprobe - which bit of an EMF+ header's EmfPlusFlags says "recorded for a display"?
 *
 * GDI+ plays page units of UnitDisplay as pixels for a metafile recorded for a display, and as 1/100 inch for one
 * recorded for a printer.  Wine writes the display flag as bit 0 and reads it back from bit 31, so a metafile it
 * records itself comes back as a printer's one, 4% too small at 96 dpi.  This records a red 50x50 square at
 * 25,25 in UnitDisplay and in UnitPixel, prints the flags GDI+ wrote and reports, and then, with EmfPlusFlags
 * patched to each of 0, 1, 0x80000000 and 0x80000001, where GDI+ draws the square into a 100x100 bitmap.
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
GpStatus WINAPI GdipSetPageUnit(GpGraphics *, int);
GpStatus WINAPI GdipGetPageUnit(GpGraphics *, int *);
GpStatus WINAPI GdipCreateSolidFill(DWORD, GpBrush **);
GpStatus WINAPI GdipFillRectangleI(GpGraphics *, GpBrush *, INT, INT, INT, INT);
GpStatus WINAPI GdipDeleteGraphics(GpGraphics *);
GpStatus WINAPI GdipDeleteBrush(GpBrush *);
GpStatus WINAPI GdipGetHemfFromMetafile(GpMetafile *, HENHMETAFILE *);
GpStatus WINAPI GdipDisposeImage(GpImage *);
GpStatus WINAPI GdipCreateMetafileFromEmf(HENHMETAFILE, BOOL, GpMetafile **);
GpStatus WINAPI GdipGetMetafileHeaderFromEmf(HENHMETAFILE, MetafileHeader *);
GpStatus WINAPI GdipCreateBitmapFromScan0(INT, INT, INT, INT, BYTE *, GpImage **);
GpStatus WINAPI GdipDrawImageRectI(GpGraphics *, GpImage *, INT, INT, INT, INT);
GpStatus WINAPI GdipBitmapGetPixel(GpImage *, INT, INT, DWORD *);

/* the EmfPlusFlags field of the EMF+ header record in the first comment */
static DWORD *find_flags(BYTE *bits, UINT size)
{
    UINT pos = 0;

    while (pos + 8 <= size)
    {
        EMR *emr = (EMR *)(bits + pos);
        if (emr->iType == EMR_GDICOMMENT && emr->nSize >= 12 + 4 + 28 && *(DWORD *)(bits + pos + 12) == 0x2b464d45
            && *(WORD *)(bits + pos + 16) == 0x4001)
            return (DWORD *)(bits + pos + 16 + 16);
        if (!emr->nSize) break;
        pos += emr->nSize;
    }
    return NULL;
}

static void draw(HENHMETAFILE hemf, const char *what)
{
    GpMetafile *metafile;
    GpGraphics *graphics;
    MetafileHeader header;
    GpImage *bitmap;
    int i, first = -1, last = -1;
    DWORD argb;

    memset(&header, 0, sizeof(header));
    GdipGetMetafileHeaderFromEmf(hemf, &header);
    GdipCreateMetafileFromEmf(hemf, TRUE, &metafile);
    GdipCreateBitmapFromScan0(100, 100, 0, 0xe200b /* PixelFormat32bppPARGB */, NULL, &bitmap);
    GdipGetImageGraphicsContext(bitmap, &graphics);
    GdipDrawImageRectI(graphics, metafile, 0, 0, 100, 100);
    GdipDeleteGraphics(graphics);
    for (i = 0; i < 100; i++)
    {
        GdipBitmapGetPixel(bitmap, i, 50, &argb);
        if (argb >> 24) { if (first < 0) first = i; last = i; }
    }
    printf("  %s: header EmfPlusFlags %#x, logical dpi %d, drawn from %d to %d\n", what, header.EmfPlusFlags,
           header.LogicalDpiX, first, last);
    GdipDisposeImage(bitmap);
    GdipDisposeImage(metafile);
}

static void probe(HDC ref, const char *ref_name, int unit)
{
    static const DWORD values[] = { 0, 1, 0x80000000, 0x80000001, 2 };
    GpRectF frame = { 0, 0, 100, 100 };
    GpMetafile *metafile;
    GpGraphics *graphics;
    GpBrush *brush;
    HENHMETAFILE hemf;
    UINT size, i;
    BYTE *bits;
    DWORD *flags;
    int page_unit = -1;
    char what[64];

    if (GdipRecordMetafile(ref, 5 /* EmfTypeEmfPlusDual */, &frame, 2 /* MetafileFrameUnitPixel */, L"probe",
                           &metafile))
    {
        printf("%s: recording failed\n", ref_name);
        return;
    }
    GdipGetImageGraphicsContext(metafile, &graphics);
    GdipGetPageUnit(graphics, &page_unit);
    if (unit >= 0) GdipSetPageUnit(graphics, unit);
    GdipCreateSolidFill(0xffff0000, &brush);
    GdipFillRectangleI(graphics, brush, 25, 25, 50, 50);
    GdipDeleteBrush(brush);
    GdipDeleteGraphics(graphics);
    GdipGetHemfFromMetafile(metafile, &hemf);
    GdipDisposeImage(metafile);

    size = GetEnhMetaFileBits(hemf, 0, NULL);
    bits = malloc(size);
    GetEnhMetaFileBits(hemf, size, bits);
    flags = find_flags(bits, size);
    printf("%s, default page unit %d, drawn in unit %d: EmfPlusFlags written %#lx, LogicalDpi %lu\n", ref_name,
           page_unit, unit >= 0 ? unit : page_unit, flags ? *flags : 0xdeadbeef, flags ? flags[1] : 0);
    draw(CopyEnhMetaFileW(hemf, NULL), "as recorded");
    for (i = 0; flags && i < ARRAY_SIZE(values); i++)
    {
        *flags = values[i];
        sprintf(what, "patched to %#lx", values[i]);
        draw(SetEnhMetaFileBits(size, bits), what);
    }
    free(bits);
    DeleteEnhMetaFile(hemf);
}

int main(void)
{
    GdiplusStartupInput input = { 1 };
    ULONG_PTR token;
    HDC screen = GetDC(NULL), mem = CreateCompatibleDC(0);

    GdiplusStartup(&token, &input, NULL);
    probe(screen, "screen DC", -1);
    probe(screen, "screen DC", 1 /* UnitDisplay */);
    probe(screen, "screen DC", 2 /* UnitPixel */);
    probe(mem, "memory DC", -1);
    ReleaseDC(NULL, screen);
    DeleteDC(mem);
    printf("done\n");
    return 0;
}
