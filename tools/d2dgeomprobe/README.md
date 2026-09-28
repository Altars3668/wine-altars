# D2D1 几何算法原生对照

两个探针问 Direct2D 的几何算法交出什么，同一个 PE 在 winref（Windows build 29671）和 Wine 下各跑一次再逐行对照：

- `d2dgeom.c`：`CombineWithGeometry`（四种模式、变换、曲线、hollow/open 图形）、`Outline`、`CompareWithGeometry`、group 的 `Simplify`/`Outline`/包含测试/`Tessellate`、`ComputeLength`/`ComputePointAtLength`（直线、椭圆、圆角矩形、曲线路径、group、变换）、`Widen`/`GetWidenedBounds` 的简单情形、`Stream`、圆弧的分段、不同大小四分之一圆的切线。
- `d2dstroke.c`：各种连接（MITER 不同上限、MITER_OR_BEVEL、BEVEL、ROUND）、线帽、虚线下的加宽面积与包围盒，原路折返，闭合图形加宽超过自身，旋转后椭圆和圆角矩形的包围盒，`Stream` 对各类段的回放。

构建与运行：

    scripts/build-probe.sh tools/d2dgeomprobe/d2dgeom.c tools/d2dgeomprobe/d2dgeom.exe d2d1 dxguid uuid
    WIN_HOST=… WIN_USER=… WIN_PORT=… scripts/winrun.sh tools/d2dgeomprobe/d2dgeom.exe > tools/d2dgeomprobe/d2dgeom.win.txt

`*.win.txt` 留存 winref 上的原生输出。探针只创建自己的几何对象，不读写文件，不需要设备或窗口，ssh 会话里即可运行。

## 测出的契约

- **展平**：每段三次 Bézier 递归二分，直到两个控制点与弦上 1/3、2/3 处的点在 L∞ 下都小于容差。由此算出的面积与长度与 Windows 到 7 位有效数字一致（圆 309.3903、椭圆 156.9767、曲线路径 50.53018）。
- **ComputePointAtLength**：点在展平折线上，顶点属于后一段，超出两端夹到端点；切线为该段两端曲线导数的加权插值，权重是到达该端点那一段的参数长度，曲线自身端点为 1/18；直线段取其方向；空几何给 NaN。
- **组合与轮廓**：先 `SetFillMode(ALTERNATE)`；图形从最上、再最左的顶点开始，按起点 y、x 降序排列；每点一次 `AddLines(1)`，显式回到起点，`EndFigure(CLOSED)`；外轮廓正向、洞反向；曲线保留为 Bézier；非法模式 `E_INVALIDARG`。
- **比较**：相同为 IS_CONTAINED，接触（边或角）即为 OVERLAP。
- **加宽**：MITER 超过上限在“上限 × 半线宽”处截平（上限按 ≥1），MITER_OR_BEVEL 才退为斜角，原路折返时截平的斜接向前伸出；无样式为平帽、MITER、上限 10。
- **椭圆/圆角矩形**：`Simplify` 先设填充模式和 `FORCE_ROUND_LINE_JOIN`，椭圆曲线之间有零长直线，圆角矩形从 (left, top+ry) 顺时针；包围盒取 Bézier 的极值。
- **Stream**：直线、三次曲线各自合并成一次调用，二次曲线逐个，弧保持为弧，零长弧丢弃，段标志变化时重放。

## 仍有的差异

- Windows 的 `Widen` 输出是 WINDING、open 图形且自重叠；Wine 输出填充相同、互不相交的闭合图形。
- Windows 把 hollow 图形反向原样放进组合与轮廓的结果，对角接触时还给出一个退化的 hollow 图形；Wine 两者都不给。
- 控制点偏差恰在容差边界时，Windows 的 float 运算顺序决定是否再细分（r=40、160 的四分之一圆末段），Wine 与之差一层。
- NaN 的位型不同（Windows 打印 `-nan`，Wine 为 `-nan(ind)`）。
