# D2D1 渐变原生对照

三个探针问 Direct2D 的渐变画出什么，同一个 PE 在 winref（Windows build 29671）和 Wine 下各跑一次再逐列对照：

- `d2dgrad.c`：线性、径向渐变在 CLAMP/WRAP/MIRROR 与 gamma 2.2/1.0 下的像素，到透明色的渐变，stop 不在两端时的镜像；集合的各个 getter；1.1 的 `CreateGradientStopCollection` 各种 pre/post 空间、插值模式、缓冲精度，以及非法参数的返回值。
- `d2dgrad2.c`：能看出换算的中间色在四种 pre/post 空间组合、直通与预乘插值下的像素，`GetGradientStops`/`GetGradientStops1` 交回什么，半透明 stop，画刷不透明度；0 个、1 个、倒序、乱序、同位置、落在 [0,1] 外的 stop；零长度线性渐变、零半径与焦点偏移（椭圆内、椭圆外）的径向渐变。
- `d2dgrad3.c`：没有确定位置时画什么——零长度、零半径、焦点在椭圆外或恰在边上时看不到的区域，每像素重复一次以上的渐变。

构建与运行：

    scripts/build-probe.sh tools/d2dgradprobe/d2dgrad.c tools/d2dgradprobe/d2dgrad.exe d2d1 dxguid uuid ole32 windowscodecs
    WIN_HOST=… WIN_USER=… WIN_PORT=… scripts/winrun.sh tools/d2dgradprobe/d2dgrad.exe > tools/d2dgradprobe/d2dgrad.win.txt

`*.win.txt` 留存 winref 上的原生输出。探针画进内存里的 WIC 位图，不需要设备或窗口，ssh 会话里即可运行。

## 测出的契约

- **扩展模式**：位置先归入 [0,1] 再在 stop 间插值——CLAMP 截断，WRAP 取小数部分，MIRROR 为周期 2 的三角波。落在 [0,1] 外的 stop 只通过它们在 [0,1] 内造成的颜色起作用（stop 在 −1 与 2 时，左端是 2/3 红 + 1/3 蓝，不是纯红）。
- **stop 顺序**：按位置稳定排序后插值，同位置的几个取最后一个；`GetGradientStops` 仍按给出的顺序交回。0 个 stop 返回 `E_INVALIDARG`，1 个 stop 是纯色。
- **颜色空间**：1.0 的 `CreateGradientStopCollection` 等于 1.1 的 pre=sRGB（gamma 2.2）或 pre=scRGB（gamma 1.0）、post=sRGB、8 位 UNORM、直通插值；gamma 1.0 时给出的 sRGB stop 先按 sRGB 曲线换成线性，`GetGradientStops1` 交回换算后的值（0.5 → 0.2140），`GetGradientStops` 交回原值。1.1 的 stop 就在 pre 空间里给出，两个 getter 都原样交回。
- **插值与预乘**：直通模式插值未预乘的颜色与 alpha，换到 post 空间，再预乘；预乘模式插值预乘后的颜色，换空间时先去预乘、换算、再预乘。画刷不透明度乘在全部四个通道上。
- **缓冲精度**：UNORM 精度（8 位、8 位 sRGB、16 位）把 pre 空间里的 stop 截到 [0,1]；浮点精度保留超界值，到输出时才截断。精度 UNKNOWN 返回 `D2DERR_INVALID_CALL`；CUSTOM 空间、非法插值模式、gamma 或扩展模式返回 `E_INVALIDARG`。
- **退化情形**：零长度线性渐变在 CLAMP 下是 [0,1] 两端颜色各半，WRAP/MIRROR 下是整个周期的平均色；零半径径向渐变在 CLAMP 下是末端颜色，另两种模式为平均色；每像素重复一次以上的渐变显示平均色。
- **径向焦点**：取从焦点出发的射线与椭圆较远的交点；焦点在椭圆外时，射线不向前碰到椭圆的区域画 t=1 的颜色。

## 仍有的差异

- Windows 用纹理采样渐变：WRAP 接缝处半个纹素内混入另一端的颜色，被压到一两个像素内的 CLAMP 渐变也被滤波平均；Wine 逐像素精确求值，这些像素差 2～5 级（焦点恰在椭圆边上时，外侧区域 Windows 是接缝混色，Wine 是末端颜色）。
- gamma 1.0 下接近 0 的线性值，Windows 的 CLAMP 结果比精确的 sRGB 曲线低 2 级（47 对 49），WRAP 则高 1 级，成因未查明。
