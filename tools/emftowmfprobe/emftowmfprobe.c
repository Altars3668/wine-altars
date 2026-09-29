/*
 * emftowmfprobe - what GdipEmfToWmfBits() makes of a metafile.
 *
 * PowerPoint exports a slide as WMF through GDI+; Wine's GdipEmfToWmfBits()
 * was a stub, and the export left no file.  For an EMF+ dual metafile, an
 * EMF+ only one and a plain GDI one, this prints what GdipEmfToWmfBits()
 * returns with each flag, the header of the result, and how many records of
 * each function it holds, next to GetWinMetaFileBits() on the same metafile.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <windows.h>
#include <stdio.h>

typedef int GpStatus;
typedef void GpGraphics, GpMetafile, GpBrush, GpPen, GpImage;
typedef struct { float X, Y, Width, Height; } GpRectF;
typedef struct { UINT32 GdiplusVersion; void *DebugEventCallback; BOOL SuppressBackgroundThread, SuppressExternalCodecs; } GdiplusStartupInput;

GpStatus WINAPI GdiplusStartup(ULONG_PTR *, const GdiplusStartupInput *, void *);
GpStatus WINAPI GdipRecordMetafile(HDC, int, const GpRectF *, int, const WCHAR *, GpMetafile **);
GpStatus WINAPI GdipGetImageGraphicsContext(GpImage *, GpGraphics **);
GpStatus WINAPI GdipCreateSolidFill(DWORD, GpBrush **);
GpStatus WINAPI GdipFillRectangle(GpGraphics *, GpBrush *, float, float, float, float);
GpStatus WINAPI GdipCreatePen1(DWORD, float, int, GpPen **);
GpStatus WINAPI GdipDrawLine(GpGraphics *, GpPen *, float, float, float, float);
GpStatus WINAPI GdipDeleteGraphics(GpGraphics *);
GpStatus WINAPI GdipDeleteBrush(GpBrush *);
GpStatus WINAPI GdipDeletePen(GpPen *);
GpStatus WINAPI GdipGetHemfFromMetafile(GpMetafile *, HENHMETAFILE *);
GpStatus WINAPI GdipDisposeImage(GpImage *);
UINT WINAPI GdipEmfToWmfBits(HENHMETAFILE, UINT, BYTE *, INT, INT);

static void describe_wmf(const BYTE *data, UINT size)
{
    const BYTE *p = data, *end = data + size;
    unsigned int counts[0x1000] = {0}, i, total = 0;

    if (size >= 22 && *(const DWORD *)p == 0x9ac6cdd7)
    {
        printf("    placeable header: bbox %d,%d-%d,%d, inch %u\n", *(const SHORT *)(p + 6), *(const SHORT *)(p + 8),
               *(const SHORT *)(p + 10), *(const SHORT *)(p + 12), *(const WORD *)(p + 14));
        p += 22;
    }
    if (end - p < 18)
    {
        printf("    no METAHEADER\n");
        return;
    }
    printf("    METAHEADER type %u, header size %u, version %#x, size %lu words, objects %u, max record %lu\n",
           *(const WORD *)p, *(const WORD *)(p + 2), *(const WORD *)(p + 4), *(const DWORD *)(p + 6),
           *(const WORD *)(p + 10), *(const DWORD *)(p + 12));
    p += 18;
    while (end - p >= 6)
    {
        DWORD words = *(const DWORD *)p;
        WORD function = *(const WORD *)(p + 4);

        if (words < 3 || (end - p) < words * 2) break;
        if (function == 0x0626 && words >= 5)   /* META_ESCAPE */
        {
            WORD escape = *(const WORD *)(p + 6);
            if (escape == 0x000f) counts[0xf00]++;   /* MFCOMMENT */
            else counts[function & 0xfff]++;
        }
        else counts[function & 0xfff]++;
        total++;
        p += words * 2;
    }
    printf("    %u records:", total);
    for (i = 0; i < ARRAY_SIZE(counts); i++)
        if (counts[i]) printf(" %s%#x x%u", i == 0xf00 ? "comment" : "", i == 0xf00 ? 0 : i, counts[i]);
    printf("\n");
}

static void probe(const char *what, HENHMETAFILE emf, HDC hdc)
{
    static const INT flags[] = { 0, 1, 2, 4, 3 };
    ENHMETAHEADER header;
    unsigned int i;
    UINT size;
    BYTE *data;

    GetEnhMetaFileHeader(emf, sizeof(header), &header);
    printf("%s: EMF %lu bytes, %lu records, bounds %ld,%ld-%ld,%ld, frame %ld,%ld-%ld,%ld\n", what, header.nBytes,
           header.nRecords, header.rclBounds.left, header.rclBounds.top, header.rclBounds.right,
           header.rclBounds.bottom, header.rclFrame.left, header.rclFrame.top, header.rclFrame.right,
           header.rclFrame.bottom);
    for (i = 0; i < ARRAY_SIZE(flags); i++)
    {
        SetLastError(0xdeadbeef);
        size = GdipEmfToWmfBits(emf, 0, NULL, MM_ANISOTROPIC, flags[i]);
        printf("  GdipEmfToWmfBits(flags %#x): %u bytes, error %lu\n", flags[i], size, GetLastError());
        if (!size) continue;
        data = malloc(size);
        size = GdipEmfToWmfBits(emf, size, data, MM_ANISOTROPIC, flags[i]);
        describe_wmf(data, size);
        free(data);
    }
    size = GdipEmfToWmfBits(emf, 0, NULL, MM_TEXT, 0);
    printf("  GdipEmfToWmfBits(MM_TEXT): %u bytes\n", size);
    SetLastError(0xdeadbeef);
    size = GetWinMetaFileBits(emf, 0, NULL, MM_ANISOTROPIC, hdc);
    printf("  GetWinMetaFileBits: %u bytes, error %lu\n", size, GetLastError());
    if (size)
    {
        data = malloc(size);
        size = GetWinMetaFileBits(emf, size, data, MM_ANISOTROPIC, hdc);
        describe_wmf(data, size);
        free(data);
    }
}

