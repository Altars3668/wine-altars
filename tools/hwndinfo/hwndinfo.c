/* hwndinfo -- 报告一个 HWND 的类名、标题、矩形、所属进程与祖先链。 */
#include <windows.h>
#include <stdio.h>
static void out(const char*s){DWORD w;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),s,(DWORD)strlen(s),&w,NULL);}
static void outf(const char*f,...){char b[600];va_list a;va_start(a,f);vsnprintf(b,sizeof(b),f,a);va_end(a);out(b);}
static void show( HWND h, const char *tag )
{
    WCHAR cls[128]={0}, txt[128]={0}; char cb[256],tb[256]; RECT r={0},cr={0}; DWORD pid=0,tid;
    if (!h) { outf("  %-10s (NULL)\n", tag); return; }
    GetClassNameW(h,cls,127); GetWindowTextW(h,txt,127);
    WideCharToMultiByte(CP_UTF8,0,cls,-1,cb,sizeof(cb),NULL,NULL);
    WideCharToMultiByte(CP_UTF8,0,txt,-1,tb,sizeof(tb),NULL,NULL);
    GetWindowRect(h,&r); GetClientRect(h,&cr);
    tid = GetWindowThreadProcessId(h,&pid);
    outf("  %-10s hwnd=%p class=%-26s text=%-18s 屏幕(%ld,%ld,%ld,%ld) 客户区%ldx%ld pid=%lu tid=%lu %s\n",
         tag, h, cb, tb, r.left,r.top,r.right,r.bottom, cr.right, cr.bottom, pid, tid,
         IsWindowVisible(h)?"可见":"隐藏");
}
int wmain( int argc, WCHAR **argv )
{
    HWND h = (HWND)(ULONG_PTR)wcstoul(argc>1?argv[1]:L"0",NULL,16);
    if (!IsWindow(h)) { outf("hwnd %p 不是有效窗口\n", h); return 1; }
    show(h,"目标");
    show(GetParent(h),"父窗口");
    show(GetAncestor(h,GA_ROOT),"GA_ROOT");
    show(GetFocus(),"焦点");
    show(GetForegroundWindow(),"前台");
    return 0;
}
