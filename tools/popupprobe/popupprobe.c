/* popupprobe -- 直接验证 user32.CalculatePopupWindowPosition 的行为。
 *
 * Office 的 mso30win32client 延迟导入这个函数；Wine 以前没有它，
 * 延迟加载因此抛 0xC06D007F。补上实现之后，用本探针核对几何语义：
 * 对齐、贴屏、以及 exclude 矩形的避让方向。
 *
 * Build:
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -municode \
 *       -o popupprobe.exe popupprobe.c -luser32
 */
#include <windows.h>
#include <stdio.h>

#ifndef TPM_WORKAREA
#define TPM_WORKAREA 0x10000
#endif

static int failed;

static void out( const char *s ) { DWORD w; WriteFile( GetStdHandle(STD_OUTPUT_HANDLE), s, (DWORD)strlen(s), &w, NULL ); }
static void outf( const char *f, ... ) { char b[512]; va_list a; va_start(a,f); vsnprintf(b,sizeof(b),f,a); va_end(a); out(b); }

static void check( const char *what, const RECT *got, LONG l, LONG t, LONG r, LONG b )
{
    BOOL ok = got->left == l && got->top == t && got->right == r && got->bottom == b;
    if (!ok) failed++;
    outf( "  %-34s %s  得到 (%ld,%ld)-(%ld,%ld)", what, ok ? "通过" : "失败",
          got->left, got->top, got->right, got->bottom );
    if (!ok) outf( "  期望 (%ld,%ld)-(%ld,%ld)", l, t, r, b );
    out( "\n" );
}

int wmain( void )
{
    RECT pos, exclude, mon;
    POINT anchor;
    SIZE size = { 200, 100 };
    HMONITOR hmon;
    MONITORINFO mi = { sizeof(mi) };

    anchor.x = 500; anchor.y = 400;
    hmon = MonitorFromPoint( anchor, MONITOR_DEFAULTTOPRIMARY );
    GetMonitorInfoW( hmon, &mi );
    mon = mi.rcMonitor;
    outf( "显示器 (%ld,%ld)-(%ld,%ld)  工作区 (%ld,%ld)-(%ld,%ld)\n\n",
          mon.left, mon.top, mon.right, mon.bottom,
          mi.rcWork.left, mi.rcWork.top, mi.rcWork.right, mi.rcWork.bottom );

    /* 默认：左上对齐，锚点就是左上角 */
    if (!CalculatePopupWindowPosition( &anchor, &size, TPM_LEFTALIGN | TPM_TOPALIGN, NULL, &pos ))
    { out( "  调用失败\n" ); return 1; }
    check( "左上对齐", &pos, 500, 400, 700, 500 );

    CalculatePopupWindowPosition( &anchor, &size, TPM_RIGHTALIGN | TPM_BOTTOMALIGN, NULL, &pos );
    check( "右下对齐", &pos, 300, 300, 500, 400 );

    CalculatePopupWindowPosition( &anchor, &size, TPM_CENTERALIGN | TPM_VCENTERALIGN, NULL, &pos );
    check( "居中对齐", &pos, 400, 350, 600, 450 );

    /* 超出显示器右下角时贴回来 */
    anchor.x = mon.right - 10; anchor.y = mon.bottom - 10;
    CalculatePopupWindowPosition( &anchor, &size, TPM_LEFTALIGN | TPM_TOPALIGN, NULL, &pos );
    check( "右下越界后贴屏", &pos, mon.right - 200, mon.bottom - 100, mon.right, mon.bottom );

    /* 负坐标同样贴回来 */
    anchor.x = mon.left - 50; anchor.y = mon.top - 50;
    CalculatePopupWindowPosition( &anchor, &size, TPM_LEFTALIGN | TPM_TOPALIGN, NULL, &pos );
    check( "左上越界后贴屏", &pos, mon.left, mon.top, mon.left + 200, mon.top + 100 );

    /* exclude：默认保持水平对齐，改上下避让 -> 落到 exclude 下方 */
    anchor.x = 500; anchor.y = 400;
    SetRect( &exclude, 450, 380, 650, 430 );
    CalculatePopupWindowPosition( &anchor, &size, TPM_LEFTALIGN | TPM_TOPALIGN, &exclude, &pos );
    check( "exclude：默认向下让开", &pos, 500, 430, 700, 530 );

    /* TPM_VERTICAL：保持垂直对齐，改左右避让 -> 落到 exclude 右侧 */
    CalculatePopupWindowPosition( &anchor, &size, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_VERTICAL, &exclude, &pos );
    check( "exclude：TPM_VERTICAL 向右让开", &pos, 650, 400, 850, 500 );

    /* 不相交的 exclude 不应改变位置 */
    SetRect( &exclude, 0, 0, 50, 50 );
    CalculatePopupWindowPosition( &anchor, &size, TPM_LEFTALIGN | TPM_TOPALIGN, &exclude, &pos );
    check( "exclude 不相交则不动", &pos, 500, 400, 700, 500 );

    /* TPM_WORKAREA 用工作区而不是整个显示器 */
    anchor.x = mi.rcWork.right - 10; anchor.y = mi.rcWork.bottom - 10;
    CalculatePopupWindowPosition( &anchor, &size, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_WORKAREA, NULL, &pos );
    check( "TPM_WORKAREA 贴工作区", &pos, mi.rcWork.right - 200, mi.rcWork.bottom - 100,
           mi.rcWork.right, mi.rcWork.bottom );

    /* 参数校验 */
    SetLastError( 0 );
    if (CalculatePopupWindowPosition( NULL, &size, 0, NULL, &pos )) { out( "  NULL 锚点应失败      失败\n" ); failed++; }
    else outf( "  %-34s 通过  GetLastError=%lu\n", "NULL 锚点返回 FALSE", GetLastError() );

    outf( "\n%s（%d 项失败）\n", failed ? "有失败" : "全部通过", failed );
    return failed ? 1 : 0;
}
