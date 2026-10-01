# 进程看到的 DPI 与 Office 窗口的 DPI 感知：`dpiprobe.exe`

4K 屏（GNOME 200%）上 Office 只有应有大小的一半：GNOME 50 的 Xwayland 给 X 客户端显示器的物理像素，缩放比例只经
根窗口资源库里的 `Xft.dpi` 告诉客户端，而 Wine 一直报 96。探针分三种线程上下文（进程启动时的、按显示器感知 v2、不感知）
打印 `GetDpiForSystem`、屏幕 DC 的 `LOGPIXELSX`、屏幕尺寸、每个显示器的有效 DPI 与原始 DPI；再对正在运行的 Word、Excel、
PowerPoint、Outlook 顶层窗口打印窗口的 DPI 感知上下文（是否 v2）、`GetDpiForWindow`、窗口矩形、进程的感知，以及子窗口按类名
归并的感知与 DPI——子窗口的 DPI 应当等于顶层窗口的。

`results/dpiprobe.wine-192.txt`：altars-up 在 `Xft.dpi` 192 的 3840x2160 Xvfb 上、Word 运行时的输出。新启动的进程系统 DPI
为 192（与 Windows 1803 起一样，取进程启动时主显示器的 DPI），不感知的视图看到虚拟化的 1920x1080；Word 的顶层窗口是
按显示器感知 v1（进程本身不感知，Office 在运行时设线程上下文），各类子窗口与顶层同为 192。Windows 上的对照待测（winref）。

构建：`x86_64-w64-mingw32-gcc -O2 -Wall -o dpiprobe.exe dpiprobe.c -luser32 -lgdi32 -lshcore`。
