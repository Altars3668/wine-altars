/*
 * glyphrasterprobe - how GDI renders the glyphs of a font file loaded for this process only: the text metrics, and for
 * a few characters GetGlyphOutline's metrics and bitmaps (GGO_BITMAP, GGO_GRAY8_BITMAP) and ExtTextOut into a
 * one-bit DIB as tall as the font, each described by its black pixels: how many, their bounding box, and the columns
 * that are black from top to bottom.  Word draws the glyphs of a font it cannot embed in a PDF this way.
 *
 *   glyphrasterprobe <font file> <face name> [height...]   (default heights -16 and -92)
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static void describe_bits(const char *what, const BYTE *bits, int width, int height, int stride, BOOL one_bit, BOOL black_is_zero)
{
    int x, y, count = 0, left = width, right = -1, top = height, bottom = -1, full = 0;

    for (x = 0; x < width; x++)
    {
        int column = 0;
        for (y = 0; y < height; y++)
        {
            int on = one_bit ? (bits[y * stride + x / 8] >> (7 - x % 8)) & 1 : bits[y * stride + x] != 0;
            if (black_is_zero) on = !on;
            if (!on) continue;
            count++; column++;
            if (x < left) left = x;
            if (x > right) right = x;
            if (y < top) top = y;
            if (y > bottom) bottom = y;
        }
        if (height > 8 && column == height) full++;
    }
    if (!count) printf("    %s %dx%d: nothing\n", what, width, height);
    else printf("    %s %dx%d: %d pixels in (%d,%d)-(%d,%d), %d columns top to bottom\n", what, width, height, count,
                left, top, right, bottom, full);
}

static double fixed(FIXED f) { return f.value + f.fract / 65536.0; }

/* the contours of an outline: how many, and for each its curves by type with their numbers of points and the extent */
static void describe_outline(HDC hdc, WCHAR ch, UINT format, const char *what)
{
    static const MAT2 identity = { {0, 1}, {0, 0}, {0, 0}, {0, 1} };
    GLYPHMETRICS gm;
    DWORD size = GetGlyphOutlineW(hdc, ch, format, &gm, 0, NULL, &identity), at = 0;
    double minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;
    char line[1024];
    int contours = 0, len = 0;
    BYTE *buf;

    if (size == GDI_ERROR || !size || !(buf = malloc(size))) { printf("    %s: %ld\n", what, (long)size); return; }
    GetGlyphOutlineW(hdc, ch, format, &gm, size, buf, &identity);
    line[0] = 0;
    while (at + sizeof(TTPOLYGONHEADER) <= size)
    {
        TTPOLYGONHEADER *header = (TTPOLYGONHEADER *)(buf + at);
        DWORD curve_at = at + sizeof(*header);

        if (header->dwType != TT_POLYGON_TYPE || header->cb < sizeof(*header) || at + header->cb > size) break;
        contours++;
        if (len < 900) len += sprintf(line + len, " [");
        while (curve_at < at + header->cb)
        {
            TTPOLYCURVE *curve = (TTPOLYCURVE *)(buf + curve_at);
            int k;
            for (k = 0; k < curve->cpfx; k++)
            {
                double x = fixed(curve->apfx[k].x), y = fixed(curve->apfx[k].y);
                if (x < minx) minx = x; if (x > maxx) maxx = x; if (y < miny) miny = y; if (y > maxy) maxy = y;
            }
            if (len < 900) len += sprintf(line + len, "%s%d", curve->wType == TT_PRIM_LINE ? "L" : curve->wType == TT_PRIM_QSPLINE ? "Q" :
                                          curve->wType == TT_PRIM_CSPLINE ? "C" : "?", curve->cpfx);
            curve_at += sizeof(*curve) + (curve->cpfx - 1) * sizeof(POINTFX);
        }
        if (len < 900) len += sprintf(line + len, "]");
        at += header->cb;
    }
    printf("    %s: %ld bytes, %d contours, points in (%.1f,%.1f)-(%.1f,%.1f):%s\n", what, (long)size, contours, minx, miny,
           maxx, maxy, line);
    free(buf);
}