static HENHMETAFILE record(HDC hdc, int type)
{
    GpRectF frame = { 0, 0, 100, 100 };
    GpMetafile *metafile;
    GpGraphics *graphics;
    HENHMETAFILE emf = NULL;
    GpBrush *brush;
    GpPen *pen;
    GpStatus status;

    status = GdipRecordMetafile(hdc, type, &frame, 2 /* MetafileFrameUnitPixel */, L"emftowmfprobe", &metafile);
    if (status) { printf("GdipRecordMetafile(%d) %d\n", type, status); return NULL; }
    GdipGetImageGraphicsContext(metafile, &graphics);
    GdipCreateSolidFill(0xffff0000, &brush);
    GdipFillRectangle(graphics, brush, 10, 10, 50, 40);
    GdipCreatePen1(0xff0000ff, 3.0f, 2 /* UnitPixel */, &pen);
    GdipDrawLine(graphics, pen, 0, 0, 100, 100);
    GdipDeletePen(pen);
    GdipDeleteBrush(brush);
    GdipDeleteGraphics(graphics);
    GdipGetHemfFromMetafile(metafile, &emf);
    GdipDisposeImage(metafile);
    return emf;
}

int main(void)
{
    GdiplusStartupInput input = { 1 };
    ULONG_PTR token;
    HENHMETAFILE emf;
    HDC hdc = GetDC(NULL), mfdc;
    RECT rect = { 0, 0, 2000, 2000 };

    GdiplusStartup(&token, &input, NULL);
    if ((emf = record(hdc, 5 /* EmfTypeEmfPlusDual */))) { probe("EMF+ dual", emf, hdc); DeleteEnhMetaFile(emf); }
    if ((emf = record(hdc, 4 /* EmfTypeEmfPlusOnly */))) { probe("EMF+ only", emf, hdc); DeleteEnhMetaFile(emf); }
    if ((emf = record(hdc, 3 /* EmfTypeEmfOnly */))) { probe("EMF only (GDI+)", emf, hdc); DeleteEnhMetaFile(emf); }
    mfdc = CreateEnhMetaFileW(hdc, NULL, &rect, NULL);
    Rectangle(mfdc, 10, 10, 60, 50);
    emf = CloseEnhMetaFile(mfdc);
    probe("GDI EMF", emf, hdc);
    DeleteEnhMetaFile(emf);
    {
        /* the placeable header's bounds for other frames, and a frame not at the origin */
        static const RECT frames[] = { { 0, 0, 10000, 5000 }, { 0, 0, 25400, 25400 }, { 1000, 2000, 5000, 7000 },
                                       { 0, 0, 2000, 2000 }, { 0, 0, 2620, 2617 } };
        unsigned int i;
        for (i = 0; i < ARRAY_SIZE(frames); i++)
        {
            UINT size;
            BYTE *data;
            mfdc = CreateEnhMetaFileW(hdc, NULL, &frames[i], NULL);
            Rectangle(mfdc, 10, 10, 60, 50);
            emf = CloseEnhMetaFile(mfdc);
            size = GdipEmfToWmfBits(emf, 0, NULL, MM_ANISOTROPIC, 2);
            data = malloc(size);
            size = GdipEmfToWmfBits(emf, size, data, MM_ANISOTROPIC, 2);
            printf("frame %ld,%ld-%ld,%ld: ", frames[i].left, frames[i].top, frames[i].right, frames[i].bottom);
            if (size >= 22)
            {
                const BYTE *p = data + 22 + 18;
                printf("bbox %d,%d-%d,%d inch %u checksum %#x", *(SHORT *)(data + 6), *(SHORT *)(data + 8),
                       *(SHORT *)(data + 10), *(SHORT *)(data + 12), *(WORD *)(data + 14), *(WORD *)(data + 20));
                while (p + 6 <= data + size && *(DWORD *)p >= 3)
                {
                    WORD function = *(WORD *)(p + 4);
                    if (function == 0x20b || function == 0x20c)
                        printf(", %s %d,%d", function == 0x20b ? "window org" : "window ext",
                               *(SHORT *)(p + 8), *(SHORT *)(p + 6));
                    p += *(DWORD *)p * 2;
                }
                printf("\n");
            }
            else printf("%u bytes\n", size);
            size = GdipEmfToWmfBits(emf, 10, data, MM_ANISOTROPIC, 2);
            printf("  into 10 bytes: %u\n", size);
            free(data);
            DeleteEnhMetaFile(emf);
        }
    }
    ReleaseDC(NULL, hdc);
    printf("done\n");
    return 0;
}
