# screenprobe：以每显示器 DPI 感知进程的眼光看屏幕和窗口

`monprobe` 以不感知 DPI 的程序身份提问，Wine（和 Windows 一样）给它缩放后的数字：192 dpi 的 3840x2160 显示器读出来是
1920x1080，看上去就像会话用错了屏幕。这个探针先声明每显示器 DPI 感知（PMv2）再问同样的问题，数字都是物理像素，并列出
某个类的可见顶层窗口：矩形、客户区、DPI、显示状态、样式、放置信息。不打印窗口标题。

```
screenprobe [class]          （默认 OpusApp，即 Word 主窗口）
screenprobe -all             所有可见顶层窗口：类、句柄、进程、矩形、样式、扩展样式、拥有者、DPI
```

注意：对别的进程的窗口，旧版 Wine 的 `GetWindowPlacement` 只给按窗口矩形编的假数据（altars-up `7c86d99d82a` 起与 Windows
一样读得到真实放置信息）。
