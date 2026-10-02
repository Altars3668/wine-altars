# Windows.UI.Composition 绘制表面的绘制状态：`drawsurfprobe.exe`

Excel 的 AirSpace 在 Wine 下每次启动约 34 次记 `CompositionErrorActivity`（0x88980801，
DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED）：渲染线程对一个表面 BeginDraw、SuspendDraw、再对同一表面 BeginDraw（换
更新矩形，连续几次），UI 线程稍后 EndDraw；Wine 拒绝挂起之后的 BeginDraw（DirectComposition 文档也这么写）。探针按
AirSpace 的序列和一组对照序列调用（两次 BeginDraw、另一表面、挂起切换、跨线程 EndDraw/ResumeDraw、绘制中与挂起时的
Resize/Scroll、乱序调用），打印每个 HRESULT，每次绘制填不同颜色，最后用 `CopySurface` 读回像素看哪些更新生效。

探针先问 BeginDraw 收什么（新表面第一次只画一部分、整张、IDXGISurface、`ID3D11Texture2D`，以及用 Direct3D 设备而不是
Direct2D 设备建的图形设备），再给每个新表面整张画一次，然后跑上面的序列。

`results/drawsurfprobe.win.txt`：winref 的桌面会话（Windows 11 build 29671）。新表面第一次绘制必须覆盖整张（只画一部分是
E_INVALIDARG）；同一表面再 BeginDraw，不论挂起没有，都先结束手上那次，所以 AirSpace 的三次绘制都留下了；同一时间只画一张
表面；绘制中 Resize 会结束这次绘制；次序不对的调用是 0x80131509（COR_E_INVALIDOPERATION），不是 DirectComposition 自己的
错误码。偏移是图集里的位置，每次都不同。三处 Wine 有意不跟：用 Direct3D 设备建的图形设备在 Windows 上 BeginDraw 一律
E_INVALIDARG；探针的 Scroll 参数在 Windows 上一律 E_INVALIDARG（原因未明）；绘制中 Resize 之后 Windows 整个设备再也画不了
（最后一段全是 0x80131509）。`results/drawsurfprobe.wine.txt`：altars-up `23afc3b16f3` 之后，除这三处外逐行相同。

构建：`WINE_BUILD=<树>/build-wow64 scripts/build-probe.sh tools/drawsurfprobe/drawsurfprobe.c tools/drawsurfprobe/drawsurfprobe.exe combase d3d11 d2d1 dxgi uuid`。
