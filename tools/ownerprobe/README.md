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

## 结论（Windows 11 29671，results/windows-29671.txt）

- 拥有者销毁时：同线程的被拥有窗口一起销毁；别的线程、别的进程的被拥有窗口留下，`GW_OWNER`、`GWLP_HWNDPARENT` 立即为 NULL
  ——在两个进程里都是。
- 跨进程 `GetWindowPlacement` 与窗口所在进程自己读到的逐行相同；跨进程 `SetWindowPlacement` 成功并移动窗口。
- `WPF_RESTORETOMAXIMIZED`：窗口一最大化就报（Wine 以前只在从最大化最小化后才报），还原到正常后仍保留；
  `SetWindowPlacement`（showCmd `SW_SHOWNOACTIVATE`）之后清掉（Wine 仍保留，规则待查）。

altars-up `d0e9284e955`、`1de0dc928da`、`7c86d99d82a` 照此实现，`results/wine-7c86d99d82a.txt` 与 Windows 只差两处：
最小化位置（那次 Windows 跑在服务会话，没有任务栏，最小化窗口排在 (0,736)；交互桌面与 Wine 一样是 -32000）
和上面 `SetWindowPlacement` 后的标志。

还查出 Wine 原有的一个卡顿：隐藏或销毁拥有者时，Wine 同步地调整别的线程的被拥有弹出窗口的 Z 序，
对方线程不处理消息就一直等（复现：拥有者销毁卡 10 秒，直到对方的 `WaitForSingleObject` 超时）。现在按
Windows 的做法用 `SWP_ASYNCWINDOWPOS` 投递过去（`d0e9284e955`），销毁 2 ms 内返回。
