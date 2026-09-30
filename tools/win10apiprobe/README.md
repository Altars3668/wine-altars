# Office 按名字找、Wine 没有的 Windows 10 函数：`win10apiprobe.exe`

用 `WINEDEBUG=warn+module` 跑 Word，`GetProcAddress` 找不到的系统导出有：user32 的 `InheritWindowMonitor`（OART、
写作助手等用）、userenv 的 `DeriveAppContainerSidFromAppContainerName` 与 `GetAppContainerFolderPath`（加载项框架 OSF 与
它的 Web 沙箱 WEBSANDBOX 用）、ntdll 的 `NtQueryDirectoryFileEx`（C2R 的 App-V 层装钩子时探测）；还加载不了
`isolatedwindowsenvironmentutils.dll`（msoadfsb.exe 判断是否在隔离环境里）。Office 在找不到时都会退回旧路径，所以
现在不出错；要不要补、怎样补，先看 Windows 的回答。

探针打印：`isolatedwindowsenvironmentutils.dll` 的导出与本进程是否隔离；上面几个函数在不在；几个容器名派生出的
SID（旁边是本程序按“小写名字的 UTF-16LE 做 SHA-256，取前 7 个 DWORD”算的，看是否一致）；对没有包的 SID 调
`GetAppContainerFolderPath` 的结果（只打印 `\Packages\` 之后的部分）；`InheritWindowMonitor` 对有效、空、桌面、
外壳和无效窗口的返回值与错误码。

`results/win10apiprobe.wine.txt`：Wine（全都没有）；Windows 的待测（winref）。

构建：`x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror win10apiprobe.c -o win10apiprobe.exe -luser32 -ladvapi32 -lbcrypt -lole32`。
