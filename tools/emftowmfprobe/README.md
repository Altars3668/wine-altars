# EMF 到 WMF：GdipEmfToWmfBits

`emftowmfprobe.exe` 用 GDI+ 以三种方式（EMF+ 双格式、仅 EMF+、仅 EMF）录一个填充矩形与一条线，再用 GDI 录一个矩形，对每个元文件以各种
标志调用 `GdipEmfToWmfBits()`，打印返回的大小、可放置头、METAHEADER 与各函数记录的个数，并与 `GetWinMetaFileBits()` 对照；最后看不同画框
下可放置头的边界与 SetWindowOrg/Ext。

起因：PowerPoint 把幻灯片导出为 WMF 时先画成 EMF，再以 `GdipEmfToWmfBits(emf, 0, NULL, MM_ANISOTROPIC, 0)` 取大小与数据；Wine 的桩返回
NotImplemented（作为大小就是 6），PowerPoint 不写文件也不报错。

`results/emftowmf.win.txt`（Windows 11 build 29671）：结果就是 `GetWinMetaFileBits()` 转出的 GDI 记录（EMF+ 记录不画），不带
`EmfToWmfBitsFlagsEmbedEmf` 时去掉携带 EMF 的 MFCOMMENT 注释，带 `EmfToWmfBitsFlagsIncludePlaceable` 时前加 22 字节可放置头（每英寸 48
单位，边界由参考设备换算）；缓冲不够返回 0。另一个发现：Windows 的 GDI+ 以双格式或仅 EMF 录制时写出完整的 GDI 记录（双格式 29 条），
Wine 只写 EMF+ 记录（6 条），所以 Wine 下 PowerPoint 导出的 EMF 只有 EMF+，转出的 WMF 几乎是空的——这一处尚未实现。

构建：`scripts/build-probe.sh tools/emftowmfprobe/emftowmfprobe.c tools/emftowmfprobe/emftowmfprobe.exe gdiplus gdi32 user32`。
