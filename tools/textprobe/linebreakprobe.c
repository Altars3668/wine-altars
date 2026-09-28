/*
 * Where DrawText breaks a line.  For each case, in Arial: the text, how many of its characters fit in the width, how
 * many characters the first line takes (uiLengthDrawn of DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL in a rectangle
 * one line high), and whether without DT_EDITCONTROL the text is laid out wider than that width (a word that can
 * not be broken): breaks at white space of each kind, at a zero-width space and after a hyphen, and next to East
 * Asian wide characters.  Then, in SimSun, for punctuation c, whether a line may start with it -- "文件c编辑" in the
 * width of "文件": 2 if it may, 1 if the line backs up -- and end with it -- "文c编辑视" in the width of "文c".
 */
#include <windows.h>
#include <stdio.h>

static const struct
{
    const char *name;
    const WCHAR *str;
    int fit_chars;
}
cases[] =
{
    { "space", L"Hello World wide", 8 },
    { "space at the edge", L"Hello World wide", 6 },
    { "tab", L"Hello\tWorld wide", 8 },
    { "tab at the edge", L"Hello\tWorld wide", 6 },
    { "tab just past", L"Hello\tWorld wide", 5 },
    { "two tabs", L"Hello\t\tWorld wide", 9 },
    { "space and tab", L"Hello \tWorld wide", 9 },
    { "tab and space", L"Hello\t World wide", 9 },
    { "two spaces", L"Hello  World wide", 9 },
    { "three spaces", L"Hello   World wide", 10 },
    { "ideographic space", L"Hello\x3000World wide", 8 },
    { "no-break space", L"Hello\x00a0World wide", 8 },
    { "vertical tab", L"Hello\x000bWorld wide", 8 },
    { "en space", L"Hello\x2002World wide", 8 },
    { "zero-width space", L"Hello\x200bWorld wide", 8 },
    { "zero-width space at the edge", L"Hello\x200bWorld wide", 6 },
    { "hyphen", L"well-known words", 7 },
    { "hyphen at the edge", L"well-known words", 5 },
    { "ideographs", L"\x6587\x4ef6\x7f16\x8f91\x89c6\x56fe", 3 },
    { "ideographs, parenthesis", L"\x6587\x4ef6(F) \x7f16\x8f91(E)", 4 },
    { "ideographs, comma", L"\x6587\x4ef6\xff0c\x7f16\x8f91\x89c6\x56fe", 2 },
    { "ideographs, comma fitting", L"\x6587\x4ef6\xff0c\x7f16\x8f91\x89c6\x56fe", 3 },
    { "ideographs, corner bracket", L"\x6587\x4ef6\x300c\x7f16\x8f91\x300d", 3 },
    { "latin, ideographs", L"abc\x6587\x4ef6\x7f16", 4 },
    { "ideographs, latin", L"\x6587\x4ef6" L"abc def", 4 },
    { "katakana, prolonged sound mark", L"\x30d5\x30a1\x30a4\x30eb\x30fc\x7de8\x96c6", 4 },
};

static const WCHAR punctuation[] =
    L"!%),.:;>?]}'\"([{-/$#&*+<=@\\^_`|~"
    L"\x00a2\x00a3\x00a5\x00a8\x00ab\x00b0\x00b7\x00bb\x02c7\x02c9\x2014\x2015\x2016\x2018\x2019\x201c\x201d\x2022"
    L"\x2025\x2026\x2027\x2030\x2032\x2033\x2039\x203a\x2103\x2236\x3001\x3002\x3003\x3005\x3006\x3008\x3009\x300a"
    L"\x300b\x300c\x300d\x300e\x300f\x3010\x3011\x3014\x3015\x3016\x3017\x3018\x3019\x301a\x301b\x301c\x301e\x301f"
    L"\x3041\x3063\x3083\x309b\x309c\x309d\x309e\x30a0\x30a1\x30c3\x30e3\x30fb\x30fc\x30fd\x30fe\xfe36\xfe3a\xfe3e"
    L"\xfe40\xfe44\xfe50\xfe51\xfe52\xfe54\xfe55\xfe56\xfe57\xfe5a\xfe5c\xfe5e\xff01\xff02\xff04\xff05\xff07\xff08"
    L"\xff09\xff0c\xff0d\xff0e\xff1a\xff1b\xff1f\xff3b\xff3d\xff40\xff5b\xff5c\xff5d\xff5e\xff61\xff63\xff64\xff65"
    L"\xff70\xff9e\xff9f\xffe0\xffe1\xffe5";

static UINT first_line(HDC hdc, const WCHAR *str, int fit_chars, UINT flags, BOOL *wider)
{
    DRAWTEXTPARAMS dtp = { sizeof(dtp) };
    TEXTMETRICW tm;
    INT ext[32];
    SIZE size;
    RECT rect, calc;

    GetTextMetricsW(hdc, &tm);
    GetTextExtentExPointW(hdc, str, lstrlenW(str), 0, NULL, ext, &size);
    SetRect(&rect, 0, 0, ext[fit_chars - 1], tm.tmHeight);
    calc = rect;
    DrawTextW(hdc, str, -1, &calc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    if (wider) *wider = calc.right > rect.right;
    DrawTextExW(hdc, (WCHAR *)str, -1, &rect, flags | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL, &dtp);
    return dtp.uiLengthDrawn;
}

static HFONT select_font(HDC hdc, const WCHAR *face)
{
    LOGFONTW lf = {0};

    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfHeight = -12;
    lstrcpyW(lf.lfFaceName, face);
    return SelectObject(hdc, CreateFontIndirectW(&lf));
}

int main(void)
{
    HWND hwnd = CreateWindowExA(0, "static", NULL, WS_POPUP, 0, 0, 200, 200, 0, 0, 0, NULL);
    HDC hdc = GetDC(hwnd);
    unsigned int i;
    BOOL wider;

    DeleteObject(select_font(hdc, L"Arial"));
    for (i = 0; i < ARRAY_SIZE(cases); i++)
    {
        UINT length = first_line(hdc, cases[i].str, cases[i].fit_chars, 0, &wider);
        printf("%s, %d fitting: first line %u, %s\n", cases[i].name, cases[i].fit_chars, length,
               wider ? "laid out wider" : "laid out within");
    }

    DeleteObject(select_font(hdc, L"SimSun"));
    printf("may start, may end a line:\n");
    for (i = 0; punctuation[i]; i++)
    {
        WCHAR start[6] = { 0x6587, 0x4ef6, punctuation[i], 0x7f16, 0x8f91, 0 };
        WCHAR end[6] = { 0x6587, punctuation[i], 0x7f16, 0x8f91, 0x89c6, 0 };

        printf("  U+%04X %s, %s\n", punctuation[i], first_line(hdc, start, 2, 0, NULL) == 2 ? "starts" : "not first",
               first_line(hdc, end, 2, 0, NULL) == 2 ? "ends" : "not last");
    }
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);
    return 0;
}
