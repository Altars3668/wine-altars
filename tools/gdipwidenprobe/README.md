# GdipWidenPath 的拐角：`gdipwidenprobe.exe`

Wine 的 gdiplus 原先没有圆角连接（画成斜切角），Office 普查里 `GdipWidenPath unimplemented line join 2` 出现上百次。
altars-up 起在外侧用不超过 90° 的贝塞尔圆弧连接两条边的偏移端点（与圆形线帽相同的画法），内侧保留斜切。gdiplus 的测试
只按点对比了直线和线帽，拐角的点列没有 Windows 的规格。

探针对直角折线、锐角折线和闭合三角形，用 10 单位宽的笔和四种连接方式各做一次 `GdipWidenPath`，打印结果的每个点与类型，
另对直角加一次 flatness 1.0。`results/gdipwidenprobe.wine.txt`：altars-up。Windows 的待测（winref）：圆角是贝塞尔还是展平的
折线、点的个数与位置，据此写 graphicspath 测试并对齐实现。

构建：`x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror gdipwidenprobe.c -o gdipwidenprobe.exe -lgdiplus`。
