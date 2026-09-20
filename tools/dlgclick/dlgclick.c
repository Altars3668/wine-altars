/* dlgclick -- 给一个对话框按下指定按钮。
 * :0 上 mutter 不转发 XTEST，脚本点不了 Wine 的窗口；这里直接发 WM_COMMAND。 */
#include <windows.h>
#include <stdio.h>
static void out(const char*s){DWORD w;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),s,(DWORD)strlen(s),&w,NULL);}
static void outf(const char*f,...){char b[512];va_list a;va_start(a,f);vsnprintf(b,sizeof(b),f,a);va_end(a);out(b);}

static WCHAR want_title[128];
static int want_id;
static int done;

static BOOL CALLBACK enum_proc( HWND hwnd, LPARAM lp )
{
    WCHAR cls[64] = {0}, txt[128] = {0};
    char cb[128], tb[256];

    GetClassNameW( hwnd, cls, 63 );
    GetWindowTextW( hwnd, txt, 127 );
    if (wcscmp( cls, L"#32770" ) && wcscmp( cls, L"NUIDialog" )) return TRUE;
    if (want_title[0] && !wcsstr( txt, want_title )) return TRUE;

    WideCharToMultiByte(CP_UTF8,0,cls,-1,cb,sizeof(cb),NULL,NULL);
    WideCharToMultiByte(CP_UTF8,0,txt,-1,tb,sizeof(tb),NULL,NULL);
    outf( "  找到对话框 %p class=%s title=%s → 发送按钮 %d\n", hwnd, cb, tb, want_id );
    PostMessageW( hwnd, WM_COMMAND, MAKEWPARAM(want_id, BN_CLICKED), 0 );
    done++;
    (void)lp;
    return TRUE;
}

int wmain( int argc, WCHAR **argv )
{
    want_id = argc > 1 ? _wtoi(argv[1]) : IDNO;
    if (argc > 2) lstrcpynW( want_title, argv[2], (int)(sizeof(want_title)/sizeof(want_title[0])) );
    EnumWindows( enum_proc, 0 );
    if (!done) out( "  没找到匹配的对话框\n" );
    return done ? 0 : 1;
}
