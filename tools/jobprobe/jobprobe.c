/* jobprobe -- EnumJobsW 能不能列出正在排队的作业。
 * Wine 里这个 API 一直是 stub，所以打印队列窗口永远是空的。 */
#include <windows.h>
#include <winspool.h>
#include <stdio.h>
static void out(const char*s){DWORD w;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),s,(DWORD)strlen(s),&w,NULL);}
static void outf(const char*f,...){char b[600];va_list a;va_start(a,f);vsnprintf(b,sizeof(b),f,a);va_end(a);out(b);}
static const char *u8(const WCHAR*w){static char b[4][300];static int n;char*p=b[n=(n+1)&3];
    if(!w) return "(null)"; WideCharToMultiByte(CP_UTF8,0,w,-1,p,300,NULL,NULL); return p;}

static void list_jobs( HANDLE pr, const char *tag )
{
    DWORD needed=0,count=0; BYTE *buf;
    EnumJobsW(pr,0,0xffffffff,2,NULL,0,&needed,&count);
    outf("  %-12s needed=%lu  ", tag, needed);
    if (!needed) { out("（没有作业）\n"); return; }
    buf = HeapAlloc(GetProcessHeap(),0,needed);
    if (!EnumJobsW(pr,0,0xffffffff,2,buf,needed,&needed,&count))
    { outf("EnumJobs 失败 %lu\n", GetLastError()); HeapFree(GetProcessHeap(),0,buf); return; }
    outf("共 %lu 个作业\n", count);
    for (DWORD i=0;i<count;i++)
    {
        JOB_INFO_2W *j = ((JOB_INFO_2W*)buf)+i;
        outf("      id=%lu 文档=%-24s 打印机=%-28s 状态=%#lx devmode=%s\n",
             j->JobId, u8(j->pDocument), u8(j->pPrinterName), j->Status,
             j->pDevMode ? "有" : "无");
    }
    HeapFree(GetProcessHeap(),0,buf);
}

int wmain(void)
{
    WCHAR name[MAX_PATH]; DWORD len=MAX_PATH; HANDLE pr; DOCINFOW di={sizeof(di)};
    HDC dc; int job;
    if (!GetDefaultPrinterW(name,&len)) { out("没有默认打印机\n"); return 2; }
    outf("打印机: %s\n", u8(name));
    if (!OpenPrinterW(name,&pr,NULL)) { out("OpenPrinter 失败\n"); return 2; }
    list_jobs(pr,"打印之前");

    dc = CreateDCW(NULL,name,NULL,NULL);
    if (!dc) { out("CreateDC 失败\n"); ClosePrinter(pr); return 2; }
    di.lpszDocName = L"jobprobe 测试文档";
    job = StartDocW(dc,&di);
    outf("StartDoc = %d\n", job);
    StartPage(dc);
    TextOutW(dc,100,100,L"jobprobe",8);
    EndPage(dc);
    list_jobs(pr,"作业进行中");
    EndDoc(dc);
    DeleteDC(dc);
    Sleep(300);
    list_jobs(pr,"打印之后");
    ClosePrinter(pr);
    return 0;
}
