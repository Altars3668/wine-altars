/* 把预览字形的 ClearType 位图打出来：h=20 走分条路径，h=28 走 FreeType 原生路径。 */
#include <windows.h>
#include <stdio.h>
#define WINE_GGO_HRGB_BITMAP 0x11
static void out( const char *s ) { DWORD w; WriteFile( GetStdHandle(STD_OUTPUT_HANDLE), s, (DWORD)strlen(s), &w, NULL ); }
static void outf( const char *f, ... ) { char b[1024]; va_list a; va_start(a,f); vsnprintf(b,sizeof(b),f,a); va_end(a); out(b); }
static const MAT2 mat = { {0,1},{0,0},{0,0},{0,1} };
static void dump( HDC hdc, int h, int cols )
{
    static const char *ramp = " .:-=+*#%@";
    HFONT f = CreateFontW( h, 0,0,0, FW_NORMAL,0,0,0, DEFAULT_CHARSET,0,0, CLEARTYPE_QUALITY,0, L"msofp_4_42" );
    HGDIOBJ prev = SelectObject( hdc, f );
    GLYPHMETRICS gm;
    DWORD need = GetGlyphOutlineW( hdc, 0xce, GGO_GLYPH_INDEX | WINE_GGO_HRGB_BITMAP, &gm, 0, NULL, &mat );
    if (need != GDI_ERROR && need)
    {
        unsigned *b = HeapAlloc( GetProcessHeap(), 0, need );
        if (GetGlyphOutlineW( hdc, 0xce, GGO_GLYPH_INDEX | WINE_GGO_HRGB_BITMAP, &gm, need, b, &mat ) != GDI_ERROR)
        {
            unsigned pitch = gm.gmBlackBoxX;
            outf( "height=%d  黑框 %ux%u\n", h, gm.gmBlackBoxX, gm.gmBlackBoxY );
            for (unsigned y = 0; y < gm.gmBlackBoxY; y++)
            {
                char line[400]; unsigned n = 0;
                for (unsigned x = 0; x < gm.gmBlackBoxX && (int)x < cols; x++)
                {
                    unsigned p = b[y*pitch + x];
                    unsigned v = max( max( (p>>16)&0xff, (p>>8)&0xff ), p&0xff );
                    line[n++] = ramp[ v * 9 / 255 ];
                }
                line[n] = 0; outf( "  %s\n", line );
            }
        }
        else outf( "height=%d 渲染失败\n", h );
        HeapFree( GetProcessHeap(), 0, b );
    }
    else outf( "height=%d 取尺寸失败\n", h );
    SelectObject( hdc, prev ); DeleteObject( f );
    out( "\n" );
}
int wmain( void )
{
    const WCHAR *path = L"C:\\users\\crossover\\AppData\\Local\\Microsoft\\FontCache\\4\\PreviewFont\\flat_officeFontsPreview_4_42.ttf";
    HDC hdc = CreateCompatibleDC( NULL );
    if (!AddFontResourceExW( path, FR_PRIVATE, NULL )) { out("字体没加载上\n"); return 2; }
    out( "=== h=20（FreeType 拒绝，走新的分条实现）===\n" );  dump( hdc, 20, 110 );
    out( "=== h=28（FreeType 自己渲染，作对照）===\n" );       dump( hdc, 28, 110 );
    RemoveFontResourceExW( path, FR_PRIVATE, NULL );
    return 0;
}
