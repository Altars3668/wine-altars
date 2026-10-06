# 最大化被裁成一块、跨进程的拥有者与放置、JSON 迭代顺序（2026-10-06，第二轮）

接 [office-upgrade-references-corners-20261006.md](office-upgrade-references-corners-20261006.md)。用户报告：

> 现在这个 word 又被一个小窗口限制住了，只能显示一块的内容。……点按钮缩小回来再点最大化放大，窗口会被缩小后的区域截断。
> 但是手动拖动放大缩小能正常缩放。
> 那个升级计划的内容和顶上关闭的控制栏是分离的。
> 没做的做了。

改动都在 wine-src-up 的 `altars-up` 分支，已推送；`dist-up` 已更新（含协议改动后的 wineserver），`/opt` 要重新安装才生效。

| 提交 | 内容 |
|---|---|
| `6235b9e06eb` | win32u：新的窗口表面第一次刷新时，把它的形状（或“没有形状”）告诉驱动 |
| `d0e9284e955` | win32u：别的线程的被拥有弹出窗口异步重排，拥有者不再被对方卡住 |
| `1de0dc928da` | win32u：拥有者销毁时清掉别的进程窗口的拥有者 |
| `7c86d99d82a` | win32u、server：跨进程读写窗口放置信息；最大化即报 `WPF_RESTORETOMAXIMIZED` |
| `5862cbec5b4` | windows.web：`JsonObject` 按 Windows 的哈希表顺序迭代 |

## 1. 最大化后只显示还原时那一块

我上一轮的圆角提交 `13af402977c` 引入的回归。Word 主窗口请求圆角，还原时窗口表面带圆角形状（2880x1620）；最大化不圆角，
新表面没有形状位图。`window_surface_flush` 只在形状变化时把 `shape_changed` 交给驱动，没有形状的表面永远不“变化”，
winex11 就不会 `XShapeCombineMask(None)`，X 窗口一直按还原时的形状裁剪。拖动缩放时窗口始终是还原状态、形状每次重算，
所以正常。`xwininfo -shape` 一眼可见：3840x2064 的窗口带着 2880x1436 的 “Window shape extents”。

修法：表面记一个 `shape_flushed`，驱动成功刷新前每次都把形状当作已变化（`WINE_GDI_DRIVER_VERSION` 113，各驱动一起换）。
在无头 mutter 上用还原/最大化命令验证：还原时形状 2880x1620，最大化后无形状、内容铺满。

## 2. 跨进程的拥有者与放置（tools/ownerprobe）

升级对话框关闭时日志有 `set_window_owner cannot set owner (nil) on other process window`：WebView2 的内容窗口在浏览器进程，
被 Word 的对话框拥有；Wine 清不掉别的进程窗口的拥有者，服务端也留着死句柄。探针在 Windows 上测得：拥有者销毁时，
别的线程、别的进程的被拥有窗口留下、拥有者立即为 NULL（两边进程都这样看）；跨进程 `GetWindowPlacement` 与窗口所在进程
自己读到的逐行相同，`SetWindowPlacement` 能成功。

- `1de0dc928da`：服务端的拥有者立即清掉，对方进程自己的副本在它的线程收到消息时清掉；在那之前，对方进程查到缓存的拥有者
  已不是窗口时改问服务端，所以 `GW_OWNER`、`GetParent` 立即为 NULL。
- `7c86d99d82a`：窗口所在进程每次改动放置信息（正常矩形、最小/最大化位置、`WPF_RESTORETOMAXIMIZED`）都交给服务端放进窗口的
  共享内存，别的进程直接读（按窗口当前位置做与本进程相同的惰性更新），不发消息，对方线程不处理消息也能读——Wine 自己的
  `test_other_process_window` 正是这种情形，它的三个 `todo_wine` 现在通过。设置则作为 `WM_WINE_SETWINDOWPLACEMENT` 交给窗口的
  线程执行，带上调用线程的 DPI 感知。顺带：Windows 窗口一最大化就报 `WPF_RESTORETOMAXIMIZED`。
- `d0e9284e955`：测试时发现原有的卡顿——隐藏或销毁拥有者时，`swp_owner_popups` 同步地给别的线程的被拥有弹出窗口发
  `SetWindowPos`，对方不处理消息就一直等（复现中拥有者销毁卡 10 秒）。改为 `SWP_ASYNCWINDOWPOS` 投递，与 Windows 对另一个输入
  队列的线程的做法一致，销毁 2 ms 内返回。

user32 的 `win`、`msg` 测试与改动前逐项对比：失败项相同（mutter/Xvfb 下原有的几项），新加的跨进程断言全部通过。

## 3. JSON 迭代顺序（tools/jsonprobe）

Windows 写出（Stringify）用加入顺序，迭代对象和视图却用它内部哈希表的顺序。27 种规模（16–500 个成员）、删除再加、清空、
倒序解析的结果与一个模型逐条吻合：ATL 的 `CAtlMap`，键是名字 UTF-16 字节的 FNV-1a，开始 17 个桶，新成员放桶首，超过
2.25×桶数时按 ATL 的素数表重哈希。`5862cbec5b4` 照此实现，探针输出与 Windows 逐行相同（只剩一个探针自身未初始化变量的行，
已修探针）。windows.web 的测试 939 + 354 项全过。

## 4. 闪烁：测试窗口走了 Wayland 驱动

中途我在用户桌面上起了一个测试 Word，用户看到“正文文字在疯狂闪烁”“窗口也在闪”。原因：我照抄了 gnome-shell 进程的环境变量，
它本身没有 `DISPLAY`，Wine 连不上 X，按默认顺序退到 winewayland，Word 在 Wayland 驱动下闪烁。已停掉。用户自己的 Word 走 X11，
不受影响。教训见作者的私有笔记。

## 5. 升级计划对话框“分离”：尚未复现

无头 mutter（缩放 1 与 2）、仿照用户 GNOME Shell 搭的隔离环境（Ubuntu 模式、用户启用的扩展、底部任务栏工作区、隐藏顶栏）里，
对话框打开时标题栏与内容相连（差 6 px），用窗口管理器挪动、快速连续拖动，内容都立即跟上。需要用户描述当时的操作。
搭隔离 Shell 时它把扩展更新装进了用户的真实扩展目录（blur-my-shell 72→74，用户决定保留）。

## 6. 已测、待做（第 167 批探针）

- msxml（tools/msxmlmoreprobe）：3.0 不该提供 `IXMLDOMDocument3`；缺 `IPersistMoniker`、`IProvideClassInfo`、`IOleCommandTarget`、
  `IServiceProvider`、`IMarshal`；未定义实体、控制字符、两个根元素 Wine 都接受了；parseError 的位置与 srcText；
  `E_PENDING` 挂起语义；`E_ACCESSDENIED` 直接返回；不做 DTD 校验；实体引用节点（待重测）。
- DWM（tools/dwmframeprobe）：圆角弹出窗口的阴影（ROUND 与 ROUNDSMALL 大小不同）与默认边框。
- 桌面上 Word/Excel/PowerPoint/Outlook 的图标原先仍指向退役的 `~/.wine-c2r-test`，已按用户要求改指 `~/.wine-c2r-up`。
