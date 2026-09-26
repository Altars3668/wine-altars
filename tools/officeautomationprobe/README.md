# Office 自动化回归探针

这两个 VBScript 通过真实的 `Word.Application`、`Excel.Application` COM 类驱动安装在专用 Wine prefix 中的 Office，而不是模拟应用或许可证。各收两个 Windows 路径参数：本地输出文件、进度日志。进度每步立即写入，卡住时不必等待 `cscript` 的 stdout 缓冲。运行时加超时；**不发送邮件、不上传云文档、不打印**。

前提：Click-to-Run 的 `root/vreg/*.vreg.dat` 含真实 COM 注册。若 `HKCR\Word.Application\CLSID` 等为空，先用系统 Python（有 `hivex`）运行 `scripts/export-office-registry.py --automation-only --win-c "$WINEPREFIX/drive_c" -o <新目录>`，审阅生成的 `31-automation-*.reg`、备份现有注册表后，才用 `DIST=/opt/wine-altars WINEPREFIX=... scripts/apply-office-registry.sh <目录>` 导入。导入脚本等待 wineserver 退出，**先关闭该 prefix 的 Office 进程**。输出来自本机安装包，不复制他机账户/许可、不编造 CLSID。

例如让 Wine 的 `cscript.exe //nologo` 执行 `word-save.vbs`，传入 `Z:\tmp\word-check.docx`、`Z:\tmp\word-progress.txt`。Word 成功后验证文件是合法 ZIP，包含 `[Content_Types].xml` 和 `word/document.xml`，正文有 `wine-altars Word automation smoke`。

2026-09-26 实测：Word 的六步进度全部完成，保存的 DOCX 可解包且正文一致；Excel 的 `CreateObject` 和 `Visible` 已成功，`Workbooks.Add` 卡住，**`excel-save.vbs` 不是通过的测试**。独立启动 Excel 可绘制开始屏幕，但点空白工作簿后标题变为「工作簿1」，屏幕仍停在开始屏幕。PowerPoint、Outlook 的自动化和文件格式尚未验收。
