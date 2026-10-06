# Office 弹出面板：闪一下就关、展开慢

2026-10-06。用户报告：标题栏“只读 · 兼容性模式 · 已保存”那一栏点开的面板黑一下就自己关了；打印页等处的下拉菜单展开很卡。两件事都在 Wine 里，各有一个根因。测试在 `:2`（无头 mutter + Xwayland，与用户的 GNOME 会话同一种窗口管理器），开发前缀 `~/.wine-c2r-up`，只读兼容模式测试文档放在会话临时目录。

## 一、面板闪一下就关：winex11 在激活后把弹出窗口重新映射

工具：`tools/popuptrace` 里的 `popuptrace`（同会话、按物理像素点击，每 2 ms 记录顶层窗口的新建/显示/隐藏/销毁、前台窗口、Word 主线程的激活/焦点/捕获窗口）。修复前 10 次点击里 5 次失败，失败的时间线（毫秒，相对点击）：

| 时刻 | 事件 |
|---:|---|
| 437 | 面板显示 |
| 442 | 面板成为前台，Word 线程 active = 面板 |
| 465 | 前台变成桌面窗口 `#32769`，active = 0 |
| 468 | Office 隐藏面板 |
| 501 | 前台回到 Word 主窗口 |

`WINEDEBUG=+event,+x11drv` 下读到的完整顺序：

1. Office 用 `SWP_NOACTIVATE` 显示面板，`is_window_managed()` 判为非托管，映射成 override-redirect，`_NET_WM_USER_TIME` 0。
2. Office 激活面板；`X11DRV_ActivateWindow` 对非托管窗口直接 `XSetInputFocus`，X 焦点到了面板（FocusIn）。
3. 3 ms 后一次只改形状的 `SetWindowPos`（标志 `0x181f`）进 `X11DRV_WindowPosChanged`，`is_window_managed()` 因“它是激活窗口”返回真，`window_set_managed()` 把它撤销映射、改成托管、再映射。
4. 撤销映射期间 mutter 把焦点和 `_NET_ACTIVE_WINDOW` 交给自己的窗口 `0x200003`；`hwnd_from_window()` 把不是 Wine 的 X 窗口当成桌面。焦点事件若在状态切换完成后才处理，Wine 就把前台设成桌面（`lost focus, setting fg to desktop`），面板失去激活，Office 关掉它。

开了调试日志时节奏变慢，这些事件落在“状态切换期间”被忽略，面板反而保住了——所以必须在无日志条件下统计失败率，不能拿一次日志复现下结论。旧的 CrossOver/11.0 树里同一段逻辑完全一样（上游 `3ff01d95577`，2025-02-19），不是这次升级引入的。

修复（altars-up `e9be6140953`，`dlls/winex11.drv/window.c`）：把托管判定拆成“激活”与“结构”（标题栏、边框、带系统菜单的弹出、全屏、`WS_EX_APPWINDOW`、拥有托管弹出窗口）两部分；已经映射（或即将映射）的非托管窗口若只因被激活才要托管，就保持原状，直到下次隐藏再显示。激活时它已经有 X 焦点。

验证：

- 修复后同条件 10 次点击 0 次提前关闭（修复前 5 次）。
- 面板打开后前台和焦点在面板内的控件上；按 Esc 关闭后 Word 主窗口拿回激活、焦点回到文档窗口；点击正文 3.6 ms 内前台回到 Word，面板隐藏、销毁。
- `:77` 上 user32 `win`、`msg`、`input` 修复前后交替各跑几次，没有新失败：`msg` 唯一一项是 `msg.c:5730` 的 todo 意外通过，前后相同；`win` 修复前一次 2 个偶发失败、重跑 0，修复后两次都是 0；`input` 修复前三次里两次出现大量 raw input 失败（1203、730 个，`rawinput_wndproc` 里的 `WM_INPUT` 洪泛，`GetRawInputData` 拿到已失效的句柄；退出码是封顶 255 的失败数，不是崩溃），第三次 0；修复后三次都是 0。旧版也能跑出 0，这项不算本修复的效果。d2d1 全量 25018 项，2 个失败（`d2d1.c:15491` Test 2 Figure does not match）与 10-02 的基线完全相同。见 `tools/popuptrace/results/winetest-summary.txt`。

