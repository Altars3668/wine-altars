/* loadprobe -- 命令行上每个 DLL 能否加载，失败给出 Win32 错误。
 * 用于确认 Office 的 React Native 找不找得到 JS 引擎。 */
#include <windows.h>
#include <stdio.h>
static void out(const char*s){DWORD w;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),s,(DWORD)strlen(s),&w,NULL);}
static void outf(const char*f,...){char b[512];va_list a;va_start(a,f);vsnprintf(b,sizeof(b),f,a);va_end(a);out(b);}
int wmain(int argc, WCHAR **argv)
{
    int i;
    for (i = 1; i < argc; i++)
    {
        char n[256];
        HMODULE m;
        WideCharToMultiByte(CP_UTF8,0,argv[i],-1,n,sizeof(n),NULL,NULL);
        SetLastError(0);
        m = LoadLibraryW(argv[i]);
        if (m) outf("  %-24s 加载成功 base=%p\n", n, m);
        else   outf("  %-24s 失败 GetLastError=%lu\n", n, GetLastError());
    }
    return 0;
}
