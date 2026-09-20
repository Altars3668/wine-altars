/* glyphlockprobe -- 一次 GetGlyphOutline 写坏缓冲区，会不会把 GDI 的字体锁泄漏掉。
 *
 * Word 卡死时 win32u 报的是同一个线程在 font_SelectFont 等锁、而锁被自己在
 * font_GetGlyphOutline 里拿着——只可能是那一层没走到解锁。Wine 的 unix 侧写
 * 用户缓冲区时若触发页错误，异常会被抛回 PE 侧、unix 栈帧直接作废，解锁自然
 * 不执行。本探针不依赖 Office：先用一个必然出错的缓冲区调一次 GetGlyphOutlineW，
 * 再做一次普通的 SelectObject(字体)——若锁泄漏了，第二步会永远回不来。
 *
 * Build:
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -municode \
 *       -o glyphlockprobe.exe glyphlockprobe.c -lgdi32 -luser32
 */
#include <windows.h>
#include <stdio.h>
#include <setjmp.h>

static void out( const char *s ) { DWORD w; WriteFile( GetStdHandle(STD_OUTPUT_HANDLE), s, (DWORD)strlen(s), &w, NULL ); }
static void outf( const char *f, ... ) { char b[512]; va_list a; va_start(a,f); vsnprintf(b,sizeof(b),f,a); va_end(a); out(b); }

static HANDLE done_event;
static jmp_buf jmp;
static volatile LONG faulted;

/* mingw 没有 __try/__except；用向量化处理器 + longjmp 模拟「应用接住了这个异常」。 */
static LONG CALLBACK veh( EXCEPTION_POINTERS *info )
{
    if (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
    {
        faulted = (LONG)info->ExceptionRecord->ExceptionCode;
        longjmp( jmp, 1 );
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static DWORD WINAPI second_call( void *arg )
{
    HDC hdc = arg;
    HFONT font = CreateFontW( 20, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                              0, 0, 0, 0, L"Tahoma" );
    HGDIOBJ prev;

    out( "  线程 2：SelectObject(字体) ...\n" );
    prev = SelectObject( hdc, font );
    outf( "  线程 2：返回 %p\n", prev );
    SelectObject( hdc, prev );
    DeleteObject( font );
    SetEvent( done_event );
    return 0;
}

int wmain( void )
{
    HDC hdc = CreateCompatibleDC( NULL );
    HFONT font = CreateFontW( 40, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                              0, 0, 0, 0, L"Tahoma" );
    HGDIOBJ prev = SelectObject( hdc, font );
    static const MAT2 mat = { {0,1},{0,0},{0,0},{0,1} };
    GLYPHMETRICS gm;
    DWORD need, got;
    HANDLE th;

    done_event = CreateEventW( NULL, TRUE, FALSE, NULL );

    need = GetGlyphOutlineW( hdc, 'A', GGO_BITMAP, &gm, 0, NULL, &mat );
    outf( "位图大小查询            : %lu 字节\n", need );
    if (need == GDI_ERROR || !need) { out( "拿不到大小，无法测试\n" ); return 2; }

    /* 只给一页可写内存，其后是不可访问的保护页：光栅化写满就必然出错 */
    {
        SIZE_T sz = 0x2000;
        BYTE *mem = VirtualAlloc( NULL, sz, MEM_RESERVE, PAGE_NOACCESS );
        BYTE *buf;

        if (!mem) { out( "VirtualAlloc 失败\n" ); return 2; }
        VirtualAlloc( mem, 0x1000, MEM_COMMIT, PAGE_READWRITE );  /* 第二页留作保护页 */
        buf = mem + 0x1000 - 16;   /* 从页尾开始写，几个字节后就撞上保护页 */

        out( "故意越界的 GetGlyphOutlineW ...\n" );
        AddVectoredExceptionHandler( 1, veh );
        if (!setjmp( jmp ))
        {
            got = GetGlyphOutlineW( hdc, 'A', GGO_BITMAP, &gm, need, buf, &mat );
            outf( "  返回 %lu（没有触发异常）\n", got );
        }
        else outf( "  捕获到异常 0x%08lx —— 正是 Office 会遇到的情形\n", (unsigned long)faulted );
    }

    out( "接下来做一次普通的字体选择；若 GDI 的字体锁被泄漏，它会永远回不来。\n" );
    th = CreateThread( NULL, 0, second_call, hdc, 0, NULL );
    if (WaitForSingleObject( done_event, 15000 ) == WAIT_OBJECT_0)
        out( "\n结果：正常返回 —— 锁没有泄漏\n" );
    else
        out( "\n结果：15 秒没有返回 —— 字体锁确实被泄漏了（这就是 Word 卡死的机制）\n" );

    (void)th; (void)prev;
    return 0;
}
