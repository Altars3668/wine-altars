/*
 * How GDI measures and draws characters that are meant not to show: the zero-width space (U+200B), which Office puts
 * in the names of the menu groups it merges during in-place activation, the other format and default-ignorable
 * characters, and the controls.  For the menu font and a few others: the face, and for each character whether the
 * font has a glyph, GetCharWidth32, how much wider "a<c>a" is than "aa" with GetTextExtentPoint32 and DrawText, the
 * advance GetTextExtentExPoint gives the character in "a<c>a", how much wider "a<c>a" is than "aa" with a character
 * extra of 5, whether ExtTextOut draws "a<c>a" exactly as "aa", and what ExtTextOut does with a width the caller
 * passes for the character: "kept" if it draws exactly as "aa" with that width added to the first "a", "dropped" if
 * exactly as "aa" without it (which only means something for a character that draws nothing), and whether with
 * ETO_IGNORELANGUAGE it still draws "a<c>a" exactly as "aa".  Then for each font, which of the C0 and C1 controls
 * add nothing to the extent of "a<c>a" and draw nothing.  Last, in Arial, how GetTextExtentExPoint counts such a
 * character among those that fit: in "One<c>two" with the extent of the character as the limit and one past it, and
 * in "<c>One" with limits of 0 and 1.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static const WCHAR chars[] =
{
    0x0000, 0x0001, 0x0009, 0x000a, 0x000d, 0x001b, 0x001f, 0x007f, 0x0080, 0x0085, 0x009f, 0x00a0, 0x00ad, 0x034f,
    0x061c, 0x115f, 0x1160, 0x17b4, 0x17b5, 0x180b, 0x180e, 0x200b, 0x200c, 0x200d, 0x200e, 0x200f, 0x2028, 0x2029,
    0x202a, 0x202b, 0x202c, 0x202d, 0x202e, 0x202f, 0x2060, 0x2061, 0x2062, 0x2063, 0x2064, 0x2065, 0x2066, 0x2069,
    0x206a, 0x206b, 0x206e, 0x206f, 0x3164, 0xfe00, 0xfe01, 0xfe0e, 0xfe0f, 0xfeff, 0xffa0, 0xfff0, 0xfff9, 0xfffb,
    0xfffe, 0xe000,
};

#define BITS_WIDTH  256
#define BITS_HEIGHT 64

static HDC draw_dc;
static DWORD *bits;
static DWORD first_bits[BITS_WIDTH * BITS_HEIGHT];

/* draws the first string, then the second, and tells whether the two drawings are the same */
static BOOL same_drawing_flags(UINT flags, const WCHAR *first, int first_count, const INT *first_dx,
                               const WCHAR *second, int second_count, const INT *second_dx)
{
    PatBlt(draw_dc, 0, 0, BITS_WIDTH, BITS_HEIGHT, WHITENESS);
    ExtTextOutW(draw_dc, 4, 4, flags, NULL, first, first_count, first_dx);
    GdiFlush();
    memcpy(first_bits, bits, sizeof(first_bits));
    PatBlt(draw_dc, 0, 0, BITS_WIDTH, BITS_HEIGHT, WHITENESS);
    ExtTextOutW(draw_dc, 4, 4, flags, NULL, second, second_count, second_dx);
    GdiFlush();
    return !memcmp(first_bits, bits, sizeof(first_bits));
}

static BOOL same_drawing(const WCHAR *first, int first_count, const INT *first_dx,
                         const WCHAR *second, int second_count, const INT *second_dx)
{
    return same_drawing_flags(0, first, first_count, first_dx, second, second_count, second_dx);
}

