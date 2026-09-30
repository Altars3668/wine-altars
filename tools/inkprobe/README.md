# 墨迹组件：`inkprobe.exe`

Office 模块里引用了成套的 Tablet PC 墨迹对象（InkDisp、InkRenderer、InkCollector、InkDrawingAttributes、InkTransform、
InkRectangle、InkRecognizers、RealTimeStylus），AirSpace 还用 Windows 10 的 InkDesktopHost 与 InkD2DRenderer。Wine 下
PowerPoint 放映的笔画不出东西（`CoCreateInstance(CLSID_InkRenderer)` 未注册），Word 的功能区没有“绘图”选项卡。

探针在 STA 线程上对每个类打印：`CoCreateInstance(IUnknown)` 的结果（建出来立即释放）、InprocServer32、线程模型、ProgID；
再打印应用能读到的笔与触摸相关系统度量（SM_TABLETPC、SM_DIGITIZER、SM_MAXIMUMTOUCHES、SM_CONVERTIBLESLATEMODE 等）
与 `GetPointerDevices` 列出的指针设备类型。

`results/inkprobe.wine.txt`：altars-up 下只有 rtscom.dll 的 RealTimeStylus，其余 11 个类都未注册，度量全 0、没有指针设备。
Windows 的结果（winref，SSH 会话与桌面会话各一次）决定先补哪一套、各类放在哪个 DLL。

构建：`x86_64-w64-mingw32-gcc -O1 -Wall -o tools/inkprobe/inkprobe.exe tools/inkprobe/inkprobe.c -lole32 -luuid -ladvapi32 -luser32`。
