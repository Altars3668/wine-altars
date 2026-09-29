# 非 OLE 剪贴板经 OleGetClipboard 提供什么：`oleclipprobe.exe`

程序不经 OLE（`SetClipboardData`）放上剪贴板的数据，OLE 程序（Office 粘贴走这条路）通过 `OleGetClipboard` 拿到的
IDataObject 为每种格式枚举什么 TYMED，`GetData` 对每种介质回答什么。另一 Wine 会话或 X11 原生程序复制的内容到了
Wine 里也是这种剪贴板（没有 "Ole Private Data"）。

探针放上所有标准格式（CF_TEXT、CF_DIB、CF_SYLK、CF_DIF、CF_TIFF、CF_PENDATA、CF_RIFF、CF_WAVE、CF_HDROP、
CF_ENHMETAFILE、CF_PALETTE、CF_DSP*）和几种注册格式（Embed Source、Object Descriptor、Link Source、
Rich Text Format、一个私有格式），打印 user32 合成后的格式列表，再逐格式打印枚举的 TYMED 与六种介质的 `GetData`
结果。它会替换所在窗口站的剪贴板：在 Windows 上经 SSH 会话运行（有自己的窗口站），不要用 `--desktop`。

`results/oleclipprobe.wine.txt`（altars-up `1f038e2c738` 之后）：Wine 把 CF_DIB、CF_DIBV5、CF_PALETTE、CF_TIFF、
CF_RIFF、CF_WAVE、CF_SYLK、CF_DIF、CF_PENDATA 和 CF_DSP* 枚举为 TYMED_NULL，`GetData` 一律 `DV_E_TYMED`；
由 CF_ENHMETAFILE 合成的 CF_METAFILEPICT 按 TYMED_MFPICT 取返回 `E_FAIL`。Windows 的结果待测（winref），测到后按它
修 ole32 的 `get_tymed_from_nonole_cf` 与图元文件路径。

构建：`x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror oleclipprobe.c -o oleclipprobe.exe -lole32 -luuid
-lgdi32 -luser32`。
