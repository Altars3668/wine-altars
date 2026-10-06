# 最大化被裁成一块、跨进程的拥有者与放置、JSON 迭代顺序（2026-10-06，第二轮）

接 [office-upgrade-references-corners-20261006.md](office-upgrade-references-corners-20261006.md)。用户报告：

> 现在这个 word 又被一个小窗口限制住了，只能显示一块的内容。……点按钮缩小回来再点最大化放大，窗口会被缩小后的区域截断。
> 但是手动拖动放大缩小能正常缩放。
> 那个升级计划的内容和顶上关闭的控制栏是分离的。
> 没做的做了。

改动都在 wine-src-up 的 `altars-up` 分支，已推送；`dist-up` 已更新（含协议改动后的 wineserver），`/opt` 要重新安装才生效。
第 6–9 节是同一天接着做的部分。

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

## 6. msxml：坏文档、根元素之后、DTD 校验、E_PENDING、接口（第 167、168 批，tools/msxmlmoreprobe）

| 提交 | 内容 |
|---|---|
| `f943304d0da` | msxml3：读取器的缓冲区在移动或增长后于数据末尾终止（后面几项依赖它） |
| `29dd3a40a14` | 未定义实体、文本里的控制字符、第二个根元素按 Windows 报错；位置、srcText、filepos；空 `loadXML` 记缺根 |
| `bc4d55d3ccb` | 流拒绝访问时 `load()` 原样返回 `E_ACCESSDENIED` |
| `d3a2b898381` | 流连续两次 `E_PENDING` 时挂起：load 成功、readyState 3、持有流 |
| `37ad0e3e213` | `IXMLDOMDocument3` 只给 MSXML 6.0；3.0 文档的 IDispatch 用 IXMLDOMDocument2 |
| `72207003cd2` | 校验用的 libxml2 文档里补上元素的属性声明链（`xmlCopyDtd` 丢了它，缺必需属性一直查不出） |
| `e7a811081f3` | DTD 校验错误用 MSXML 的代码（Wine 测试里十个 todo_wine 通过） |
| `56757fe71dc` | 根元素之后不能有的内容各报各的错、报在 Windows 停下的地方 |
| `452c34eb44f` | DTD 校验错误按读的顺序取第一个，报在读取器遇到它的位置（与 SAX 定位器在相应事件上的位置相同） |

每个提交都带了 Windows 实测出的测试；msxml3 全部测试（domdoc 45358 项等）0 失败，改动前的构建在新测试上失败。
探针的 reasons、pending、epilog、validation 段与 Windows 逐行相同（不计 reason 原文与读块大小），只差 6.0 保留 EMPTY 元素
空格的那一处。细节见 [tools/msxmlmoreprobe/README.md](../tools/msxmlmoreprobe/README.md)。

## 7. 窗口：跨进程设拥有者不再卡住，WPF_RESTORETOMAXIMIZED（第 168 批，tools/ownerprobe）

| 提交 | 内容 |
|---|---|
| `a7ee2db407f` | 跨进程 `SetWindowLongPtr(GWLP_HWNDPARENT)` 直接改服务端、通知对方线程，不再同步等它；别的线程问服务端 |
| `9a6d1602ec0` | `WPF_RESTORETOMAXIMIZED`：最大化期间报，否则只在从最大化最小化或带标志最小化地 `SetWindowPlacement` 之后报 |

新探针段里发现：对方线程不处理消息时，跨进程设拥有者在 Wine 里一直等（Windows 立即返回）；`WPF_RESTORETOMAXIMIZED`
上一轮我让它一最大化就置位，结果“最大化、还原”之后还在。ownerprobe 现在与 Windows 只差最小化位置和服务会话的可见性。
user32 的 win、msg 测试与改动前对比：没有新增失败，新加的测试在改动前失败。

## 8. JSON：Split 两半（第 168 批）

`IMapView.Split` 两半按视图的哈希表顺序切开，Wine 已经如此；jsonprobe 与 Windows 逐行相同（含上次未初始化的那一行）。

## 9. 还没做的

- DWM 阴影与默认边框：第 167 批截图的边距不够、灰底分辨率只有 1/128；dwmframe 已改为 64 px 边距并加黑、白背景，
  第 168 批时有人在用那台电脑，探针按设计拒绝运行，待下一批。拿到数据后按截图做九宫格阴影（四角取实测，四边沿窗口拉伸），
  在 winex11 里给圆角弹出窗口配一个 ARGB 窗口垫在下面，只在合成管理器运行时启用。
- msxml 实体引用节点：结构已测清（见探针 README），Wine 仍把实体展开成文本；要让读取器把属性值里的原始引用交给 DOM 构建器。
- msxml 的 `IPersistMoniker`、`IProvideClassInfo`、`IOleCommandTarget`、`IServiceProvider`、`IMarshal`：第 168 批只测到
  一个类（探针自身的引用计数错误使它崩溃，已修），待下一批。
- reason 原文：Windows 是带参数的中文，Wine 是英文、不带参数。
- 升级计划对话框“分离”：仍未复现，需要用户描述当时的操作。
- 桌面上 Word/Excel/PowerPoint/Outlook 的图标原先仍指向退役的 `~/.wine-c2r-test`，已按用户要求改指 `~/.wine-c2r-up`。
