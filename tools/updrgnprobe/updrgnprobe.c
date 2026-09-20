/* updrgnprobe -- 哪个窗口的更新区域一直清不掉。
 *
 * Word 打开字体下拉后主线程卡在 MsgWaitForMultipleObjectsEx 里满载空转：
 * 等待每次都立刻返回，说明队列里有个它取不走的唤醒源。WM_PAINT 是头号
 * 嫌疑——只要某个窗口的 update region 永远非空，QS_PAINT 就一直置位。
 *
 * 遍历目标进程的全部窗口，报告 update rect 非空的那些；隔一秒再测一次，
 * 看是不是同一批、同一块。
 *
 * Build:
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -municode \
 *       -o updrgnprobe.exe updrgnprobe.c -luser32
 */
#include <windows.h>
#include <stdio.h>

static void out( const char *s ) { DWORD w; WriteFile( GetStdHandle(STD_OUTPUT_HANDLE), s, (DWORD)strlen(s), &w, NULL ); }
static void outf( const char *f, ... ) { char b[512]; va_list a; va_start(a,f); vsnprintf(b,sizeof(b),f,a); va_end(a); out(b); }

static DWORD target_pid;
static int found;

static void walk( HWND parent, int depth )
{
    HWND child = NULL;

    while ((child = FindWindowExW( parent, child, NULL, NULL )))
    {
        DWORD pid = 0;
        RECT r;
        GetWindowThreadProcessId( child, &pid );
        if (pid == target_pid && GetUpdateRect( child, &r, FALSE ) &&
            (r.right > r.left || r.bottom > r.top))
        {
            WCHAR cls[64] = {0}, txt[64] = {0};
            char cb[128], tb[128];
            GetClassNameW( child, cls, 63 );
            GetWindowTextW( child, txt, 63 );
            WideCharToMultiByte( CP_UTF8, 0, cls, -1, cb, sizeof(cb), NULL, NULL );
            WideCharToMultiByte( CP_UTF8, 0, txt, -1, tb, sizeof(tb), NULL, NULL );
            outf( "  %*shwnd=%p class=%-28s text=%-18s update=(%ld,%ld)-(%ld,%ld) visible=%d\n",
                  depth * 2, "", child, cb, tb, r.left, r.top, r.right, r.bottom,
                  IsWindowVisible( child ) );
            found++;
        }
        if (depth < 4) walk( child, depth + 1 );
    }
}

int wmain( int argc, WCHAR **argv )
{
    HWND w;
    int pass;

    if (argc < 2) { out( "用法: updrgnprobe <窗口标题子串>\n" ); return 2; }

    w = FindWindowW( NULL, argv[1] );
    if (!w)
    {   /* 按类名再试 */
        w = FindWindowW( argv[1], NULL );
    }
    if (!w) { out( "没找到窗口\n" ); return 1; }
    GetWindowThreadProcessId( w, &target_pid );
    outf( "目标 hwnd=%p pid=%lu\n", w, target_pid );

    for (pass = 1; pass <= 3; pass++)
    {
        outf( "--- 第 %d 次 ---\n", pass );
        found = 0;
        walk( NULL, 0 );
        if (!found) out( "  没有窗口带非空 update region\n" );
        Sleep( 1000 );
    }
    return 0;
}
