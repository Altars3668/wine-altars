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

## 结论（Windows 11 29671，浅色主题，results/windows-29671.txt 与 windows-29671-sheet.png）

- 只有请求了圆角（ROUND、ROUNDSMALL）的弹出窗口才有 DWM 的柔和阴影；DEFAULT、DONOTROUND 是直角、无阴影，设了边框色也不画。
- ROUND 的阴影：左右约 26 px，紧贴窗口处约暗 12%；上方约 13 px、暗 6%；下方最深，紧贴处暗 22%，32 px 外仍未消失
  （相当于向下偏移的模糊阴影）。`CS_DROPSHADOW`、`WS_EX_TOOLWINDOW` 不影响它。
- ROUNDSMALL 的阴影小得多：左右约 10 px（暗约 5%），上方几乎没有，下方约 20 px（暗约 9%）。
- 设了边框色就在窗口最外一圈像素画不透明的该色（0x616161 → 97,97,97；红）；`DWMWA_COLOR_NONE` 不画。
- 不设边框色时最外一圈像素各边不同（左右 114、上 118、下 106），跟着阴影变，像是半透明的默认边框叠在阴影上；
  颜色与透明度要在黑、白背景上再测才能定。
- `CS_DROPSHADOW` 的直角窗口是经典的右下硬阴影，约 5 px（71、86、106、121、126）。

Wine 现在只画圆角与给定颜色的边框（altars-up `13af402977c`、`dc64c3f9fb8`），阴影与默认边框都还没有。第 167 批的边距
（32 px）不够：ROUND 的阴影到下方 32 px 处仍暗 8.6%，相邻格子也互相串影；第 168 批时有人在用那台电脑，探针拒绝运行，
加大边距、加黑白背景的一批待下次。
