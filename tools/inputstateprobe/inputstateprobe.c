/* inputstateprobe -- GetInputState 在有键盘/鼠标输入排队时到底报不报。
 *
 * Word 打开字体下拉后，主线程在 wwlib 的循环里反复调 GetInputState 空转，
 * 既不派发消息也不响应 Esc。若 Wine 的 GetInputState 在输入确实排队时仍
 * 返回 0，这个循环就永远出不来。本探针建一个窗口，然后持续打印
 * GetInputState / GetQueueStatus，外部用 xdotool 注入按键和点击对照。
 *
 * Build:
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -municode \
 *       -o inputstateprobe.exe inputstateprobe.c -luser32
 */
#include <windows.h>
#include <stdio.h>

static void out( const char *s ) { DWORD w; WriteFile( GetStdHandle(STD_OUTPUT_HANDLE), s, (DWORD)strlen(s), &w, NULL ); }
static void outf( const char *f, ... ) { char b[512]; va_list a; va_start(a,f); vsnprintf(b,sizeof(b),f,a); va_end(a); out(b); }

static LRESULT CALLBACK wndproc( HWND h, UINT m, WPARAM w, LPARAM l )
{
    return DefWindowProcW( h, m, w, l );
}

int wmain( int argc, WCHAR **argv )
{
    WNDCLASSW wc = { 0 };
    HWND hwnd;
    int secs = (argc > 1) ? _wtoi( argv[1] ) : 20;
    int i;

    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleW( NULL );
    wc.lpszClassName = L"inputstateprobe";
    wc.hCursor = LoadCursorW( NULL, (const WCHAR *)IDC_ARROW );
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW( &wc );

    hwnd = CreateWindowExW( 0, L"inputstateprobe", L"inputstateprobe", WS_OVERLAPPEDWINDOW,
                            100, 100, 500, 300, NULL, NULL, wc.hInstance, NULL );
    if (!hwnd) { outf( "CreateWindow 失败 %lu\n", GetLastError() ); return 1; }
    ShowWindow( hwnd, SW_SHOW );
    UpdateWindow( hwnd );
    SetForegroundWindow( hwnd );
    SetFocus( hwnd );

    outf( "窗口 %p，接下来 %d 秒只轮询、不派发消息（和 Word 的循环一样）\n", hwnd, secs );
    out( "秒  GetInputState  GetQueueStatus(QS_ALLINPUT)\n" );

    for (i = 0; i < secs * 2; i++)
    {
        BOOL st = GetInputState();
        DWORD qs = GetQueueStatus( QS_ALLINPUT );
        outf( "%4.1f      %d         0x%08lx\n", i / 2.0, st, qs );
        Sleep( 500 );
    }

    /* 收个尾：把排队的消息读出来看都是什么 */
    {
        MSG msg; int n = 0;
        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE ) && n < 40)
        { outf( "  队列消息 0x%04x wp=%Iu\n", msg.message, (ULONG_PTR)msg.wParam ); n++; }
        outf( "  共取出 %d 条\n", n );
    }
    return 0;
}
