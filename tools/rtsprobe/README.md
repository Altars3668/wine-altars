# RealTimeStylus 与 StrokeBuilder：`rtsprobe.exe`

PowerPoint 放映时的笔（OART.DLL 的墨迹工具）用一个 RealTimeStylus：挂自己的同步插件，再把 StrokeBuilder 当异步插件挂上，
按下时对每个平板上下文调 `GetTabletFromTabletContextId`。Wine 的 rtscom 里有一批没测过的取值：默认的期望包描述
（X、Y、NormalPressure）、鼠标的平板上下文号（1）、`GetPacketDescriptionData` 给出的度量与缩放（HIMETRIC，
dpi/2540）、没设窗口就启用的错误码、启用后再换窗口的错误码、关掉鼠标（`SetAllTabletsMode(FALSE)`）后的上下文、
`GetTabletFromTabletContextId`（Wine 是 E_NOTIMPL）、StrokeBuilder 的默认墨迹、DataInterest 和两种插件接口。

探针依次打印这些；拿到的平板对象再打印名称、能力位、最大输入矩形、X/Y/NormalPressure/PacketStatus 的支持与度量。
平板的即插即用 ID 只打印长度和第一个 `\` 之前的部分。

`results/rtsprobe.wine.txt`：altars-up 的结果。Windows 的结果（winref，SSH 会话与桌面会话各一次）用来改 rtscom 里没测过的取值
和 dlls/rtscom/tests。

构建（用 Wine 树里生成的 rtscom.h，MinGW 没有这个头）：
`WINE_BUILD=$PWD/wine-src-up/build-wow64 scripts/build-probe.sh tools/rtsprobe/rtsprobe.c tools/rtsprobe/rtsprobe.exe ole32 oleaut32 user32 uuid`。
