# WIC 缩放器各插值模式：`wicscaleprobe.exe`

PowerPoint 用 `IWICBitmapScaler` 以 Fant 模式缩放图片，Wine 的缩放器原先除最近邻外的模式都退化成最近邻。探针把一维阶跃、渐变与
棋盘格在 Gray8、BGR24、BGRA32（直通 alpha）、PBGRA32、RGBA64、RGBAFloat128 与 8 位索引格式上放大、缩小，五种模式逐一打印
输出格式、`CopyPixels` 的结果与像素，并从中间位置再拷一次最后一行，看结果是否与拷贝起点有关。

`results/wicscaleprobe.wine.txt`：altars-up 实现各模式之后的输出（线性、Keys 三次 a=-1/2、按缩小倍数加宽的高质量三次、
缩小时按覆盖面积平均、放大时线性的 Fant；8 位、16 位、浮点通道插值，索引与打包格式仍取最近邻）。Windows 的输出待测（winref），
用来对齐像素中心、边缘、舍入与哪些格式插值。

构建：`x86_64-w64-mingw32-gcc -O2 -Wall -o wicscaleprobe.exe wicscaleprobe.c -lole32 -lwindowscodecs`。