## 二、展开慢：每个弹出面板都现场编译一遍 Direct2D 着色器

面板创建后约 400 ms 才显示。Word 主线程这段时间一直在 `NtUserWaitMessage` 里空闲；按线程统计 CPU（Windows 侧 `GetThreadTimes`），每次点击有一个工作线程耗约 420 ms，`wined3d_cs` 约 120 ms。对该线程采样（`wsample`：暂停线程、取上下文、复制 64 KB 栈、立即恢复，离线 `StackWalk64`）：

```
mso40uiwin32client（窗口过渡动画 AS_WindowTransition）
 → graphicscapture!session_StartCapture → frame_pool_start
 → dcomp!capture_start → D2D1CreateDevice
 → d2d1!d2d_device_init → d3dcompiler_47!D3DCompile
 → wined3d!vkd3d_shader_compile → hlsl_parse … find_vectorizable_expr_groups（深层递归）
```

Office 给每个淡入的弹出窗口做一次 Windows.Graphics.Capture，抓取每次新建一个 Direct2D 设备，而 Wine 的 `d2d_device_init` 每个设备都从源码编译 5 个形状顶点着色器和一个很大的像素着色器。Windows 的 Direct2D 用预编译字节码，没有这笔开销。

修复（altars-up `8b24432f92e`，`dlls/d2d1/device.c`、`factory.c`）：编译结果与设备无关，改为进程级缓存——首次在锁内编译，之后每个设备只增加 blob 引用；编译失败不缓存、下次重试；`FreeLibrary` 卸载时释放（进程退出时不处理）。

验证：标题栏面板从点击到显示 385–430 ms → 70–83 ms。打印页的五个下拉菜单用同一个 Word、同一打印页，只换 64 位 d2d1.dll 各重启一次，各点三次（`tools/popuptrace/dropdowns.sh` 的做法）：

| 下拉菜单 | 每个设备都编译 | 进程级缓存 |
|---|---:|---:|
| 打印机 | 1065–1449 ms | 61–77 ms |
| 页数范围 | 1711–1740 ms | 66–84 ms |
| 单面/双面 | 1398–1493 ms | 62–72 ms |
| 对照 | 698–785 ms | 63–75 ms |
| 每版页数 | 2911 ms | 78–95 ms |

下拉菜单比标题栏面板更慢，跟踪里同样先有 `AS_WindowTransition`，走的是同一条抓取路径。

## 采样器的几个坑（工具与记录在 `tools/popuptrace`）

- 线程停在系统调用里时，上下文 RIP 落在 win32u/ntdll 系统调用桩的 `ret` 上；dbghelp 从这里展不下去，按叶子函数手工取 `[RSP]` 为返回地址再交给 `StackWalk64`。
- Click-to-Run 的 `C:\Program Files\Common Files\...` 是 Office 进程自己的文件系统虚拟化，别的进程打不开，`SymLoadModuleExW` 返回 18；改从 `root\vfs\ProgramFilesCommonX64\...` 加载。Wine 把部分 Office DLL 复制进匿名内存，`/proc/<pid>/maps` 里没有文件名，模块表要用 psapi 枚举。
- 回溯每个样本要几毫秒，5 ms 间隔实际只采到几十个样本；先用线程 CPU 时间差找出忙碌线程，再专门采它。
- 用户的 Word 不受这些采样影响：全部在 `:2` 的测试实例上做，没有向 `:0` 的 Word 发送任何输入。

## 还没解决或另行处理的

- 测试 Word 两次关闭时等了 60–90 秒才退出（一次开着大量日志，一次刚做完采样），之后正常关闭 2 秒内退出，未能复现，不算确认的问题。
- 系统的 `/opt/wine-altars` 仍是 10-01 的 11.18（`wine-11.18-703`）；`dist-up` 已是含这两个修复的 `wine-11.19-536-g8b24432f92e`。换装需要 sudo，用户批准后自动审批仍拦下了，要由用户自己执行。
