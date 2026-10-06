# 升级计划打不开、引用页崩溃、面板直角与滚动条（2026-10-06）

用户报告（Word，`~/.wine-c2r-up`）：

> 右上角升级计划点不开。而且现在所有的展开面板，包括字体、双面打印等都是直角和大宽滚动条，和 windows 上行为不符。
> word 引用页点了就崩，还会弹出一个控制台然后秒关。

四件事，四处根因，全部按 Windows 11（winref，build 29671）实测补齐。改动都在 wine-src-up 的 `altars-up` 分支，已推送；
测试显示 `:77` 上用 `dist-up` 验证过，`/opt` 还没有更新。

| 现象 | 根因 | 提交 |
|---|---|---|
| 引用页一点就崩 | msxml3 从流加载时用了失败的 `Read` 没设的计数 | `9b72583d2b6`，相邻缺口 `70197f2e665`、`361f375c387` |
| 升级计划点不开 | OSF 要 `JsonObject` 的 statics；WebView2 被强制走 DirectComposition 的“窗口转视觉” | `6cef8b4f04e`、`1b0ac62bb2f`，对齐 Windows `132f61412fa` |
| 面板直角 | DWM 的圆角与沿圆角的边框都没画 | `13af402977c`、`dc64c3f9fb8` |
| 大宽滚动条 | 实测与 Windows 一致，不是缺口（见第 4 节） | UISettings 的两个设置 `18800d64786` |

## 1. 引用页崩溃：msxml3

打开“引用”选项卡时 Word 从另一个文档加载 XML，路径是 `IXMLDOMDocument::load(VT_UNKNOWN 文档)` → 该文档自己的
`IStream` → `domdoc_load_from_stream` 的复制循环。用 `tools/excstack` 在第一次机会访问冲突处取调用栈追到这里。
源文档是空的，Wine 的文档流对空文档 `Read` 返回 `E_FAIL` 且不写计数，复制循环却照用这个未初始化的计数，从栈上
写出几 GB，Word 崩溃。

`tools/msxmlstreamprobe` 在 Windows 上把各种 `Read` 行为问了一遍（结果在该目录 `results/`），据此：

- `9b72583d2b6`：只复制成功的 `Read` 给出的字节，且不超过缓冲区；XMLView 的数据回调有同样的循环，一并改。
- `70197f2e665`（同一功能的相邻缺口）：空文档的流读出 0 字节并返回 S_OK；另一个文档作 `VT_DISPATCH` 也能当源；
  `load` 失败返回 **S_FALSE**、留下空文档，parseError 记录流或解析器自己的错误（空流是 `XML_E_MISSINGROOT`
  `0xC00CE558`，以前被换成 `E_FAIL`）；`IPersistStreamInit::Load` 返回同一个错误。两个上游 `todo_wine` 因此通过。
- `361f375c387`：`importNode` 导入文档、文档类型、实体、记号返回 `E_INVALIDARG`（以前猜的是 `E_FAIL`），
  `tools/msxmlimportprobe` 实测。

修好后在 `:77` 上点“引用”：选项卡正常显示（含“引文与书目”的样式下拉），Word 不再崩溃。

未做：流在 `Read` 返回 `E_PENDING` 时 Windows 会保留流、以后再读（异步加载）；Windows 保留 DTD 内部实体的实体引用节点，
Wine 展开成文本；MSXML 3.0 文档在 Windows 上不提供 `IXMLDOMDocument3`，Wine 提供。

## 2. 升级计划：Windows.Data.Json 与 DirectComposition 的“窗口转视觉”

点“升级计划”打开的是 OSF（OSF.DLL）的 WebViewDialog（场景 75），里面是 WebView2。Office 的 ULS 日志按“ODP Activation”
一行行列出失败的标签，标签是 5 个字符的 30 位值（每 6 位一个字符，字母表 `a-z0-9`，高位在前），在代码里以小端 imm32
出现（`b8..bf`、`41 bc` 等之后），从最深的调用往外排，据此一层层找到：

1. OSF 要 `Windows.Data.Json.JsonObject` 的 statics（`IJsonObjectStatics`，IID `{2289f159-54de-45d8-abcc-22603fa066a0}`），
   Wine 没有。`6cef8b4f04e` 实现了其余的 Windows.Data.Json（JsonArray/JsonObject 的集合接口、statics、JsonError、
   `IStringable`、严格的 RFC 8259 读取）。
2. OSF 用环境变量 `COREWEBVIEW2_FORCED_HOSTING_MODE` 强制 WebView2 走“窗口转视觉”（由 `Mso40UIwin32client` 序号
   #39068 按特性门决定）。这条路要 DirectComposition 的私有伙伴接口：IID `{d14b6158-c3fa-4bce-9c1f-b61d8665eab0}`，
   前 27 个槽同 `IDCompositionDesktopDevice`，27 是 `CreateSharedResource`，28 是 `OpenSharedResourceHandle`，
   29 是 `OpenSharedResource`。宿主（Word 里的 EmbeddedBrowserWebView.dll）建共享视觉，浏览器进程（msedge.dll 经
   `webview2_integration.dll`）打开它把内容合成进去。

`1b0ac62bb2f` 实现了 DirectComposition 设备、视觉树、目标、裁剪、变换与这套伙伴接口。Wine 没有跨进程合成，于是做了
等效的模拟：浏览器的 `Chrome_WidgetWin_1` 内容窗口（Chrome 本来把它设成 alpha 0 的分层窗口，并一直摆在视觉的屏幕位置）
在 Commit 时改成可见、输入穿透（`WS_EX_TRANSPARENT`，winex11 给空的 X 输入形状），拥有者设为宿主的根窗口；输入落到宿主
的 `Chrome_WidgetWin_0`，由它经 IPC 转给浏览器。两边进程共享一个节（`CreateFileMapping`）记 32 位窗口句柄，任一边
Commit 都能完成这一步。

