# 进程的命令行：`cmdlineprobe.exe` / `cmdlineprobe32.exe`

Wine 原先没有 `NtQueryInformationProcess(ProcessCommandLineInformation)`（类 60，Windows 8.1 起），WMI 的
`Win32_Process.CommandLine` 因此对调用者以外的进程一律为空。altars-up `44814a09f7a` 起由 wineserver 从目标进程 PEB 指向的
参数块读命令行（进程自己改过的也读得到；进程还没开始跑时取创建者给的），只要 `PROCESS_QUERY_LIMITED_INFORMATION`；
结果的排布照 `ProcessImageFileName`：`UNICODE_STRING` 头紧跟字符串和一个结尾零，`MaximumLength = Length + 2`。

探针打印：对自己用 0 字节、比 `UNICODE_STRING` 少一字节、刚好一个头、比所需少一字节、刚好所需的缓冲区时的状态与长度，
成功时的 `Length`、`MaximumLength`、`Buffer` 指向哪里、字符串后面是什么；就地改掉自己命令行里的一个字符后再问一次（看读的
是不是活的 PEB）；以及一个挂起创建的子进程，经只有有限查询权限的句柄和只有 `SYNCHRONIZE` 的句柄各问一次。

`results/*.wine.txt`：altars-up。Windows 的待测（winref）：上述排布是否一致、拒绝访问时 ReturnLength 写什么、WoW64 进程改了自己
32 位 PEB 里的命令行之后查到的是哪一份。

构建：`x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror cmdlineprobe.c -o cmdlineprobe.exe`，32 位用
`i686-w64-mingw32-gcc` 输出 `cmdlineprobe32.exe`。
