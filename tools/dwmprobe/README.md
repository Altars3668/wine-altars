# DWM 窗口属性的原生对照

`dwmattr.c` 在一个顶层窗口上对 0–40 号属性各做一次 4 字节与 16 字节的 `DwmGetWindowAttribute`，再逐个写入几种值（0–4、`0xffffffff`、`0xfffffffe`、`0x00ff0000`）并读回，另测 2 与 8 字节写入；最后是子窗口、NULL 缓冲、NULL 与已销毁窗口。服务会话没有合成，必须在桌面会话里跑：

    scripts/build-probe.sh tools/dwmprobe/dwmattr.c tools/dwmprobe/dwmattr.exe dwmapi user32
    WIN_HOST=… WIN_USER=… WIN_PORT=… scripts/winrun.sh --desktop tools/dwmprobe/dwmattr.exe > tools/dwmprobe/dwmattr.win.txt

`dwmattr.win.txt` 是 winref（Windows 11 build 29671）上的输出。

## 测出的契约

- 只读：1 NCRENDERING_ENABLED（1）、14 CLOAKED（写过非零的 CLOAK 后为 `DWM_CLOAKED_APP`）、15 FREEZE_REPRESENTATION（0）、37 VISIBLE_FRAME_BORDER_THICKNESS（1，读时缓冲必须正好 4 字节）；5 CAPTION_BUTTON_BOUNDS 与 9 EXTENDED_FRAME_BOUNDS 读出矩形，缓冲不足 16 字节为 `E_NOT_SUFFICIENT_BUFFER`（写入时同样先报这个）。
- 只写（读为 `E_INVALIDARG`）：2–4、6、7、10–13、17、34–36、39，任何值都收；34 BORDER_COLOR、35 CAPTION_COLOR、36 TEXT_COLOR 与 17 必须正好 4 字节，36 不收 `DWMWA_COLOR_NONE`。
- 可读写：16、19、20（深色模式）读回归一成 0/1；33 圆角偏好收 0–4，读回原值；38 系统背景类型收任何值，读回原值。其余（0、8、18、21–32）一律 `E_INVALIDARG`。
- 写入缓冲小于 4 字节为 `E_INVALIDARG`，8 字节可以（上面几个“正好 4”的除外）；NULL 缓冲 `E_INVALIDARG`；NULL 或已销毁的窗口 `E_HANDLE`；子窗口大多 `E_HANDLE`，但能读 CLOAKED 与 37，写 8、15、18、37 先报 `E_INVALIDARG`。

Wine 原先：`DwmSetWindowAttribute` 什么都不存、一律 `S_OK`，`DwmGetWindowAttribute` 只认 EXTENDED_FRAME_BOUNDS。wine-src `5d52423` 之后除了与 Wine 自己的非客户区度量有关的两个矩形（标题按钮区域、没有隐形调整边框的扩展框架）和未公开的 40 号，逐行一致。

## 答对之后 Office 换了框架

圆角偏好答成 `S_OK`/0 之后，Office 走 Windows 11 的框架：标题栏画在客户区里，只留左右下各一个调整边框和顶部 1 像素。Wine 见窗口有非客户区就让窗口管理器加标题栏，还按标准标题栏高度缩可见区域，于是 GNOME 的标题栏压在 Office 自己的标题栏上。`tools/winrects` 打印窗口与客户区矩形用来查这个；wine-src `1af6bd6` 让“客户区伸到标题栏位置”的窗口不被装饰，Chrome/Electron 一类自绘标题栏的程序同样受益。
