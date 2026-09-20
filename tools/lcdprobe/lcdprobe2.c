/* lcdprobe2 -- 找出 ClearType 渲染失败的宽度阈值。
 * 对 Office 的预览字体 msofp_4_42 的字形 0xce 扫字号，记录黑框尺寸与成败。 */
#include <windows.h>
#include <stdio.h>
#define WINE_GGO_HRGB_BITMAP 0x11
static void out( const char *s ) { DWORD w; WriteFile( GetStdHandle(STD_OUTPUT_HANDLE), s, (DWORD)strlen(s), &w, NULL ); }
static void outf( const char *f, ... ) { char b[512]; va_list a; va_start(a,f); vsnprintf(b,sizeof(b),f,a); va_end(a); out(b); }
static const MAT2 mat = { {0,1},{0,0},{0,0},{0,1} };
int wmain( void )
{
    const WCHAR *path = L"C:\\users\\crossover\\AppData\\Local\\Microsoft\\FontCache\\4\\PreviewFont\\flat_officeFontsPreview_4_42.ttf";
    HDC hdc = CreateCompatibleDC( NULL );
    int h;
    if (!AddFontResourceExW( path, FR_PRIVATE, NULL )) { out( "字体没加载上\n" ); return 2; }
    out( "字号  黑框      三倍宽   结果\n" );
    for (h = 4; h <= 44; h += 2)
    {
        HFONT f = CreateFontW( h, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                               0, 0, CLEARTYPE_QUALITY, 0, L"msofp_4_42" );
        HGDIOBJ prev = SelectObject( hdc, f );
        GLYPHMETRICS gm;
        DWORD need = GetGlyphOutlineW( hdc, 0xce, GGO_GLYPH_INDEX | WINE_GGO_HRGB_BITMAP, &gm, 0, NULL, &mat );
        if (need != GDI_ERROR)
        {
            void *buf = HeapAlloc( GetProcessHeap(), 0, need ? need : 1 );
            DWORD got = GetGlyphOutlineW( hdc, 0xce, GGO_GLYPH_INDEX | WINE_GGO_HRGB_BITMAP, &gm, need, buf, &mat );
            outf( "%4d  %4ux%-4u  %5u    %s\n", h, gm.gmBlackBoxX, gm.gmBlackBoxY,
                  gm.gmBlackBoxX * 3, got == GDI_ERROR ? "失败" : "成功" );
            HeapFree( GetProcessHeap(), 0, buf );
        }
        else outf( "%4d  （取尺寸就失败）\n", h );
        SelectObject( hdc, prev ); DeleteObject( f );
    }
    RemoveFontResourceExW( path, FR_PRIVATE, NULL );
    return 0;
}
