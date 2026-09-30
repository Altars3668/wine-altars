# NtQueryDirectoryFileEx 的各个查询标志：`dirqueryprobe.exe`

Windows 10 加了 `NtQueryDirectoryFileEx`：`NtQueryDirectoryFile` 的两个布尔参数（ReturnSingleEntry、RestartScan）换成一个标志字。
Click-to-Run Office 的 App-V 层按名查找它以便挂钩；Wine 没有（Wine 的 kernelbase 仍走 `NtQueryDirectoryFile`，App-V 对它的
挂钩照常起作用，所以缺它不影响 Office 的虚拟化文件系统）。

探针在临时目录里建 `a.txt`、`ab.txt`、`b.txt`、`c.dat` 和子目录 `d`，在同一个句柄上连续调用，打印每次的标志、掩码、状态和
返回的名字，从中读出各标志对目录游标和掩码的作用：`SL_RESTART_SCAN` 0x1、`SL_RETURN_SINGLE_ENTRY` 0x2、
`SL_INDEX_SPECIFIED` 0x4、`SL_RETURN_ON_DISK_ENTRIES_ONLY` 0x8、`SL_NO_CURSOR_UPDATE` 0x10，以及不是标志的位；另有旧函数的
同样调用作对照、缓冲区过小、各信息类，以及对普通文件调用。

`results/dirqueryprobe.wine.txt`：Wine（只有旧函数那一段）；Windows 的待测（winref），据此实现。

构建：`x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror dirqueryprobe.c -o dirqueryprobe.exe -lntdll`。
