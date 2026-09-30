# 调用线程是否持有加载器锁，以及退出时 TLS 回调与 DllMain 的顺序：`dllsyncprobe.exe`

Word、PowerPoint 等 16 个 Office 二进制链接了 AuxUlib（`aux_ulib.lib`）。它初始化时
`GetModuleHandleW(L"api-ms-win-core-libraryloader-l1-1-0.dll")`，在那里查 `PrivIsDllSynchronizationHeld`，
按 `BOOL (WINAPI *)(BOOL *held)` 调用（反汇编 `SDXHelper.exe` 得出）；查不到时自己比较 `PEB->LoaderLock`
（PEB+0x110）的 `OwningThread`（+0x10）与当前线程号。Wine 原先没有这个函数，每次启动都留下一次查找失败。

探针打印哪些模块导出它，以及在下列状态下它的返回值、写出的值、最后错误、AuxUlib 自己的检查结果和 TEB 的
`SameTebFlags`（0x1000 LoadOwner，0x2000 LoaderWorker）：

- 本程序的 TLS 回调：进程附加、线程附加、线程分离、进程分离；
- 旁边的 `dllsyncprobe_dll.dll` 的入口点（只回调本程序）：`LoadLibrary` 载入时、线程附加与分离、进程退出时；
- 载入与卸下 `version.dll` 时的 DLL 通知回调；
- 主线程不持锁、持锁一次、两次、放掉一次、全放掉（`LdrLockLoaderLock`）；
- 主线程持锁时另一个线程问（外加该线程 `LdrLockLoaderLock` 只试不等的结果，证明锁确实被占着）。

各状态按实际到达的先后列出；进程退出时的两行在最后、当场直接写出（带上各自收到的 `reserved`），它们的先后就是加载器调用 exe 的 TLS
回调与 DLL 入口点的顺序。

## 结果

`results/dllsyncprobe.wine.txt`：Wine（kernelbase 补上函数之后）。改之前各状态都是 “no function”，
AuxUlib 自己的检查与现在函数的回答逐项相同。另一处差异：Wine 退出时**不以 `DLL_PROCESS_DETACH` 调用 exe
的 TLS 回调**——只有 “DllMain, process detach at exit” 一行（上游 2020 年给 exe 补了进程附加、线程附加与
分离，唯独没有进程分离）；MSVC 的 `__dyn_tls_dtor` 靠这一次调用析构主线程的 `thread_local` 对象。
Windows 的结果待测（winref），据此定退出时两者的先后（以及 TLS 回调收到的 `reserved`）再改 ntdll。

## 构建

```
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror dllsyncprobe.c -o dllsyncprobe.exe
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror -shared dllsyncprobe_dll.c -o dllsyncprobe_dll.dll
```

两个文件放在同一目录运行。
