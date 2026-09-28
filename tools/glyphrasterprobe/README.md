# glyphrasterprobe

`glyphrasterprobe <字体文件> <字体名> [高度...]` 以 `AddFontResourceEx(FR_PRIVATE)` 只为本进程装入一个字体文件，打印各高度下的文字度量，以及几个字符的 `GetGlyphOutline`：`GGO_METRICS` 的返回值与盒子、`GGO_NATIVE` 与 `GGO_BEZIER` 的轮廓结构（每个轮廓里各段曲线的类型与点数，L 直线、Q 二次、C 三次）、`GGO_BITMAP` 与 `GGO_GRAY8_BITMAP` 的像素，以及 `ExtTextOut` 画进一位 DIB 的像素；再按字形号取 Word 画 Latin Modern Math 公式时请求的那几个字形的 `GGO_BITMAP`。字体不装进系统，Windows 上跑完删掉文件即可。

    scripts/build-probe.sh tools/glyphrasterprobe/glyphrasterprobe.c tools/glyphrasterprobe/glyphrasterprobe.exe gdi32

`glyphrasterprobe-92.win.txt`、`glyphrasterprobe-220.win.txt` 是 Windows 上 Latin Modern Math（CFF 轮廓）在 -92 与 -220 的结果。与 Wine 对比（2026-09-28）：

- 位图（`GGO_BITMAP`、`ExtTextOut`）两边基本一致，盒子差 1～2 像素，没有多出的整列像素——Word 导出 PDF 时 CFF 数学字体的竖线不是 GDI 的字形位图带来的。
- `GGO_BEZIER`：Windows 给出字体原来的三次曲线，每段一条 `TT_PRIM_CSPLINE` 记录、3 个点；Wine 把 FreeType 轮廓里的三次控制点（`FT_CURVE_TAG_CUBIC`）当成二次控制点，先近似成二次再转回三次，每条记录 6 个点。
- `GGO_NATIVE`：两边都把三次曲线近似成二次，但近似方法不同（Windows 多为每段 1 个二次、记录 2 个点）。
- `GGO_METRICS` 的返回值：Windows 返回一个与字形大小有关的正数，Wine 返回 1。
