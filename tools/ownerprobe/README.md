# ownerprobe：拥有者与窗口放置在线程、进程之间怎么走

2026-10-06 起因：升级计划对话框关闭时 Wine 报 `set_window_owner cannot set owner (nil) on other process window`。
WebView2 的内容窗口（浏览器进程）被 Word 的对话框拥有，对话框销毁时 Wine 清不掉别的进程窗口的拥有者，服务端还留着死句柄。
顺带发现 Wine 对别的进程的窗口 `GetWindowPlacement` 只给假数据、`SetWindowPlacement` 直接失败。

```
ownerprobe            （服务会话即可；只建隐藏窗口，不显示，不要输入）
```

它会再启动自己作为“另一个进程”。各段：

| 段 | 问什么 |
|---|---|
| same thread / another thread | 拥有者销毁后，同线程、同进程别的线程里被拥有窗口的去留与 `GW_OWNER` |
| another process | 跨进程设置拥有者（`SetWindowLongPtr(GWLP_HWNDPARENT)`）；拥有者销毁后两个进程各自看到的 `GW_OWNER` |
| placement of the other process's window | 对方窗口正常、最大化、从最大化最小化、还原、再还原时，两个进程各自读到的 `WINDOWPLACEMENT`；再从外面 `SetWindowPlacement` 移动它 |
| ... taking no messages | 对方进程的另一个窗口所在线程不处理消息时，从这里给它设拥有者、销毁拥有者：两边各看到什么，会不会卡住 |
| WPF_RESTORETOMAXIMIZED in this process | 18 种操作序列（`ShowWindow`、`SetWindowPlacement` 的各种 showCmd 与标志）前后的标志，以及随后还原到哪里 |

## 结论（Windows 11 29671，results/windows-29671.txt）

- 拥有者销毁时：同线程的被拥有窗口一起销毁；别的线程、别的进程的被拥有窗口留下，`GW_OWNER`、`GWLP_HWNDPARENT` 立即为 NULL
  ——在两个进程里都是。
- 跨进程 `GetWindowPlacement` 与窗口所在进程自己读到的逐行相同；跨进程 `SetWindowPlacement` 成功并移动窗口。
- `WPF_RESTORETOMAXIMIZED`（第 168 批）：最大化期间报；此外只在“从最大化最小化”或“`SetWindowPlacement` 以最小化的
  showCmd 带着该标志”之后报，直到再次最小化或 `SetWindowPlacement`；其余的 `SetWindowPlacement` 清掉它，还原、最大化都不动它。
  所以“最大化、还原”之后不报，“从最大化最小化、还原、再还原”之后仍报（与 user32/tests/msg.c 的 ShowWindow 表一致）。
- 对方线程不处理消息时，跨进程 `SetWindowLongPtr(GWLP_HWNDPARENT)` 立即生效、立即返回；对方进程的另一个线程立即看到新的拥有者。

altars-up `d0e9284e955`、`1de0dc928da`、`7c86d99d82a`、`a7ee2db407f`、`9a6d1602ec0` 照此实现，`results/wine-9a6d1602ec0.txt`
与 Windows 只差环境决定的两处：最小化位置（Windows 跑在服务会话，没有任务栏，最小化窗口排在 (0,736)；交互桌面与 Wine 一样
是 -32000），以及 `IsWindowVisible`（服务会话的桌面不可见，Windows 上一律 0）。

还查出 Wine 原有的一个卡顿：隐藏或销毁拥有者时，Wine 同步地调整别的线程的被拥有弹出窗口的 Z 序，
对方线程不处理消息就一直等（复现：拥有者销毁卡 10 秒，直到对方的 `WaitForSingleObject` 超时）。现在按
Windows 的做法用 `SWP_ASYNCWINDOWPOS` 投递过去（`d0e9284e955`），销毁 2 ms 内返回。
