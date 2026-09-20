/* previewglyphprobe -- 复现 Office 字体列表卡死的那一次字形请求。
 *
 * Word 卡住时 win32u 的诊断记下了没有返回的那次调用：
 *     glyph 0xce  format 0x91 (GGO_GLYPH_INDEX | WINE_GGO_HRGB_BITMAP)  buflen 7248
 * 用的字体是 Office 为字体下拉列表生成的预览字体 msofp_4_42
 * （AppData\Local\Microsoft\FontCache\4\PreviewFont\flat_officeFontsPreview_*.ttf，
 *  555 个字形，每个是一整条字体名的轮廓；0xce 有 22 条轮廓、574 个点）。
 *
 * 这里不经过 Office，直接私有加载那个字体，按同样的格式要同一个字形，
 * 在独立线程里跑并设超时，然后再做一次普通的 SelectObject——若 GDI 的
 * 字体锁被泄漏，第二步会永远回不来。
 *
 * Build:
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -municode \
 *       -o previewglyphprobe.exe previewglyphprobe.c -lgdi32 -luser32
 */
#include <windows.h>
#include <stdio.h>

#define WINE_GGO_HRGB_BITMAP 0x11

static void out( const char *s ) { DWORD w; WriteFile( GetStdHandle(STD_OUTPUT_HANDLE), s, (DWORD)strlen(s), &w, NULL ); }
static void outf( const char *f, ... ) { char b[512]; va_list a; va_start(a,f); vsnprintf(b,sizeof(b),f,a); va_end(a); out(b); }

static const MAT2 mat = { {0,1},{0,0},{0,0},{0,1} };

struct call_args { HDC hdc; UINT glyph; UINT format; DWORD buflen; void *buf; DWORD ret; };

static DWORD WINAPI do_call( void *p )
{
    struct call_args *a = p;
    GLYPHMETRICS gm;
    a->ret = GetGlyphOutlineW( a->hdc, a->glyph, a->format, &gm, a->buflen, a->buf, &mat );
    return 0;
}

static DWORD WINAPI do_select( void *p )
{
    HDC hdc = p;
    HFONT f = CreateFontW( 16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Tahoma" );
    HGDIOBJ prev = SelectObject( hdc, f );
    SelectObject( hdc, prev );
    DeleteObject( f );
    return 0;
}

int wmain( int argc, WCHAR **argv )
{
    const WCHAR *path = (argc > 1) ? argv[1] :
        L"C:\\users\\crossover\\AppData\\Local\\Microsoft\\FontCache\\4\\PreviewFont\\flat_officeFontsPreview_4_42.ttf";
    HDC hdc = CreateCompatibleDC( NULL );
    GLYPHMETRICS gm;
    int added, h, best_h = 0;
    DWORD best = 0, need;
    HFONT font;
    HGDIOBJ prev;
    struct call_args args;
    HANDLE th;
    void *buf;

    added = AddFontResourceExW( path, FR_PRIVATE, NULL );
    outf( "AddFontResourceEx     : 加入 %d 个字体\n", added );
    if (!added) { out( "字体没加载上，退出\n" ); return 2; }

    /* 找出让这个字形恰好需要约 7248 字节的字号 */
    for (h = 8; h <= 96; h++)
    {
        font = CreateFontW( h, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                            0, 0, CLEARTYPE_QUALITY, 0, L"msofp_4_42" );
        prev = SelectObject( hdc, font );
        need = GetGlyphOutlineW( hdc, 0xce, GGO_GLYPH_INDEX | WINE_GGO_HRGB_BITMAP, &gm, 0, NULL, &mat );
        SelectObject( hdc, prev );
        DeleteObject( font );
        if (need == GDI_ERROR) continue;
        if (!best || labs( (long)need - 7248 ) < labs( (long)best - 7248 )) { best = need; best_h = h; }
        if (need == 7248) break;
    }
    outf( "最接近 7248 的字号    : height=%d 需要 %lu 字节\n", best_h, best );
    if (!best_h) { out( "没量到尺寸，退出\n" ); return 2; }

    font = CreateFontW( best_h, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                        0, 0, CLEARTYPE_QUALITY, 0, L"msofp_4_42" );
    prev = SelectObject( hdc, font );
    buf = HeapAlloc( GetProcessHeap(), 0, best );

    args.hdc = hdc; args.glyph = 0xce;
    args.format = GGO_GLYPH_INDEX | WINE_GGO_HRGB_BITMAP;
    args.buflen = best; args.buf = buf; args.ret = 0xdeadbeef;

    out( "发起那次字形请求 ...\n" );
    th = CreateThread( NULL, 0, do_call, &args, 0, NULL );
    if (WaitForSingleObject( th, 20000 ) == WAIT_OBJECT_0)
        outf( "  返回 %lu%s\n", args.ret, args.ret == GDI_ERROR ? "（GDI_ERROR）" : "" );
    else
        out( "  20 秒没有返回 —— 就是它卡住了\n" );

    out( "再做一次普通的选字体（检查锁有没有被带走）...\n" );
    th = CreateThread( NULL, 0, do_select, hdc, 0, NULL );
    if (WaitForSingleObject( th, 15000 ) == WAIT_OBJECT_0)
        out( "\n结果：选字体正常 —— 锁没被带走\n" );
    else
        out( "\n结果：选字体也卡住 —— 字体锁被泄漏，复现成功\n" );

    (void)prev;
    RemoveFontResourceExW( path, FR_PRIVATE, NULL );
    return 0;
}