结果：对话框完整显示“升级 Microsoft 365 订阅”，切“每年/每月”价格随之变化（输入通），`WM_CLOSE` 干净关闭。

之后 `tools/jsonprobe` 在 Windows 上实测 Windows.Data.Json 文档没写的部分，`132f61412fa` 照此对齐：成员按加入顺序、
TryParse 失败仍给出空值、改动使视图/迭代器失效、`%.15G`/`%.17G` 的数字写法与旧 CRT 的 NaN 写法、Parse 512 层/
Stringify 1024 层、`Split` 16 个以内不拆等（细节见 `tools/jsonprobe/README.md`）。Windows 上该一致性测试 939 项 0 失败。
改完后升级对话框照常显示。

## 3. 面板的圆角：DWM 的圆角与边框

Office 的下拉、菜单、浮出面板用 `DWMWA_WINDOW_CORNER_PREFERENCE` 请求圆角（字体下拉是 `DWMWCP_ROUNDSMALL`），用
`DWMWA_BORDER_COLOR` 请求边框色（`0x616161`），自己只画一个方角的 1 px 边框。Windows 11 的 DWM 裁出圆角（96 dpi 下
8 px / 4 px，最大化时不圆），并沿圆角画边框、在弯处做平滑。

- `13af402977c`：dwmapi 把圆角偏好交给 win32u，裁进窗口表面的形状（`GetWindowRgn` 仍返回应用自己设的区域）。
- `dc64c3f9fb8`：只裁不画，角看上去像缺了几个像素的直角。窗口表面现在带上“框架”（半径、边框宽、颜色），每次刷新时
  画在应用内容之上：角上像素 8×8 取样，不足一半在内的裁掉，边框在弯处宽 1.25 px，按覆盖率与**角内侧的颜色**混合
  （不是与像素本身：应用自己的方边框会被混进去，而 Windows 那里透出的是窗口外面）。结果与 Windows 的角部像素每通道
  相差约 0x10 以内，重复绘制不变；见 `tools/ddshot/results/corners-windows-wine.png`。
  `struct window_surface` 加了字段，`WINE_GDI_DRIVER_VERSION` 111→112，win32u 与各显示驱动要一起换。

没做：DWM 给这些弹出层画的阴影（左右约 10 px，底部约 14 px、更深）；只给了颜色时才画边框，默认颜色的边框不画。

## 4. 滚动条：与 Windows 一致

`tools/ddshot` 在 winref 上（用户的 Word 开着，探针拒绝碰它，改用同一 NetUI 控件的 Excel 字体列表）与 `:77` 上的 Word
各截一次：两边窗口事实相同（`Net UI Tool Window`、303 px 宽、分层、`ROUNDSMALL`），滚动条逐列相同——17 px 的 `f5`
轨道、9 px 直角深灰滑块、三角箭头、1 px 白缝、`0x61` 边框，指针悬停在轨道或滑块上也不变宽。也就是说，现在 Wine 下的
滚动条就是 Windows 上的样子。报告里的“大宽滚动条”这次没有测出差别；若某个面板仍显得宽，需要指明是哪一个再对比。

顺带：

- `18800d64786`：`UISettings.AutoHideScrollBars` 读 `HKCU\Control Panel\Accessibility\DynamicScrollbars`（缺省为开，
  Windows 11 上该值本来就不存在），`AdvancedEffectsEnabled` 读 `Themes\Personalize\EnableTransparency`。Office 自己的
  `WebView2Host.dll` 里有 `IUISettings5` 的 IID 和 `AutoHideScrollBars` 字样，引用了前者。
- 升级对话框里的 WebView2 页面用的是 Chromium 的 Fluent（Windows 11）滚动条。msedge.dll 自己打开
  `HKCU\Control Panel\Accessibility` 读 `DynamicScrollbars`，缺省也按 1。
- Office 侧的 `Microsoft.Office.UXPlatform.VisualVersion.Fluent2025` 门在 Wine 下也是开的（诊断日志
  `FeatureQueryBatched` 事件），`Office.UX.InitialCommonUIState` 报 `IsFluent: true`。

## 5. 工具与实测批次

- `tools/ddshot`：下拉面板的截图与窗口事实，Windows 与 Wine 同一套代码；会拒绝动用户正在用的 Office。
- `tools/jsonprobe`、`tools/msxmlimportprobe`、`tools/msxmlstreamprobe`：只问未知点、不传未验证的 NULL 输出指针，
  可在 winref 上安全运行，输出与 Wine 逐行 diff。
- `tools/excstack`：调试 API 附加，在第一次机会异常处用 dbghelp 回溯，不改目标。
- `scripts/win-deskrun.ps1`：在 winbatch 的同一个连接里，把程序放进用户的交互桌面会话运行（一次性计划任务）。

winref 批次 160–166 都经局域网的 22 端口跑（先读 banner、核对主机密钥）。第 160 批里 Wine 的 Windows.Data.Json
测试在 Windows 上崩了一次：`CreateNullValue(NULL)` 在 Windows 上不检查输出指针。此后测试里未经实测的 NULL 输出指针都删了。

## 6. 部署

`dist-up` 已是这些提交；win32u 的结构改动要连同 winex11 等驱动一起换（已换）。`/opt/wine-altars` 还没有，要从
`dist-up` 重新安装后，日常用的 Office 才会带上这些修复。
