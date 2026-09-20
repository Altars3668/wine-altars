/* lcdprobe -- Wine 下 ClearType（次像素）字形渲染到底能不能用。
 *
 * Office 的字体预览字形在 WINE_GGO_HRGB_BITMAP 下拿到 FT_Err_Raster_Overflow，
 * 而那个字形只有 148x11 像素，不像是"太大"。这里对一批普通字体和字号走同一格式，
 * 看失败是普遍现象还是只出在特定字形上。
 *
 * Build:
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -municode \
 *       -o lcdprobe.exe lcdprobe.c -lgdi32
 */
#include <windows.h>
#include <stdio.h>

#define WINE_GGO_HRGB_BITMAP 0x11
#define WINE_GGO_VRGB_BITMAP 0x13

static void out( const char *s ) { DWORD w; WriteFile( GetStdHandle(STD_OUTPUT_HANDLE), s, (DWORD)strlen(s), &w, NULL ); }
static void outf( const char *f, ... ) { char b[512]; va_list a; va_start(a,f); vsnprintf(b,sizeof(b),f,a); va_end(a); out(b); }

static const MAT2 mat = { {0,1},{0,0},{0,0},{0,1} };

static void try_one( HDC hdc, const WCHAR *face, int height, WCHAR ch, UINT format, const char *fmtname )
{
    HFONT f = CreateFontW( height, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                           0, 0, CLEARTYPE_QUALITY, 0, face );
    HGDIOBJ prev = SelectObject( hdc, f );
    GLYPHMETRICS gm;
    DWORD need;
    char fb[64];

    need = GetGlyphOutlineW( hdc, ch, format, &gm, 0, NULL, &mat );
    WideCharToMultiByte( CP_UTF8, 0, face, -1, fb, sizeof(fb), NULL, NULL );
    if (need == GDI_ERROR)
        outf( "  %-18s h=%-3d U+%04X %-6s 失败（GDI_ERROR）\n", fb, height, ch, fmtname );
    else
    {
        void *buf = HeapAlloc( GetProcessHeap(), 0, need ? need : 1 );
        DWORD got = GetGlyphOutlineW( hdc, ch, format, &gm, need, buf, &mat );
        outf( "  %-18s h=%-3d U+%04X %-6s %s（%lu 字节，黑框 %ux%u）\n", fb, height, ch, fmtname,
              got == GDI_ERROR ? "渲染失败" : "成功", need, gm.gmBlackBoxX, gm.gmBlackBoxY );
        HeapFree( GetProcessHeap(), 0, buf );
    }
    SelectObject( hdc, prev );
    DeleteObject( f );
}

int wmain( void )
{
    HDC hdc = CreateCompatibleDC( NULL );
    static const WCHAR *faces[] = { L"Tahoma", L"Arial", L"DengXian", L"Times New Roman" };
    int h[] = { 12, 20, 40, 96 };
    size_t i, j;

    out( "普通字体的 ClearType 渲染：\n" );
    for (i = 0; i < ARRAYSIZE(faces); i++)
        for (j = 0; j < ARRAYSIZE(h); j++)
            try_one( hdc, faces[i], h[j], 'A', WINE_GGO_HRGB_BITMAP, "HRGB" );
    return 0;
}