int main(int argc, char **argv)
{
    WCHAR path[MAX_PATH], face[LF_FACESIZE];
    static const WCHAR chars[] = { 'a', '2', '+', '=', 'x', 0x221a, 0x00b1 };
    static const MAT2 identity = { {0, 1}, {0, 0}, {0, 0}, {0, 1} };
    static const WORD glyphs[] = { 0x510, 0x3f5, 0xc05, 0xa3b, 0x527, 0x512, 0x511, 0x1e, 0x15, 0x13, 0xc };
    int heights[8] = { -16, -92 }, nheights = 2, h, i;
    HDC hdc = CreateCompatibleDC(NULL);

    if (argc < 3) return 2;
    MultiByteToWideChar(CP_ACP, 0, argv[1], -1, path, MAX_PATH);
    MultiByteToWideChar(CP_ACP, 0, argv[2], -1, face, LF_FACESIZE);
    if (argc > 3) for (nheights = 0; nheights < 8 && 3 + nheights < argc; nheights++) heights[nheights] = atoi(argv[3 + nheights]);
    printf("AddFontResourceEx: %d\n", AddFontResourceExW(path, FR_PRIVATE, NULL));

    for (h = 0; h < nheights; h++)
    {
        LOGFONTW lf = {0};
        TEXTMETRICW tm;
        HFONT font, old;
        WCHAR name[LF_FACESIZE];

        lf.lfHeight = heights[h];
        lf.lfQuality = NONANTIALIASED_QUALITY;
        lstrcpyW(lf.lfFaceName, face);
        font = CreateFontIndirectW(&lf);
        old = SelectObject(hdc, font);
        GetTextFaceW(hdc, LF_FACESIZE, name);
        GetTextMetricsW(hdc, &tm);
        printf("height %d: face %ls, tmHeight %ld ascent %ld descent %ld internal %ld avgwidth %ld maxwidth %ld\n",
               heights[h], name, tm.tmHeight, tm.tmAscent, tm.tmDescent, tm.tmInternalLeading, tm.tmAveCharWidth,
               tm.tmMaxCharWidth);
        for (i = 0; i < ARRAY_SIZE(chars); i++)
        {
            GLYPHMETRICS gm;
            DWORD size;
            BYTE *buf;
            WORD glyph;
            SIZE extent;

            GetGlyphIndicesW(hdc, &chars[i], 1, &glyph, GGI_MARK_NONEXISTING_GLYPHS);
            GetTextExtentPoint32W(hdc, &chars[i], 1, &extent);
            size = GetGlyphOutlineW(hdc, chars[i], GGO_METRICS, &gm, 0, NULL, &identity);
            printf("  U+%04X glyph %u, extent %ldx%ld: metrics %ld, box %ux%u at (%ld,%ld), advance %d\n", chars[i],
                   glyph, extent.cx, extent.cy, (long)size, gm.gmBlackBoxX, gm.gmBlackBoxY, gm.gmptGlyphOrigin.x,
                   gm.gmptGlyphOrigin.y, gm.gmCellIncX);
            describe_outline(hdc, chars[i], GGO_NATIVE, "GGO_NATIVE");
            describe_outline(hdc, chars[i], GGO_BEZIER, "GGO_BEZIER");
            size = GetGlyphOutlineW(hdc, chars[i], GGO_BITMAP, &gm, 0, NULL, &identity);
            if (size != GDI_ERROR && size && (buf = malloc(size)))
            {
                GetGlyphOutlineW(hdc, chars[i], GGO_BITMAP, &gm, size, buf, &identity);
                describe_bits("GGO_BITMAP", buf, gm.gmBlackBoxX, gm.gmBlackBoxY, ((gm.gmBlackBoxX + 31) / 32) * 4, TRUE, FALSE);
                free(buf);
            }
            else printf("    GGO_BITMAP: %ld\n", (long)size);
            size = GetGlyphOutlineW(hdc, chars[i], GGO_GRAY8_BITMAP, &gm, 0, NULL, &identity);
            if (size != GDI_ERROR && size && (buf = malloc(size)))
            {
                GetGlyphOutlineW(hdc, chars[i], GGO_GRAY8_BITMAP, &gm, size, buf, &identity);
                describe_bits("GGO_GRAY8", buf, gm.gmBlackBoxX, gm.gmBlackBoxY, (gm.gmBlackBoxX + 3) & ~3, FALSE, FALSE);
                free(buf);
            }
            else printf("    GGO_GRAY8: %ld\n", (long)size);
            {
                /* ExtTextOut into a white one-bit DIB as wide as the extent and as tall as the font */
                BITMAPINFO *bmi = calloc(1, sizeof(BITMAPINFOHEADER) + 2 * sizeof(RGBQUAD));
                HDC mem = CreateCompatibleDC(hdc);
                void *bits;
                HBITMAP dib, prev;
                int width = extent.cx + 8, height = tm.tmHeight, stride = ((width + 31) / 32) * 4;
                HFONT prevfont;

                bmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi->bmiHeader.biWidth = width;
                bmi->bmiHeader.biHeight = -height;
                bmi->bmiHeader.biPlanes = 1;
                bmi->bmiHeader.biBitCount = 1;
                bmi->bmiColors[1].rgbRed = bmi->bmiColors[1].rgbGreen = bmi->bmiColors[1].rgbBlue = 255;
                dib = CreateDIBSection(mem, bmi, DIB_RGB_COLORS, &bits, NULL, 0);
                prev = SelectObject(mem, dib);
                prevfont = SelectObject(mem, font);
                PatBlt(mem, 0, 0, width, height, WHITENESS);
                SetTextColor(mem, RGB(0, 0, 0));
                SetBkMode(mem, TRANSPARENT);
                ExtTextOutW(mem, 4, 0, 0, NULL, &chars[i], 1, NULL);
                GdiFlush();
                describe_bits("ExtTextOut", bits, width, height, stride, TRUE, TRUE);
                SelectObject(mem, prevfont);
                SelectObject(mem, prev);
                DeleteObject(dib);
                DeleteDC(mem);
                free(bmi);
            }
        }
        /* the glyphs Word asks for by index when it draws the equations of this font as images */
        for (i = 0; i < ARRAY_SIZE(glyphs); i++)
        {
            GLYPHMETRICS gm;
            DWORD size;
            BYTE *buf;

            size = GetGlyphOutlineW(hdc, glyphs[i], GGO_GLYPH_INDEX | GGO_METRICS, &gm, 0, NULL, &identity);
            printf("  glyph %#x: metrics %ld, box %ux%u at (%ld,%ld), advance %d\n", glyphs[i], (long)size,
                   gm.gmBlackBoxX, gm.gmBlackBoxY, gm.gmptGlyphOrigin.x, gm.gmptGlyphOrigin.y, gm.gmCellIncX);
            size = GetGlyphOutlineW(hdc, glyphs[i], GGO_GLYPH_INDEX | GGO_BITMAP, &gm, 0, NULL, &identity);
            if (size != GDI_ERROR && size && (buf = malloc(size)))
            {
                GetGlyphOutlineW(hdc, glyphs[i], GGO_GLYPH_INDEX | GGO_BITMAP, &gm, size, buf, &identity);
                printf("    size %ld;", (long)size);
                describe_bits("GGO_BITMAP", buf, gm.gmBlackBoxX, gm.gmBlackBoxY, ((gm.gmBlackBoxX + 31) / 32) * 4, TRUE, FALSE);
                free(buf);
            }
            else printf("    GGO_BITMAP: %ld\n", (long)size);
        }
        SelectObject(hdc, old);
        DeleteObject(font);
    }
    return 0;
}
