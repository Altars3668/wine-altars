# Office 自动化回归探针

这三个 VBScript 通过真实的 `Word.Application`、`Excel.Application`、`PowerPoint.Application` COM 类驱动安装在专用 Wine prefix 中的 Office，而不是模拟应用或许可证。各收两个 Windows 路径参数：本地输出文件、进度日志。进度每步立即写入，卡住时不必等待 `cscript` 的 stdout 缓冲。运行时加超时；**不发送邮件、不上传云文档、不打印**。

前提：Click-to-Run 的 `root/vreg/*.vreg.dat` 含真实 COM 注册。若 `HKCR\Word.Application\CLSID` 等为空，先用系统 Python（有 `hivex`）运行 `scripts/export-office-registry.py --automation-only --win-c "$WINEPREFIX/drive_c" -o <新目录>`，审阅生成的 `31-automation-*.reg`、备份现有注册表后，才用 `DIST=/opt/wine-altars WINEPREFIX=... scripts/apply-office-registry.sh <目录>` 导入。导入脚本等待 wineserver 退出，**先关闭该 prefix 的 Office 进程**。输出来自本机安装包，不复制他机账户/许可、不编造 CLSID。

例如让 Wine 的 `cscript.exe //nologo` 执行 `word-save.vbs`，传入 `Z:\tmp\word-check.docx`、`Z:\tmp\word-progress.txt`。Word 成功后验证文件是合法 ZIP，包含 `[Content_Types].xml` 和 `word/document.xml`，正文有 `wine-altars Word automation smoke`。

2026-09-26 的前测：Word 的 DOCX 保存成功；Excel 虽然 COM 激活成功，`Workbooks.Add` 却阻塞，独立启动后点空白工作簿只改标题、画面仍停在开始屏幕。根因是 dcomp 的 `InteractionSources.RemoveAll` 没解除原 Visual 的交互源预留；修复并按 Windows 实测区分 `RemoveAll` 与单项 `Remove` 后，**同一 Excel 探针已通过**：新建工作簿、填写 A1、保存并关闭，XLSX 解包校验通过；直接点击空白工作簿也绘出了表格网格。PowerPoint 的幻灯片创建、标题修改、保存 PPTX 和解包校验通过；脚本进度写到“saved”，PowerPoint 进程已退出，但“closed”标记未写到，不能据此单独声称退出回调语义已验清。Outlook 尚未启动或访问邮箱，也没有发送邮件。

`powerpoint-show.vbs` 打开已有演示文稿、显示指定秒数后关闭（参数：演示文稿、进度日志、可选秒数），用于在 PowerPoint 真实显示时截取窗口。在无根 Xwayland 上根窗口不能读取像素，应按窗口 ID 截取 PowerPoint 顶层窗口；:2 这类竖屏显示器上幻灯片位于编辑区的中下部，只截上半部分会误以为幻灯片空白。

`word-save.vbs` 只退出自己启动的 Word（`GetObject` 接到的已运行实例不动）；早先版本不调用 `Quit`，自动化启动的 Word 会在脚本结束后一直驻留。`scripts/office-regress.sh [word] [excel] [powerpoint]` 依次运行三个保存探针，报告退出码、文件是否为含目标部件的合法 OOXML，以及探针到达的步骤；它等待各应用进程退出，而不是 `wineserver -w`（Click-to-Run 服务常驻，wineserver 不会退出）。

`powerpoint-effects.vbs <输出.pptx> <进度日志> <图片> [秒数]` 在一张空白幻灯片上依次施加 PowerPoint 经 Direct2D 绘制的效果：渐变、阴影、发光、柔化边缘、映像、三维棱台、图片（灰度、亮度对比度、模糊艺术效果）、文字的发光/阴影/映像。每一步单独容错并记录结果，保存后保持显示若干秒供截图，再退出。某一步让 PowerPoint 崩溃时，其后各步都会以 462（`1CE`）失败，第一条失败即是崩溃点。


`excel-charts.vbs <输出.xlsx> <进度日志> [秒数]` 填一小张表，再加上 Excel 经 Direct2D/DirectWrite 绘制的内容：带阴影的簇状柱形图、折线图、三维饼图、数据条、三色色阶、图标集、两种迷你图，保存后保持显示若干秒供截图，再退出。每一步单独容错并记录结果。探针只调 COM 接口，不读用户文件。要看 Excel 的 d2d 日志，先 `wineserver -k`，再以带 `WINEDEBUG` 的 `cscript` 作为会话首个进程启动它：COM 激活出的 Excel 继承会话首个进程的环境，也把输出写到它的 stderr；截图要带 `:2` 的 `XAUTHORITY`，按标题“… - Excel”找窗口。
