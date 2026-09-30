# 合成视觉的截帧：`capturevisualprobe.exe`

Office 的 AirSpace（Mso40UIwin32client.dll）在表格、数据透视表等网格动画里对一个合成视觉截帧：
`GraphicsCaptureItem.CreateFromVisual` → 读 `Size`（宽须 >0）→ `Direct3D11CaptureFramePool.CreateFreeThreaded`
→ 会话 → 取帧的纹理。这一路几乎每一步遇到意外的 HRESULT 都会 fail-fast（崩溃标签 0x248864d、0x248864e、0x2488651…），
只有最开头的 `RoGetActivationFactory` 失败时它平稳退回。上游 Wine 没有 GraphicsCaptureItem，d3d11 也没有
`CreateDirect3D11DeviceFromDXGIDevice`/`CreateDirect3D11SurfaceFromDXGISurface`。

探针依次打印：会话静态接口的 `IsSupported`；`CreateFromVisual(NULL)` 与零尺寸视觉；d3d11 两个导出是否存在、
设备包装的类名与 `GetInterface(IDXGIDevice)` 是否原对象；然后对一个 64x32 红色精灵（内含 8,8 起 16x16 的绿色子精灵）
分别用自由线程帧池与本线程 DispatcherQueue 帧池各走一遍：项的类名、DisplayName、Size，帧池的类名与 DispatcherQueue，
会话的 IsCursorCaptureEnabled/IsBorderRequired，StartCapture 前后与重复 StartCapture，首帧到达的延迟与线程，
帧的类名、ContentSize、SystemRelativeTime、表面类名与描述、纹理描述（绑定、杂项标志、是否同一设备）与四个像素，
立即再取一帧，把颜色改成蓝色后是否来新帧与像素，没有变化时 300 ms 内是否还来帧，以及关闭会话/帧池后的行为。

`results/capturevisualprobe.wine.txt`：altars-up 实现之后。Windows 的结果（winref，要桌面会话：`windesktop-launch.ps1`）定规格。

构建：`WINE_BUILD=<树>/build-wow64 scripts/build-probe.sh tools/capturevisualprobe/capturevisualprobe.c
tools/capturevisualprobe/capturevisualprobe.exe combase d3d11 dxgi user32 uuid dxguid`。
