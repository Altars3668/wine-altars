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

# FillOpacityMask 原生对照

`d2dmask.c` 把一张 4×4 的 A8 遮罩经白色画刷画到透明黑底的 32×32 WIC 位图上（ALIASED），逐行打印 alpha：整张放大 4 倍、取中间 2×2 放大 8 倍、不给目标矩形、只给源矩形、源矩形越出遮罩或完全在遮罩外、零宽的源矩形、倒置的目标或源矩形、另外两种 content 与一个非法值、1.1 的 `ID2D1DeviceContext::FillOpacityMask`、BGRA 与 192 DPI 的遮罩、非 ALIASED 的目标；最后看 `DrawBitmap` 怎样对待倒置的源矩形。

    scripts/build-probe.sh tools/d2daaprobe/d2dmask.c tools/d2daaprobe/d2dmask.exe d2d1 ole32 uuid
    WIN_HOST=… WIN_USER=… WIN_PORT=… scripts/winrun.sh tools/d2daaprobe/d2dmask.exe > tools/d2daaprobe/d2dmask.win.txt

`d2dmask.win.txt` 是 winref（build 29671）上的输出。

## 测出的契约

- 遮罩按双线性采样，在遮罩自己的边上夹取，而不是在源矩形的边上：取中间 2×2 时，边上的像素照样混入源矩形外的纹素（第一个像素 80，正是行 0/1、列 0/1 的双线性）。
- 源矩形先规范化（左右或上下颠倒都一样），再与遮罩求交；完全在遮罩外时整块取最近的角落纹素（128）；零宽的源矩形什么都不画。
- 没有目标矩形时，源矩形（没有就是整张遮罩）按自身的 DIP 尺寸放在原点；倒置的目标矩形与正的相同。
- 192 DPI 的 4×4 遮罩只有 2×2 DIP，缩小也是双线性（64、160），不是盒式平均。
- content 只做校验：GRAPHICS、TEXT_NATURAL、TEXT_GDI_COMPATIBLE 画出相同的值，值 3 在 `EndDraw` 返回 `E_INVALIDARG`。
- 只用遮罩的 alpha：BGRA 遮罩与 A8 的结果相同。
- 两个入口在非 ALIASED 的目标上都记 `D2DERR_WRONG_STATE`，什么也不画（同一次绘制里之前的 `Clear` 照常生效）。
- `DrawBitmap` 同样把倒置的源矩形规范化。

Wine（wine-src 的 `FillOpacityMask` 实现之后）在 GL、Vulkan 下的输出与此逐行相同，只差恰好落在半数上的双线性值的舍入（141.5 → 141 对 142，159.5 → 159 对 160）。

# FillMesh 原生对照

`d2dmesh.c` 用半透明（alpha 0.5）白色画刷把网格画到透明黑底的 64×64 WIC 位图上（ALIASED），这样被画两次的像素是 192 而不是 128：沿对角线分开的正方形、两个交叠的三角形、边穿过像素中心的三角形、两种绕向、退化三角形、世界变换下的三角形；再把一个圆的 `Tessellate` 结果用 `FillMesh` 画，与 `FillGeometry` 画同一个圆比较；最后是非 ALIASED 目标、从未 Close 的网格和空网格。

    scripts/build-probe.sh tools/d2daaprobe/d2dmesh.c tools/d2daaprobe/d2dmesh.exe d2d1 ole32 uuid

## 测出的契约

- 网格就是依次画出的三角形列表：交叠处画刷混合两次（192），共享的边只画一次（D3D 的左上规则：像素中心落在上边、左边上算，落在下边、右边上不算），两种绕向都画，退化三角形不画，世界变换照常生效。
- 非 ALIASED 目标、从未 Close 的网格都在 `EndDraw` 返回 `D2DERR_WRONG_STATE`、什么也不画；空网格 `S_OK`。
- 半径 10.3 的圆：`Tessellate`（容差 0.25）给出 22 个三角形，填出 324 个像素；ALIASED 的 `FillGeometry` 填出 326 个——Windows 画曲线也先折线化，比精确的圆（Wine 的 332 个）少。

Wine（wine-src `ee62e28`）在 GL、Vulkan 下只差交叠处的混合舍入（191 对 192），tessellation 填出的像素数与 Windows 相同。

# 描边连接原生对照

`d2djoin.c` 用 4 像素宽的白色描边（ALIASED）画尖端朝右、尖在 (40, 20.5) 的 V 形，打印沿角平分线那一行画到哪个像素：30° 与 8° 的 V 在 MITER（上限 2、10）、MITER_OR_BEVEL（上限 2）、ROUND、BEVEL 与不给样式时；再画一条走到 (40, 20.5) 又原路折返的线。

    scripts/build-probe.sh tools/d2daaprobe/d2djoin.c tools/d2daaprobe/d2djoin.exe d2d1 ole32 uuid

## 测出的契约

- MITER 的斜接超过上限时在 `上限 × 半宽` 处垂直于角平分线截平（上限 2 的 30° V 到 43；不给样式时是上限 10 的 MITER，8° V 到 59），不是画满，也不是斜切；MITER_OR_BEVEL 超限时斜切（到 40）；ROUND 到圆（41）。
- 原路折返时，MITER 向前画一个长为 `上限 × 半宽` 的矩形（上限 10 到 59，上限 2 到 43），ROUND 画半圆（41），BEVEL 与 MITER_OR_BEVEL 不向前延伸（39）。
- 与 `Widen` 的实测一致（`d2d_stroke_pieces_join`）。

Wine 原先 MITER 超限时画满（30° 到 47，8° 到 68），折返时不论连接都向前画 25 个单位（到 64）；wine-src `bbce22a` 之后与此逐行相同，只有两处贴边 0.04 像素的像素因 Windows 光栅化的顶点定点化而不同。
