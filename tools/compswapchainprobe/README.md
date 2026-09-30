# 组合交换链与合成表面：`compswapchainprobe.exe`

Excel 的网格动画（表格、数据透视表、排序、筛选、数据验证、批注各步）用 `IDXGIFactory2::CreateSwapChainForComposition`
建交换链，再用 `ICompositorInterop::CreateCompositionSurfaceForSwapChain` 做成 Windows.UI.Composition 表面显示。Wine
两者都是桩，Excel 每帧重试，一次普查几千次；实现之后 Excel 紧接着调用 `GetFrameStatistics`，桩的 `E_NOTIMPL`
让 AirSpace 直接 fail-fast（Mso20win32client.dll 里向地址 0 写 1 再 `int 0x29`），所以这些方法在 Windows 上答什么是规格。

探针依次打印：

- 创建：Excel 用的描述（337x230、B8G8R8A8、FLIP_SEQUENTIAL、STRETCH、PREMULTIPLIED、2 个缓冲）及逐项改动
  （缩放、交换效果、零尺寸、缓冲数 1/3/16/17、各 alpha 模式、格式、多重采样、stereo、用途、各标志、NULL 参数）的 HRESULT。
- 无窗口交换链上的方法：首次 Present 之前的 `GetFrameStatistics`/`GetLastPresentCount`（Excel 就是这时调用）、`GetHwnd`、
  `GetCoreWindow`、`GetDesc`（OutputWindow、Windowed）、`GetDesc1`（AlphaMode）、`GetFullscreenDesc`、
  `Get/SetFullscreenState`、`GetContainingOutput`、`ResizeTarget`、`GetRestrictToOutput`、背景色、旋转、`GetBuffer(0..2)`、
  `IDXGISwapChain2` 的矩阵与源尺寸和帧延迟、各同步间隔的 Present 平均耗时、`Present1` 带脏矩形、`GetFrameStatistics`、
  `ResizeBuffers`（含 0x0）。
- 合成器：`CreateCompositionSurfaceForSwapChain` 的结果、运行时类名、`GetIids` 个数、各 QI、对同一交换链再建一次、
  NULL/设备/窗口交换链时的返回值，以及表面存在时 Present 的耗时。
- 显示：200x200 置顶窗口上一个 100x100 精灵视觉用该交换链的表面画刷（Fill），先后呈现红、绿、半透明红（预乘），
  每次不提交，隔 500 ms 从屏幕 DC 读四个点：中心、精灵内两点、精灵外一点。

`results/compswapchainprobe.wine.txt`：altars-up。Windows 的结果（winref，要桌面会话：`windesktop-launch.ps1`）定规格。

构建：`WINE_BUILD=<树>/build-wow64 scripts/build-probe.sh tools/compswapchainprobe/compswapchainprobe.c
tools/compswapchainprobe/compswapchainprobe.exe combase d3d11 dxgi user32 gdi32 uuid dxguid`。