static void font_report(const WCHAR *face_name, const LOGFONTW *base, BYTE charset)
{
    LOGFONTW lf = *base;
    HDC hdc = GetDC(0);
    HFONT font, old, old_draw;
    WCHAR face[LF_FACESIZE];
    char name[128];
    SIZE base_size;
    RECT base_rect = {0};
    INT a_width;
    unsigned int i;

    if (face_name) lstrcpyW(lf.lfFaceName, face_name);
    if (charset) lf.lfCharSet = charset;
    font = CreateFontIndirectW(&lf);
    old = SelectObject(hdc, font);
    old_draw = SelectObject(draw_dc, font);
    GetTextFaceW(hdc, LF_FACESIZE, face);
    WideCharToMultiByte(CP_UTF8, 0, face, -1, name, sizeof(name), NULL, NULL);
    printf("%s:\n", face_name ? name : "menu font");
    GetTextExtentPoint32W(hdc, L"aa", 2, &base_size);
    DrawTextW(hdc, L"aa", 2, &base_rect, DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
    GetCharWidth32W(draw_dc, 'a', 'a', &a_width);
    for (i = 0; i < ARRAY_SIZE(chars); i++)
    {
        WCHAR str[3] = { 'a', chars[i], 'a' };
        INT ex_dx[3] = {0};
        INT pass_dx[3] = { a_width, 7, a_width }, kept_dx[2] = { a_width + 7, a_width }, dropped_dx[2] = { a_width, a_width };
        const char *passed;
        WORD index;
        INT width = -1;
        SIZE size, ex_size, extra_size, extra_base;
        RECT rect = {0};

        GetGlyphIndicesW(hdc, &chars[i], 1, &index, GGI_MARK_NONEXISTING_GLYPHS);
        GetCharWidth32W(hdc, chars[i], chars[i], &width);
        GetTextExtentPoint32W(hdc, str, 3, &size);
        DrawTextW(hdc, str, 3, &rect, DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
        GetTextExtentExPointW(hdc, str, 3, 0, NULL, ex_dx, &ex_size);
        SetTextCharacterExtra(hdc, 5);
        GetTextExtentPoint32W(hdc, L"aa", 2, &extra_base);
        GetTextExtentPoint32W(hdc, str, 3, &extra_size);
        SetTextCharacterExtra(hdc, 0);

        if (same_drawing(str, 3, pass_dx, L"aa", 2, kept_dx)) passed = "kept";
        else if (same_drawing(str, 3, pass_dx, L"aa", 2, dropped_dx)) passed = "dropped";
        else passed = "other";

        printf("  U+%04X glyph %-4s width %2d, extent +%2ld, DrawText +%2ld, ex %2d, extra +%2ld, drawn %s, width passed %s, "
               "ignoring language %s\n",
               chars[i], index == 0xffff ? "none" : "yes", width, size.cx - base_size.cx, rect.right - base_rect.right,
               ex_dx[1] - ex_dx[0], extra_size.cx - extra_base.cx,
               same_drawing(str, 3, NULL, L"aa", 2, NULL) ? "same" : "diff", passed,
               same_drawing_flags(ETO_IGNORELANGUAGE, str, 3, NULL, L"aa", 2, NULL) ? "same" : "diff");
    }
    SelectObject(draw_dc, old_draw);
    SelectObject(hdc, old);
    DeleteObject(font);
    ReleaseDC(0, hdc);
}

static void controls_report(const WCHAR *face_name, const LOGFONTW *base, BYTE charset)
{
    LOGFONTW lf = *base;
    HDC hdc = GetDC(0);
    HFONT font, old, old_draw;
    char name[128];
    SIZE base_size;
    WCHAR ch;

    if (face_name) lstrcpyW(lf.lfFaceName, face_name);
    if (charset) lf.lfCharSet = charset;
    font = CreateFontIndirectW(&lf);
    old = SelectObject(hdc, font);
    old_draw = SelectObject(draw_dc, font);
    WideCharToMultiByte(CP_UTF8, 0, face_name ? face_name : L"menu font", -1, name, sizeof(name), NULL, NULL);
    printf("%s controls taking no room and drawing nothing:", name);
    GetTextExtentPoint32W(hdc, L"aa", 2, &base_size);
    for (ch = 0; ch < 0xa0; ch++)
    {
        WCHAR str[3] = { 'a', ch, 'a' };
        SIZE size;

        if (ch == 0x20) ch = 0x7f;
        GetTextExtentPoint32W(hdc, str, 3, &size);
        if (size.cx == base_size.cx && same_drawing(str, 3, NULL, L"aa", 2, NULL)) printf(" %02x", ch);
    }
    printf("\n");
    SelectObject(draw_dc, old_draw);
    SelectObject(hdc, old);
    DeleteObject(font);
    ReleaseDC(0, hdc);
}

static void fit_report(void)
{
    static const WCHAR fit_chars[] =
    {
        0x0009, 0x000a, 0x000d, 0x001c, 0x001f, 0x0080, 0x0085, 0x009f, 0x034f, 0x061c, 0x200b, 0x200c, 0x200d,
        0x200e, 0x200f, 0x2029, 0x202a, 0x202e, 0x2061, 0x2064, 0x206a, 0x206f, 0xfe00, 0xfe0f, 0xfeff,
    };
    LOGFONTA lf = {0};
    HDC hdc = GetDC(0);
    HFONT font, old;
    unsigned int i;

    strcpy(lf.lfFaceName, "Arial");
    lf.lfHeight = 20;
    font = CreateFontIndirectA(&lf);
    old = SelectObject(hdc, font);
    printf("fitting, Arial:\n");
    for (i = 0; i < ARRAY_SIZE(fit_chars); i++)
    {
        WCHAR mid[7] = { 'O', 'n', 'e', fit_chars[i], 't', 'w', 'o' }, lead[4] = { fit_chars[i], 'O', 'n', 'e' };
        INT ext[7], fit_at, fit_past, lead_at_0, lead_at_1;
        SIZE size;

        GetTextExtentExPointW(hdc, mid, 7, 32767, NULL, ext, &size);
        GetTextExtentExPointW(hdc, mid, 7, ext[3], &fit_at, NULL, &size);
        GetTextExtentExPointW(hdc, mid, 7, ext[3] + 1, &fit_past, NULL, &size);
        GetTextExtentExPointW(hdc, lead, 4, 0, &lead_at_0, NULL, &size);
        GetTextExtentExPointW(hdc, lead, 4, 1, &lead_at_1, NULL, &size);
        printf("  U+%04X after \"One\": %d fit at its extent, %d one past; before it: %d fit at 0, %d at 1\n",
               fit_chars[i], fit_at, fit_past, lead_at_0, lead_at_1);
    }
    SelectObject(hdc, old);
    DeleteObject(font);
    ReleaseDC(0, hdc);
}

int main(void)
{
    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    BITMAPINFO info = {{ sizeof(info.bmiHeader), BITS_WIDTH, -BITS_HEIGHT, 1, 32, BI_RGB }};
    WCHAR face[LF_FACESIZE];
    char name[128];
    HDC hdc = GetDC(0);
    HFONT font, old;
    HBITMAP dib;

    draw_dc = CreateCompatibleDC(hdc);
    dib = CreateDIBSection(hdc, &info, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    SelectObject(draw_dc, dib);
    SetBkMode(draw_dc, TRANSPARENT);
    SetTextColor(draw_dc, RGB(0, 0, 0));

    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    font = CreateFontIndirectW(&ncm.lfMenuFont);
    old = SelectObject(hdc, font);
    GetTextFaceW(hdc, LF_FACESIZE, face);
    WideCharToMultiByte(CP_UTF8, 0, face, -1, name, sizeof(name), NULL, NULL);
    printf("menu font face \"%s\"\n", name);
    SelectObject(hdc, old);
    DeleteObject(font);
    ReleaseDC(0, hdc);

    font_report(NULL, &ncm.lfMenuFont, 0);
    font_report(L"Tahoma", &ncm.lfMenuFont, 0);
    font_report(L"Courier New", &ncm.lfMenuFont, 0);
    font_report(L"Wingdings", &ncm.lfMenuFont, SYMBOL_CHARSET);
    font_report(L"Marlett", &ncm.lfMenuFont, SYMBOL_CHARSET);
    controls_report(NULL, &ncm.lfMenuFont, 0);
    controls_report(L"Tahoma", &ncm.lfMenuFont, 0);
    controls_report(L"Courier New", &ncm.lfMenuFont, 0);
    controls_report(L"Wingdings", &ncm.lfMenuFont, SYMBOL_CHARSET);
    controls_report(L"Marlett", &ncm.lfMenuFont, SYMBOL_CHARSET);
    fit_report();

    DeleteDC(draw_dc);
    DeleteObject(dib);
    return 0;
}
