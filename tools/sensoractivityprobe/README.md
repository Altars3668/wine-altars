# 传感器活动监视器：`sensoractivityprobe.exe`

PowerPoint 放映中（这里是放映开始约二十秒、用过指针之后）从 `mfsensorgroup.dll` 延迟加载
`MFCreateSensorActivityMonitor`。Wine 原来没有这个 DLL，延迟加载抛 0xc06d007e 没人接，PowerPoint 在放映中崩溃。
监视器由帧服务器告诉应用哪些进程在从哪个摄像头取流；Wine 没有帧服务器。

探针在 STA 主线程上（等待时泵消息，像 PowerPoint 的主线程）依次打印：DLL 与导出是否存在；`MFStartup` 之前能否创建；
两个 NULL 参数各回什么、输出是否被清零、回调被 AddRef 几次；监视器是否敏捷（IAgileObject、IMarshal）；
没 Start 时是否报告；`Start` 后第一份报告何时到、在哪个线程与套间；报告的内容——每个设备的友好名、符号链接只打印
第一个 `#` 之前与最后两段（不含设备实例）、各进程是否本进程、是否在取流、共享/控制模式、报告时间距今多久；
报告各方法越界与 NULL 时的返回值、按设备名取报告（原样、大写）；重复 `Start`、`Stop` 后是否还有报告、
重复 `Stop`、`Stop` 后再 `Start`；另建一个监视器不 Start 就 Stop 再释放；第三个的回调返回失败、Start 后立刻释放，
之后是否还报告、何时放掉回调。只观察，不打开任何摄像头。

`results/sensoractivityprobe.wine.txt`：altars-up `b5ae900caa9`（Start 后在线程池上给一份空报告）。Windows 的结果
（winref，SSH 会话与桌面会话各一次）定规格，再据此对齐实现与 `dlls/mfsensorgroup/tests`。

构建：`WINE_BUILD=<树>/build-wow64 scripts/build-probe.sh tools/sensoractivityprobe/sensoractivityprobe.c
tools/sensoractivityprobe/sensoractivityprobe.exe mfplat ole32 user32 uuid`（树里的 mfidl.h 要有这几个接口）。
