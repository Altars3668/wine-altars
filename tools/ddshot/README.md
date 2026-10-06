# ddshot：把 Office 下拉面板“照”下来，在 Windows 与 Wine 上逐像素对比

2026-10-06 用来回答“字体等展开面板是直角、大宽滚动条，和 Windows 不符”到底差在哪。结论与修复见
[docs/office-upgrade-references-corners-20261006.md](../../docs/office-upgrade-references-corners-20261006.md)。

```
ddshot [-attach] [-excel]
```

打开“开始”选项卡上的字体下拉（MSAA 名称“字体”/“Font”、角色 combobox，点它右端的箭头），打印下拉窗口的事实：

- 类名、窗口/客户区矩形、样式、扩展样式、类样式、DPI；
- `DWMWA_EXTENDED_FRAME_BOUNDS`、`DWMWA_WINDOW_CORNER_PREFERENCE`、`DWMWA_BORDER_COLOR`、`DWMWA_SYSTEMBACKDROP_TYPE`、
  `DWMWA_NCRENDERING_POLICY` 的读取结果，窗口区域，分层属性；

再把下拉四周（外扩 24 px，含阴影）的屏幕截下来，指针分别在：箭头上（open）、列表中部（list）、滚动条轨道（track）、
滑块（thumb）、面板外（away），外加一张 `PrintWindow(PW_RENDERFULLCONTENT)`（只有窗口自己的像素，没有 DWM 加的东西）。
每张图以 base64 PNG 打印在 `PNG-BEGIN <名字> <宽>x<高>` 与 `PNG-END` 之间；编码前，凡中心点落在**别的进程**窗口上的
8×8 格都涂成灰色，只带回 Office 自己的像素。

- 不带 `-attach`：自己启动一个 Word（`/w /q /a`：空白文档、无启动画面、不加载加载项），结束时关闭它。已有
  `WINWORD.EXE` 在运行、输入桌面不是 Default（锁屏）、或两分钟内有人用过键盘鼠标（`GetLastInputInfo`），都直接拒绝，
  绝不碰别人正在用的 Word。
- `-excel`：同样规则启动 Excel，在开始屏幕按 Esc 进入空白工作簿（不打开文件，不进最近列表）。Excel 的字体列表与
  Word 是同一个 NetUI 控件，winref 上用户的 Word 开着时用它取参照。
- `-attach`：用已经在运行的那个（Wine 测试显示上的 Word）。

进程声明为 PMv2 DPI 感知，坐标与截图都是物理像素。在 winref 上要在交互桌面会话里跑：

```
scripts/winbatch.sh tools/ddshot/ddshot.exe scripts/win-deskrun.ps1 -- \
    "powershell -NoProfile -ExecutionPolicy Bypass -File win-deskrun.ps1 -Exe ddshot.exe -Arguments -excel -Wait 420"
```

输出里的 PNG 用 scratchpad 里的小脚本按 `PNG-BEGIN/END` 解码即可。

## 结果（results/）

- `windows-29671-excel.txt`：Windows 11 29671 上 Excel 字体下拉的窗口事实（截图本身没有提交）。
- `wine-dc64c3f9fb8-word.txt`：Wine（altars-up `dc64c3f9fb8`）上 Word 字体下拉的同一组事实。
- `corners-windows-wine.png`：两边左上角、右上角（含滚动条箭头与滑块）、右下角放大 6 倍并排。

两边的窗口事实一致：`Net UI Tool Window`、303 px 宽、`WS_POPUP|WS_CLIPSIBLINGS|WS_CLIPCHILDREN`、
`WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_TOPMOST`、LWA_ALPHA 255、`DWMWCP_ROUNDSMALL`。滚动条逐列相同（17 px 的 `f5`
轨道、9 px 直角滑块、1 px 白缝、`0x61` 边框），悬停也不变宽。剩下的差别只有 DWM 的阴影：左右约 10 px，底部约 14 px 且更深。
