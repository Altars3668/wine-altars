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

## 9. 接着做的（第 169 批：msxml 接口与空白、DWM 阴影）

| 提交 | 内容 |
|---|---|
| `7d7e2f14ce6` | msxml3：不在开头的 XML 声明在正文、序言、根元素之后一律按 Windows 报错 |
| `37bd26f7822` | msxml3：6.0 校验已加载的文档时仍找出 EMPTY 元素里被丢掉的空格 |
| `42daa0e2cf3` | include：ownerDocument 的参数名按 SDK 改为 XMLDOMDocument，类型库里的 coclass 名随之对了 |
| `460f1366ef0` | msxml3：文档的 IPersistMoniker、IProvideClassInfo、IOleCommandTarget、IServiceProvider；GetClassID 是创建时的类 |
| `767ba961357` | msxml3：只剩被丢空白的元素写成 `<r>\r\n</r>`（Wine 自己的三个 todo_wine 随之通过） |
| `49c42e13386` | win32u：没有边框色的圆角窗口保留圆角半径；默认边框时窗口形状向内收一圈，留给驱动画 |
| `f073327c4cf` | winex11：圆角弹出窗口下方的 ARGB 阴影窗口，画 DWM 的阴影与默认边框 |

DWM 阴影：第 169 批在白、灰、黑三种背景上截了 64 px 边距的图，阴影是纯黑带透明度，拟合成窗口矩形的模糊副本——ROUND 两层
（α 0.1445、σ 7.1、下移 2；α 0.1487、σ 19.6、下移 34.5），ROUNDSMALL 一层（α 0.0977、σ 4.1、下移 9.2）；默认边框是 0.40 不透明度的
0x767676，取代窗口最外一圈、叠在背景与阴影之上。winex11 按公式在运行时生成，跟着窗口移动、改大小、重排、显示与隐藏。
在 Xvfb + xcompmgr 上，探针截图与 Windows 在窗口外的均方根差 0.3–0.7/255、最大 2–8；默认边框那一圈在灰底上最大差 2。
用户的 Xwayland 上 mutter 持有合成选区，mutter 50 对带形状的无边框窗口不画它自己的阴影（读了它的源码），所以不会重复。
细节见 [tools/dwmframeprobe/README.md](../tools/dwmframeprobe/README.md)。

## 10. msxml 的实体引用（第 169、170 批）

| 提交 | 内容 |
|---|---|
| `43c4077e5f7` | msxml3：表达式编译出错后不再往下编——XSL 模式里带轴的查询（如 `ancestor::node()`）原来会读第 −1 步而崩溃，Windows 回 `E_FAIL` |
| `d8588136328` | msxml3：转成 libxml2 树时用 `xmlAddChild()` 的返回值——它把紧跟的文本并进前一个文本并释放新节点，原来随后写到已释放的节点上 |
| `bad663f91a6` | msxml3：实现 `IXMLDOMElement::normalize`（相邻文本合并，CDATA 不并，空文本保留） |
| `303abd68397` | msxml3：实体引用是节点，带着实体声明节点子节点的副本；DTD 结束时单独解析每个内部实体的文本；只读与克隆规则；声明的 xml 与标识；3.0 默认解析外部实体 |
| `def826471c0` | msxml3：XPath 透过实体引用（6.0 全部、3.0 一层）；XSL 模式的 `node()` 含 DOCTYPE |

模型：MSXML 在 DTD 读完时把每个内部实体的替换文本单独解析成实体声明节点的子节点（错误按替换文本里的位置报，没用到的实体
也查），正文和属性里的每个引用是只读的 entityref 节点，挂着这些子节点的副本——所以引用处的 xml:space 不起作用、只含空白的实体
引用后是空的。读取器为 DOM 做了两件新事：在 DTD 结束处单独读每个实体的文本（另开一个 locator，位置从 1:1 起），以及把属性值
里的引用原样留给 DOM（引用编码成 U+FFFF 名字 U+FFFE，这两个字符文档里不可能出现）。探针 `tools/msxmlmoreprobe` 的 entities、
entityrefs、valuespaces 三段现在除 reason 原文外与 Windows 完全一致（entities 原来差 268 行）。msxml3 全部测试 0 失败
（domdoc 46410 项，新增约 570 项，全部取自 Windows 实测）。

## 11. msxml 的 reason 原文

| 提交 | 内容 |
|---|---|
| `ace9661e905` | kernelbase：补上 E_ABORT、STG_E_ACCESSDENIED 的系统消息（Windows 有，Wine 的 FormatMessage 找不到） |
| `8730e9538e4` | msxml3：加载出错的 reason 按版本措辞、点出名字，内容模型出错时加“预期/要求”一行；中文逐字取自 Windows |

在中文环境下跑探针，reasons、epilog、validation、entityrefs、valuespaces 各段的 reason 与 Windows 逐字相同；只剩
E_FAIL 等四条系统消息是 Wine 全局的中文译法（见探针 README）。

## 12. 还没做的

- msxml 的 `IMarshal`（自由线程封送器）：有意不做，见上。
- 系统消息的中文措辞：E_FAIL、E_OUTOFMEMORY、E_INVALIDARG、E_ACCESSDENIED 等是 Wine 全局译法，与 Windows 不同，涉及所有程序，没有改。
- 圆角上几个抗锯齿像素：Windows 把窗口内容按覆盖比例与背后混合，Wine 的形状是二值的。`CS_DROPSHADOW` 的经典硬阴影也还没有。
- 升级计划对话框“分离”：仍未复现，需要用户描述当时的操作。
