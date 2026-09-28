/*
 * outlineprobe - what GetGlyphOutline returns for the glyphs of a font file loaded for this process only: the return
 * value of GGO_METRICS (and of the bitmap formats) for each font quality and with a rotating matrix, next to the black
 * box it is measured against, and every record of GGO_NATIVE and GGO_BEZIER, hinted and with GGO_UNHINTED, with its
 * points to the last bit of their 16.16 fixed-point values.
 *
 *   outlineprobe <font file or -> <face name> <height> [text]     (default text "a2+O")
 *
 * With "-" the face is taken from the installed fonts.  A text of the form #n,m,... names glyph indices instead.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static const MAT2 identity = { {0, 1}, {0, 0}, {0, 0}, {0, 1} };

static double fixed(FIXED f) { return f.value + f.fract / 65536.0; }

static void dump_outline(HDC hdc, UINT ch, UINT format, const char *what)
{
    GLYPHMETRICS gm;
    DWORD size = GetGlyphOutlineW(hdc, ch, format, &gm, 0, NULL, &identity), at = 0;
    BYTE *buf;

    if (size == GDI_ERROR || !size || !(buf = malloc(size)))
    {
        printf("    %s: %ld\n", what, (long)size);
        return;
    }
    memset(buf, 0xcc, size);
    printf("    %s: %ld bytes, returned %ld\n", what, (long)size, (long)GetGlyphOutlineW(hdc, ch, format, &gm, size, buf, &identity));
    while (at + sizeof(TTPOLYGONHEADER) <= size)
    {
        TTPOLYGONHEADER *header = (TTPOLYGONHEADER *)(buf + at);
        DWORD curve_at = at + sizeof(*header);

        if (header->dwType != TT_POLYGON_TYPE || header->cb < sizeof(*header) || at + header->cb > size)
        {
            printf("      bad header type %lu cb %lu at %lu\n", header->dwType, header->cb, at);
            break;
        }
        printf("      contour %lu bytes, start (%.5f,%.5f)\n", header->cb, fixed(header->pfxStart.x), fixed(header->pfxStart.y));
        while (curve_at < at + header->cb)
        {
            TTPOLYCURVE *curve = (TTPOLYCURVE *)(buf + curve_at);
            int k;

            printf("        %s%d", curve->wType == TT_PRIM_LINE ? "L" : curve->wType == TT_PRIM_QSPLINE ? "Q" :
                   curve->wType == TT_PRIM_CSPLINE ? "C" : "?", curve->cpfx);
            for (k = 0; k < curve->cpfx; k++)
                printf(" (%.5f,%.5f)", fixed(curve->apfx[k].x), fixed(curve->apfx[k].y));
            printf("\n");
            curve_at += sizeof(*curve) + (curve->cpfx - 1) * sizeof(POINTFX);
        }
        at += header->cb;
    }
    if (at != size) printf("      %lu bytes left over\n", size - at);
    free(buf);
}

static void print_metrics(HDC hdc, UINT ch, UINT format, const MAT2 *matrix, const char *what)
{
    GLYPHMETRICS gm;
    DWORD ret;

    memset(&gm, 0xcc, sizeof(gm));
    ret = GetGlyphOutlineW(hdc, ch, format, &gm, 0, NULL, matrix);
    printf("    %s: %ld, box %ux%u at (%ld,%ld), cell %d,%d\n", what, (long)ret, gm.gmBlackBoxX, gm.gmBlackBoxY,
           gm.gmptGlyphOrigin.x, gm.gmptGlyphOrigin.y, gm.gmCellIncX, gm.gmCellIncY);
}

int main(int argc, char **argv)
{
    static const BYTE qualities[] = { DEFAULT_QUALITY, NONANTIALIASED_QUALITY, ANTIALIASED_QUALITY, CLEARTYPE_QUALITY };
    static const char *quality_names[] = { "default", "nonantialiased", "antialiased", "cleartype" };
    WCHAR path[MAX_PATH], face[LF_FACESIZE], text[256];
    UINT chars[256], count = 0, glyph_index = 0;
    HDC hdc = CreateCompatibleDC(NULL);
    MAT2 rotate;
    int height, q;
    UINT i;

    if (argc < 4) return 2;
    MultiByteToWideChar(CP_ACP, 0, argv[1], -1, path, MAX_PATH);
    MultiByteToWideChar(CP_ACP, 0, argv[2], -1, face, LF_FACESIZE);
    height = atoi(argv[3]);
    if (argc > 4 && argv[4][0] == '#')
    {
        char *p = argv[4] + 1;
        glyph_index = GGO_GLYPH_INDEX;
        while (*p && count < ARRAY_SIZE(chars))
        {
            chars[count++] = strtoul(p, &p, 0);
            if (*p == ',') p++;
        }
    }
    else
    {
        MultiByteToWideChar(CP_UTF8, 0, argc > 4 ? argv[4] : "a2+O", -1, text, ARRAY_SIZE(text));
        for (i = 0; text[i] && count < ARRAY_SIZE(chars); i++) chars[count++] = text[i];
    }
    if (lstrcmpW(path, L"-")) printf("AddFontResourceEx: %d\n", AddFontResourceExW(path, FR_PRIVATE, NULL));

    /* 30 degrees */
    rotate.eM11.value = 0; rotate.eM11.fract = 56756;
    rotate.eM12.value = 0; rotate.eM12.fract = 32768;
    rotate.eM21.value = -1; rotate.eM21.fract = 65536 - 32768;
    rotate.eM22.value = 0; rotate.eM22.fract = 56756;

    for (q = 0; q < ARRAY_SIZE(qualities); q++)
    {
        LOGFONTW lf = {0};
        WCHAR name[LF_FACESIZE];
        TEXTMETRICW tm;
        HFONT font, old;

        lf.lfHeight = height;
        lf.lfQuality = qualities[q];
        lstrcpyW(lf.lfFaceName, face);
        font = CreateFontIndirectW(&lf);
        old = SelectObject(hdc, font);
        GetTextFaceW(hdc, LF_FACESIZE, name);
        GetTextMetricsW(hdc, &tm);
        printf("height %d, quality %s: face %ls, tmHeight %ld ascent %ld\n", height, quality_names[q], name, tm.tmHeight,
               tm.tmAscent);
        for (i = 0; i < count; i++)
        {
            printf("  %s %#x\n", glyph_index ? "glyph" : "char", chars[i]);
            print_metrics(hdc, chars[i], glyph_index | GGO_METRICS, &identity, "GGO_METRICS");
            if (q) continue;
            print_metrics(hdc, chars[i], glyph_index | GGO_METRICS | GGO_UNHINTED, &identity, "GGO_METRICS unhinted");
            print_metrics(hdc, chars[i], glyph_index | GGO_METRICS, &rotate, "GGO_METRICS rotated");
            print_metrics(hdc, chars[i], glyph_index | GGO_BITMAP, &identity, "GGO_BITMAP");
            print_metrics(hdc, chars[i], glyph_index | GGO_GRAY2_BITMAP, &identity, "GGO_GRAY2_BITMAP");
            print_metrics(hdc, chars[i], glyph_index | GGO_GRAY8_BITMAP, &identity, "GGO_GRAY8_BITMAP");
            dump_outline(hdc, chars[i], glyph_index | GGO_NATIVE, "GGO_NATIVE");
            dump_outline(hdc, chars[i], glyph_index | GGO_BEZIER, "GGO_BEZIER");
            dump_outline(hdc, chars[i], glyph_index | GGO_NATIVE | GGO_UNHINTED, "GGO_NATIVE unhinted");
            dump_outline(hdc, chars[i], glyph_index | GGO_BEZIER | GGO_UNHINTED, "GGO_BEZIER unhinted");
        }
        DeleteObject(SelectObject(hdc, old));
    }
    if (lstrcmpW(path, L"-")) RemoveFontResourceExW(path, FR_PRIVATE, NULL);
    DeleteDC(hdc);
    return 0;
}
