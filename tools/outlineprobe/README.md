# outlineprobe

`outlineprobe <字体文件或 -> <字体名> <高度> [文字]` 以 `AddFontResourceEx(FR_PRIVATE)` 只为本进程装入一个字体文件（`-` 表示用已装的字体），对每个字符打印 `GetGlyphOutline` 的结果：`GGO_METRICS` 在四种字体质量下、`GGO_UNHINTED` 时和 30° 旋转矩阵下的返回值与盒子，`GGO_BITMAP`、`GGO_GRAY2_BITMAP`、`GGO_GRAY8_BITMAP` 的返回值与盒子，以及 `GGO_NATIVE`、`GGO_BEZIER`（带与不带 hinting）的每一条记录和它的点，精确到 16.16 定点的最后一位。文字写成 `#n,m,...` 时按字形号取。

    scripts/build-probe.sh tools/outlineprobe/outlineprobe.c tools/outlineprobe/outlineprobe.exe gdi32

`results/` 是 2026-09-28 在 winref（Windows 最新版）上的结果：Nimbus Sans（CFF）16～4000 像素、Latin Modern Math（CFF）92 与 1000 像素、Liberation Sans（TrueType）92 像素、空格等边界情形，以及 gdi32 测试用的小 CFF 字体（`make-cff-test-font.py` 生成）在 1000 像素。量到的规则（wine-src `dcf9a14` 照此实现）：

- `GGO_METRICS` 返回字形的 GLYPHBITS 的大小：16 字节头，加上黑框按整字节成行的一位位图，凑成 4 的倍数，即 $\mathrm{align}_4(16+\lceil w/8\rceil h)$；各字体质量、不 hint、变换后都如此，空字形的黑框是 1×1，返回 20。
- `GGO_BEZIER` 把 CFF 的三次曲线原样给出，每段一条 3 点的 `TT_PRIM_CSPLINE` 记录；TrueType 的二次曲线照旧按连续的一串合成一条记录（Wine 原来就与 Windows 相同）。
- `GGO_NATIVE` 把每段三次曲线给成一条 `TT_PRIM_QSPLINE` 记录，含 $n$ 段二次曲线：三阶差分 $d=p_3-3p_2+3p_1-p_0$ 在两个方向上都不超过 1 像素时 $n=1$，否则取满足 $\max(|d_x|,|d_y|)\le 10n^3$ 的最小 $n\ge 2$；各段按参数等分，控制点是该段三次曲线 $a_0a_1a_2a_3$ 的 $\frac{3(a_1+a_2)-a_0-a_3}{4}$。`checkrule.py` 按这条规则核对输出：Windows 的 1240 段三次曲线没有一段段数不同，控制点最多差 $3.7\times10^{-5}$ 像素（Windows 用定点运算，与精确值差一两个 $1/65536$）。

还不同的：黑框。Windows 的黑框贴着曲线本身（或栅格化后的黑像素），Wine 用含控制点的控制框，所以 Wine 的盒子常常大一两个像素，旋转后更大（Wine 把未旋转的框的四角变换后取外接框）；灰度位图的盒子在 Windows 上还会因格式不同差一列。hinting 后的坐标也不同：TrueType 是 FreeType 的 v40 解释器与 Windows 的差别，CFF 两边的 hinter 不同，因此个别贴着阈值的曲线段数会不同。
