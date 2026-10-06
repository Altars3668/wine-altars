# 弹出面板的时序与卡顿：同会话测量工具

2026-10-06 用来查 Word 标题栏状态面板“闪一下就关”和下拉菜单展开慢的两个工具与测量记录。
结论与修复见 [docs/office-popup-focus-latency-20261006.md](../../docs/office-popup-focus-latency-20261006.md)。

两个工具都在 Wine 会话里运行（与被测的 Word 同一个 wineserver），不用 ptrace、perf 或调试器附加，
只读目标进程，不发回车，不点“打印”。只在测试显示（默认 `:2`）上用，绝不对用户桌面 `:0` 上的 Word 操作。

## popuptrace

```
popuptrace x y duration_ms [settle_ms]
```

在 Word 主窗口所在会话里点击 (x, y)（物理像素；进程声明为 per-monitor DPI 感知，并用
`MOUSEEVENTF_VIRTUALDESK`，所以高 DPI 下点得准），之后每 2 ms 记录：

- 顶层窗口的新建、显示/隐藏、位置与样式变化、销毁（类名、样式、所属进程/线程）；
- 前台窗口；
- Word 主线程的激活、焦点、捕获窗口和菜单状态（`GetGUIThreadInfo`）。

时间相对按下鼠标的时刻。`FG ... class=#32769` 表示前台被设成了桌面窗口。

- `flyout-trials.sh <输出目录> <次数> <Wine 根目录> [x y]`：反复点击标题栏面板，判定是否提前关闭
  （跟踪期内被隐藏，或前台变成桌面），仍开着就按 Esc。
- `dropdowns.sh <输出目录> <Wine 根目录>`：打开打印页，五个下拉菜单各点三次，记录点击到显示的毫秒数；
  出现了才按 Esc（此时 Esc 发给前台的下拉菜单，不会退出打印页）。

## wsample

```
wsample <窗口类> <x> <y> <duration_ms> <interval_ms> [depth]
```

找到窗口所属线程，点击一次，之后每 interval_ms 暂停该线程、取上下文、复制栈顶 64 KB、立即恢复，
再用 dbghelp `StackWalk64` 离线回溯。环境变量：

- `WSAMPLE_TID=<十六进制>`：改采这个线程；
- `WSAMPLE_BUSY=1`：不采样，只报告点击前后各线程 CPU 时间（`GetThreadTimes`）的增量，用来先找出忙碌线程；
- `WSAMPLE_DEBUG=1`、`WSAMPLE_MODS=1`：输出加载失败的模块、模块表。

坑：

- 线程停在系统调用里时，RIP 落在 win32u/ntdll 系统调用桩的 `ret` 上，dbghelp 从这里展不下去；
  遇到 `0xc3` 就按叶子函数手工取 `[RSP]` 作返回地址。
- Click-to-Run 的 `C:\Program Files\Common Files\...` 只对 Office 自己的进程虚拟化，别的进程打不开
  （`SymLoadModuleExW` 返回 18），改从 `root\vfs\ProgramFilesCommonX64\...` 加载。Wine 把部分 Office DLL
  复制进匿名内存，`/proc/<pid>/maps` 里没有文件名，所以每个模块都显式按 psapi 的基址和大小加载。
- 每个样本回溯要几毫秒，间隔小于此时实际样本很少；先用 `WSAMPLE_BUSY` 找线程，再专门采它。

## 编译

```sh
D=tools/popuptrace
x86_64-w64-mingw32-gcc -O1 -Wall -Werror -o "$D/popuptrace.exe" "$D/popuptrace.c" -luser32
x86_64-w64-mingw32-gcc -O1 -Wall -Werror -o "$D/wsample.exe" "$D/wsample.c" -ldbghelp -lpsapi
```

## 记录（results/）

- `flyout-before.txt`、`flyout-winex11-fix.txt`、`flyout-both-fixes.txt`：标题栏面板，修复前 10 次里
  5 次提前关闭、显示 385–430 ms；只修 winex11 后 0/10；再加 d2d1 着色器缓存后 0/8、显示 70–83 ms。
  每个文件开头是逐次判定，后面是原始跟踪。
- `dropdowns-no-cache.txt`、`dropdowns-cache.txt`：同一 Word、同一打印页，只换 64 位 d2d1.dll、各重启一次：
  打印机 1065–1449 → 61–77 ms，页数 1711–1740 → 66–84，单双面 1398–1493 → 62–72，对照 698–785 → 63–75，
  每版页数 2911 → 78–95。`None` 是 3000 ms 内没出现或 Esc 时序所致，两种 d2d1 下都有。
- `sample-worker-thread.txt`：无缓存时，点击后耗 CPU 的工作线程的调用栈：
  `AS_WindowTransition` → graphicscapture → dcomp `capture_start` → `D2D1CreateDevice` → `d2d_device_init` →
  `D3DCompile` → vkd3d-shader 的 HLSL 编译。
- `sample-main-thread.txt`：同一时段 Word 主线程几乎一直在 win32u 的等待里，不是主线程忙。
- `winetest-summary.txt`：`:77` 上 user32 `input`/`win`/`msg` 与 d2d1 的前后对照（见文档里的说明）。
