# RealTimeStylus 与 StrokeBuilder：`rtsprobe.exe`

PowerPoint 放映时的笔（OART.DLL 的墨迹工具）用一个 RealTimeStylus：挂自己的同步插件，再把 StrokeBuilder 当异步插件挂上，
按下时对每个平板上下文调 `GetTabletFromTabletContextId`。Wine 的 rtscom 里有一批没测过的取值：默认的期望包描述
（X、Y、NormalPressure）、鼠标的平板上下文号（1）、`GetPacketDescriptionData` 给出的度量与缩放（HIMETRIC，
dpi/2540）、没设窗口就启用的错误码、启用后再换窗口的错误码、关掉鼠标（`SetAllTabletsMode(FALSE)`）后的上下文、
`GetTabletFromTabletContextId`（Wine 是 E_NOTIMPL）、StrokeBuilder 的默认墨迹、DataInterest 和两种插件接口。

探针依次打印这些；拿到的平板对象再打印名称、能力位、最大输入矩形、X/Y/NormalPressure/PacketStatus 的支持与度量。
平板的即插即用 ID 只打印长度和第一个 `\` 之前的部分。

`results/rtsprobe.wine.txt`：altars-up 的结果。`results/rtsprobe.win.txt`：winref 的桌面会话。全部平板模式下 `GetTablet` 返回
S_OK 而平板为 NULL（探针原来照样 Release，在 Windows 上崩溃，已改）；没有窗口时启用是 0x80280005，启用中换窗口 0x80280006；
启用之前没有平板上下文；启用后输入矩形是窗口客户区。鼠标的上下文在 Windows 上以屏幕像素为逻辑范围（X 0..1920、Y 0..1080，
分辨率按显示器物理尺寸，约 55.8 每厘米），并有平板对象（名称 `\\.\DISPLAY1`、PlugAndPlayId 以 `SCREEN` 开头、能力 0x2、
X/Y/PacketStatus 支持、NormalPressure 不支持）；Wine 用 0..50800 的 himetric、没有平板对象，这两处还没改。

构建（用 Wine 树里生成的 rtscom.h，MinGW 没有这个头）：
`WINE_BUILD=$PWD/wine-src-up/build-wow64 scripts/build-probe.sh tools/rtsprobe/rtsprobe.c tools/rtsprobe/rtsprobe.exe ole32 oleaut32 user32 uuid`。

`rtsprobe.exe drag` 在探针自己的置顶窗口里用 SendInput 拖一下鼠标（只动鼠标，结束后光标放回原处），打印一个聚合了自由线程
封送器的同步插件收到的每个事件和包，以及同时挂着的 StrokeBuilder 建出的笔画。`results/rtsprobe-drag.win.txt`（winref 桌面会话）
与 `rtsprobe-drag.wine.txt`（altars-up `73a3972c421`）：包都是相对客户区的 himetric；Windows 在 RTS 自己的线程上回调，
按下时不发空中包（Wine 先收到移动消息，多一个），其余事件与笔画逐点相同。
