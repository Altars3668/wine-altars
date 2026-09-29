# Windows.UI.Composition 绘制表面的绘制状态：`drawsurfprobe.exe`

Excel 的 AirSpace 在 Wine 下每次启动约 34 次记 `CompositionErrorActivity`（0x88980801，
DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED）：渲染线程对一个表面 BeginDraw、SuspendDraw、再对同一表面 BeginDraw（换
更新矩形，连续几次），UI 线程稍后 EndDraw；Wine 拒绝挂起之后的 BeginDraw（DirectComposition 文档也这么写）。探针按
AirSpace 的序列和一组对照序列调用（两次 BeginDraw、另一表面、挂起切换、跨线程 EndDraw/ResumeDraw、绘制中与挂起时的
Resize/Scroll、乱序调用），打印每个 HRESULT，每次绘制填不同颜色，最后用 `CopySurface` 读回像素看哪些更新生效。

`results/drawsurfprobe.wine.txt` 是 Wine 的输出；Windows 的待测（winref，要桌面会话：`scripts/winrun.sh --desktop`）。

构建：`WINE_BUILD=<树>/build-wow64 scripts/build-probe.sh tools/drawsurfprobe/drawsurfprobe.c tools/drawsurfprobe/drawsurfprobe.exe combase d3d11 d2d1 dxgi uuid`。
