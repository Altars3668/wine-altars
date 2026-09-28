# D2D1 逐图元抗锯齿原生对照

`d2daa.c` 在 64×64 的 WIC 位图上用白色画刷画在透明黑底上（alpha 即覆盖率），逐行打印跨过边缘的 alpha：填充的圆、三角形、旋转的矩形、带三次曲线的路径，1 与 2 像素宽的斜线、椭圆描边，不落在像素边上的轴对齐裁剪与矩形；先 `D2D1_ANTIALIAS_MODE_PER_PRIMITIVE`，再 `ALIASED`。

    scripts/build-probe.sh tools/d2daaprobe/d2daa.c tools/d2daaprobe/d2daa.exe d2d1 dxguid uuid ole32 windowscodecs
    WIN_HOST=… WIN_USER=… WIN_PORT=… scripts/winrun.sh tools/d2daaprobe/d2daa.exe > tools/d2daaprobe/d2daa.win.txt

`d2daa.win.txt` 是 winref（build 29671）上的输出。

## 测出的契约

- 抗锯齿是精确的面积覆盖：左边在 10.25 的矩形，像素 10 为 191（0.75）；上边在 10.5，为 128。
- 轴对齐裁剪按 `PushAxisAlignedClip` 的模式抗锯齿，同样按面积：左 10.3 → 178，下 50.2 → 51，上 10.6 → 102。
- 1 像素宽的斜线一行的覆盖和约为 $w/\sin\theta$（这里 558/255 ≈ 2.19，$1/\sin 28.2° \approx 2.12$），没有一个像素被整个覆盖。
- ALIASED 按像素中心取舍：中心恰在上边、左边算在内，在下边、右边不算。
- 带强弯曲三次曲线的路径，ALIASED 下在 y=16 覆盖 x=10..32；Wine 原先把每段三次曲线当一段二次曲线，少了左边 8 个像素。

## Wine 的做法与剩余差异

每个抗锯齿图元先在它的设备空间包围盒内画一张 8 倍多重采样的 R8 覆盖遮罩：三角形的边由多重采样给出覆盖，曲线、描边曲线和圆角连接在像素着色器里按有符号距离给出盒式滤波的覆盖，MAX 混合取并集；再用一个覆盖包围盒的四边形把画刷乘遮罩（各样本的平均）画到目标上。像素对齐的矩形跳过遮罩。裁剪的抗锯齿边在像素着色器里按重叠面积精确计算。

矩形与裁剪与 Windows 逐值相同；直边是 8 级采样的量化值（如三角形边 32 对 48），曲线边缘差十几级以内。
