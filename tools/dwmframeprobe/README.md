# dwmframe：Windows 11 的 DWM 在弹出窗口四周画什么

回答“Office 的下拉面板在 Windows 上有阴影、有边框，Wine 下没有”到底该画成什么样。在中灰（0x80）背景上短暂显示 12 个白色
弹出窗口（置顶、不激活、关掉 DWM 过渡动画），等 DWM 稳定后截下每个窗口四周 64 px（96 dpi 下）的屏幕，约两秒后全部关掉；
再在黑色、白色背景上各显示一遍不设边框色的那几个（名字后缀 `@black`、`@white`），用来分出默认边框的颜色与透明度，
也让阴影的分辨率从灰底的 1/128 提高到 1/255。两分钟内有人用过键盘鼠标或桌面锁着就拒绝运行（`-force` 例外）。

```
scripts/winbatch.sh tools/dwmframeprobe/dwmframe.exe scripts/win-deskrun.ps1 -- \
    "powershell -NoProfile -ExecutionPolicy Bypass -File win-deskrun.ps1 -Exe dwmframe.exe -Wait 120"
```

| 窗口 | 圆角偏好 | 边框色 | 其他 |
|---|---|---|---|
| round / roundsmall | ROUND / ROUNDSMALL | 不设 | |
| round-color-none / round-red / round-616161 | ROUND | `DWMWA_COLOR_NONE` / 红 / 0x616161 | |
| default / donotround / donotround-red | DEFAULT / DONOTROUND / DONOTROUND | 不设 / 不设 / 红 | |
| layered-roundsmall-616161 | ROUNDSMALL | 0x616161 | `WS_EX_LAYERED`，alpha 255（Office 下拉面板的设置） |
| dropshadow-default / dropshadow-round | DEFAULT / ROUND | 不设 | 窗口类带 `CS_DROPSHADOW` |
| toolwindow-round | ROUND | 不设 | `WS_EX_TOOLWINDOW` |

## 结论（Windows 11 29671，浅色主题，results/windows-29671.txt 与 windows-29671-sheet.png，第 169 批）

- 只有请求了圆角（ROUND、ROUNDSMALL）的弹出窗口才有 DWM 的柔和阴影；DEFAULT、DONOTROUND 是直角、无阴影，设了边框色也不画。
  `CS_DROPSHADOW`、`WS_EX_TOOLWINDOW` 不影响它。
- 阴影是纯黑、带透明度（黑底上窗外全为 0；白底与灰底算出的透明度一致）。`fit_shadow.py` 把它拟合成窗口矩形的
  模糊副本（Φ 为正态分布函数，两层按透明度叠加）：
  - ROUND：两层，(α 0.1445, σ 7.08 px, 下移 1.98 px) 与 (α 0.1487, σ 19.55 px, 下移 34.5 px)；均方根误差 0.72/255，
    最大 4.3/255（96 dpi）。左右约 44 px、上方约 14 px、下方约 78 px 外才小于 1/512。
  - ROUNDSMALL：一层，(α 0.0977, σ 4.05 px, 下移 9.17 px)；均方根误差 0.39/255；左右 9、上 1、下 18 px。
- 设了边框色就在窗口最外一圈像素画不透明的该色；`DWMWA_COLOR_NONE` 不画。
- 不设边框色时画默认边框：`fit_border.py` 从三种背景下的最外一圈像素拟合出 **0.40 不透明度、约 0x767676（预乘 47）的灰色，
  叠在背景与阴影之上**——窗口自己的最外一圈像素被它取代，不是叠在窗口内容上（黑底上是 47，白底上随阴影是 181–189）。
  ROUND 与 ROUNDSMALL 相同；均方根误差 0.7–1.2/255。
- `CS_DROPSHADOW` 的直角窗口是经典的右下硬阴影，约 5 px（71、86、106、121、126）——Wine 尚未实现。

## Wine

altars-up `49c42e13386`、`f073327c4cf` 照此实现：win32u 让没有边框色的圆角窗口保留圆角半径，默认边框时窗口形状向内收一圈；
winex11 给 override-redirect 的圆角窗口在其正下方配一个 ARGB 的 override-redirect 窗口（点击穿透），按上面的公式画阴影，
默认边框时在那一圈画 0.40 的灰色。需要合成管理器（mutter 在用户的 Xwayland 上持有 `_NET_WM_CM_S0`）；
mutter 50 对带形状的无边框窗口不画它自己的阴影，所以不会重复。受管窗口、逐像素透明的窗口、虚拟桌面不画。

`CS_DROPSHADOW`（直角窗口，打开“窗口下显示阴影”时，Windows 默认开、Wine 默认关）：Windows 11 只在窗口右侧和下方画柔和的
黑影，拟合得一块从窗口左上角向内 7 px 到右下角外 2 px 的矩形、σ 1.217 px 的模糊、透明度 0.497（96 dpi），均方根误差
0.06/255。altars-up `359e8dbb15b` 照此画，与 Windows 最大差 2/255。圆角窗口带 `CS_DROPSHADOW` 时只有 DWM 的阴影。

设了边框色的圆角窗口（Office 的下拉面板是 0x616161）：Windows 把角上部分在外的像素按覆盖比例把边框色混在背后之上。
altars-up `c883cf11f07`、`9c0fe75bb77` 让 win32u 把这些像素从窗口形状里去掉，由阴影窗口按覆盖比例画边框色、叠在阴影上
（它们都在边框的圆弧上，内容就是边框色）。

`results/wine-359e8dbb15b.txt` 与 `-sheet.png` 是在 Xvfb + xcompmgr 上的输出（前缀里打开了“窗口下显示阴影”）：窗口外与 Windows 的均方根差 0.3–0.7/255、
最大 2–8；默认边框那一圈在灰底上最大差 2；设了边框色的窗口连角上一圈最大差 3–7（原来 39–55）。只剩
`DWMWA_COLOR_NONE` 的窗口角上几个像素：那里是窗口内容按覆盖比例混在背后，驱动不知道内容的颜色，Wine 的形状是二值的。在仿用户环境的隔离 GNOME Shell（mutter 50.1）里，阴影窗口按边距定位、紧贴在各自的弹出窗口之下。
