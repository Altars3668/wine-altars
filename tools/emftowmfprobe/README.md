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

## 缓冲不够时写了什么

`emftowmfprobe.exe` 的最后一节把同一个元文件转进各种大小的缓冲，打印返回值、写了多少字节、METAHEADER 的 mtSize/mtMaxRecord 和写到的最后
一条记录。Windows 是边写边走的：先写 METAHEADER（mtSize 与 mtMaxRecord 为 0），装得下的记录一条条写进去，装不下就返回 0；只差最后的
META_EOF（6 字节）时返回“大小 − 6”并把头里的 mtSize 补对；可放置头最后才写，失败时不写；缓冲连 METAHEADER 都装不下时返回 0、什么都不写。
Wine 的 `GdipEmfToWmfBits()` 已照此重写（gdiplus 提交 “Write GdipEmfToWmfBits() records one by one, as Windows does.”）。

## EmfPlusFlags 的显示位：`emfplusflagsprobe.exe`

在屏幕 DC 与内存 DC 上各录一个双格式元文件，在 UnitDisplay 下填 25,25–75,75 的矩形，然后把 EMF+ 头的 EmfPlusFlags 改成 0、0x1、
0x80000000、0x80000001、0x2，各自回放进 100×100 的位图，看第 50 行从哪画到哪。`results/emfplusflags.win.txt`：录制时写的是 0x1；
**第 0 位**置位时 UnitDisplay 按像素（25 到 74），清零时按 1/100 英寸（96 DPI 下 24 到 71）；第 31 位与其他位不起作用。Wine 原来读的是
另一位，把每个 UnitDisplay 下画的 EMF+ 缩小约 4%；已改为读第 0 位。

## 画框与边界：`emfframeprobe.exe`

用各种单位（像素、英寸、点、文档、毫米、GDI）与不给画框三种方式录元文件，打印 EMF 头的 rclFrame/rclBounds、参考设备的像素与毫米，
再读回 GDI+ 的头（位置、大小、DPI）、`GdipGetImageBounds`、宽高与 `GdipGetImageDimension`；另有 GDI 录的元文件、带可放置头与校验和为 0
或不带可放置头的 WMF。`results/emfframe.win.txt` 的要点：

- 录制时 rclFrame 的左上是 round(X·f)，右下是 round((X+W)·f − 1 像素)，f 由参考 DC 的 HORZSIZE/HORZRES 算；`MetafileFrameUnitGdi`
  原样照抄；不给画框时取画出内容的包围盒换算，rclBounds 是含端点的 GDI 边界，不加 1。
- 读回时 bounds 的宽是 frame 宽 ÷ 2540 × DpiX + 1，头里的宽取整，`GdipGetImageWidth` 取整，元文件的 `GdipGetImageDimension` 是
  HIMETRIC。
- WMF 的可放置头：bounds 与头的 X/Y 就是 Left/Top（Wine 把它们除以了每英寸单位数）；校验和不对的可放置头被忽略，类型为
  MetafileTypeEmf（3），raw format 是 EMF，没有可放置头时同样是 3。

这些读取与录制的规则 Wine 尚未全部照做。

## 双格式元文件由 GDI 回放：`dualplayprobe.exe`

GDI+ 在屏幕 DC 上录一个双格式元文件，填红色矩形，然后用 GDI 的 `PlayEnhMetaFile` 回放进白色 DIB，并用 GDI+ 画进 100×100 的位图，
打印内外的像素、头与边界，以及录下的记录。它回答的是：双格式元文件里的 GDI 记录能不能单独画出同样的东西（Windows 能，Wine 原来只写
EMF+ 记录）。

构建：`scripts/build-probe.sh tools/emftowmfprobe/<probe>.c tools/emftowmfprobe/<probe>.exe gdiplus gdi32 user32`。
