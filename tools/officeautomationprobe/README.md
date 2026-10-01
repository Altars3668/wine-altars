# Office 自动化回归探针

这三个 VBScript 通过真实的 `Word.Application`、`Excel.Application`、`PowerPoint.Application` COM 类驱动安装在专用 Wine prefix 中的 Office，而不是模拟应用或许可证。各收两个 Windows 路径参数：本地输出文件、进度日志。进度每步立即写入，卡住时不必等待 `cscript` 的 stdout 缓冲。运行时加超时；**不发送邮件、不上传云文档、不打印**。

前提：Click-to-Run 的 `root/vreg/*.vreg.dat` 含真实 COM 注册。若 `HKCR\Word.Application\CLSID` 等为空，先用系统 Python（有 `hivex`）运行 `scripts/export-office-registry.py --automation-only --win-c "$WINEPREFIX/drive_c" -o <新目录>`，审阅生成的 `31-automation-*.reg`、备份现有注册表后，才用 `DIST=/opt/wine-altars WINEPREFIX=... scripts/apply-office-registry.sh <目录>` 导入。导入脚本等待 wineserver 退出，**先关闭该 prefix 的 Office 进程**。输出来自本机安装包，不复制他机账户/许可、不编造 CLSID。

例如让 Wine 的 `cscript.exe //nologo` 执行 `word-save.vbs`，传入 `Z:\tmp\word-check.docx`、`Z:\tmp\word-progress.txt`。Word 成功后验证文件是合法 ZIP，包含 `[Content_Types].xml` 和 `word/document.xml`，正文有 `wine-altars Word automation smoke`。

2026-09-26 的前测：Word 的 DOCX 保存成功；Excel 虽然 COM 激活成功，`Workbooks.Add` 却阻塞，独立启动后点空白工作簿只改标题、画面仍停在开始屏幕。根因是 dcomp 的 `InteractionSources.RemoveAll` 没解除原 Visual 的交互源预留；修复并按 Windows 实测区分 `RemoveAll` 与单项 `Remove` 后，**同一 Excel 探针已通过**：新建工作簿、填写 A1、保存并关闭，XLSX 解包校验通过；直接点击空白工作簿也绘出了表格网格。PowerPoint 的幻灯片创建、标题修改、保存 PPTX 和解包校验通过；脚本进度写到“saved”，PowerPoint 进程已退出，但“closed”标记未写到，不能据此单独声称退出回调语义已验清。Outlook 尚未启动或访问邮箱，也没有发送邮件。

`powerpoint-show.vbs` 打开已有演示文稿、显示指定秒数后关闭（参数：演示文稿、进度日志、可选秒数），用于在 PowerPoint 真实显示时截取窗口。在无根 Xwayland 上根窗口不能读取像素，应按窗口 ID 截取 PowerPoint 顶层窗口；:2 这类竖屏显示器上幻灯片位于编辑区的中下部，只截上半部分会误以为幻灯片空白。

`word-save.vbs` 只退出自己启动的 Word（`GetObject` 接到的已运行实例不动）；早先版本不调用 `Quit`，自动化启动的 Word 会在脚本结束后一直驻留。`scripts/office-regress.sh [word] [excel] [powerpoint]` 依次运行三个保存探针，报告退出码、文件是否为含目标部件的合法 OOXML，以及探针到达的步骤；它等待各应用进程退出，而不是 `wineserver -w`（Click-to-Run 服务常驻，wineserver 不会退出）。

`powerpoint-effects.vbs <输出.pptx> <进度日志> <图片> [秒数]` 在一张空白幻灯片上依次施加 PowerPoint 经 Direct2D 绘制的效果：渐变、阴影、发光、柔化边缘、映像、三维棱台、图片（灰度、亮度对比度、模糊艺术效果）、文字的发光/阴影/映像。每一步单独容错并记录结果，保存后保持显示若干秒供截图，再退出。某一步让 PowerPoint 崩溃时，其后各步都会以 462（`1CE`）失败，第一条失败即是崩溃点。


`excel-charts.vbs <输出.xlsx> <进度日志> [秒数]` 填一小张表，再加上 Excel 经 Direct2D/DirectWrite 绘制的内容：带阴影的簇状柱形图、折线图、三维饼图、数据条、三色色阶、图标集、两种迷你图，保存后保持显示若干秒供截图，再退出。每一步单独容错并记录结果。探针只调 COM 接口，不读用户文件。要看 Excel 的 d2d 日志，先 `wineserver -k`，再以带 `WINEDEBUG` 的 `cscript` 作为会话首个进程启动它：COM 激活出的 Excel 继承会话首个进程的环境，也把输出写到它的 stderr；截图要带 `:2` 的 `XAUTHORITY`，按标题“… - Excel”找窗口。

`powerpoint-gradients.vbs <输出.pptx> <进度日志> [秒数]` 在一张空白幻灯片上放各种渐变填充：30° 的线性渐变、从中心、从角部、多 stop 的彩虹预设、渐隐到透明（压在一条深色条上，看透明部分是否透出）、中间 stop 半透明的三色渐变，以及渐变填充的文字；保存后保持显示若干秒供截图，再退出。每一步单独容错并记录结果。

`word-embed.vbs <输出.docx> <进度日志> [要打包嵌入的文件] [显示秒数] [选项]` 在新文档里用 `InlineShapes.AddOLEObject("Excel.Sheet.12")` 嵌入一张 Excel 工作表（给了文件时再嵌入它的包），保存、关闭、重新打开，读嵌入对象的类，然后像双击那样 `OLEFormat.Activate` 就地激活，取 `OLEFormat.Object` 读 `Worksheets(1).Name`，关闭并退出。每一步单独容错并记录结果；激活失败时隔十秒再试，共三次。给了显示秒数时 Word 可见，工作表保持就地激活这么久再继续。选项用逗号分隔：`open` 改用 `DoVerb(OLEIVERB_OPEN)` 在 Excel 自己的窗口里打开；`new` 不附着已在运行的 Word、只启动并退出自己的实例（在别人的 Windows 机器上必须用它）；`existing` 不新建文档、直接打开已有的输出文档（某账户第一次新建文档时 Word 会弹“自动保存新文件？”并一直等人回答）；`wait` 在激活前写下 `waiting`，等到“进度日志名 + .go”这个文件出现才继续，好先挂上调试器。ActiveX 控件不试：Microsoft 365 在激活之前就按策略拒绝（“由于您的策略设置，无法插入此对象。”）。它会以 `-Embedding` 启动 Excel；Word 的激活过滤器、跨进程的就地激活与 OLE 默认处理器都走一遍（见 `tools/comprobe/README.md`、`tools/subclassprobe/README.md`）。2026-09-28 起整个脚本在 Wine 下走完；有一次 Word 在最后 `Quit` 时崩溃（wwlib 内空指针，未能复现），另有一次 Word 退出后 Excel 过了几秒才退出。

`embedevents.exe <word-embed.vbs> <输出.docx> <进度日志> <显示秒数> [选项] [窗口.bmp]` 运行 `word-embed.vbs`（选项缺省为 `new`），用进程外的 WinEvent 钩子记录期间新出现的 Word 与 Excel 进程的每个窗口的创建、显示、隐藏、移动（只记 EXCEL* 与 `_WwG`）、换父窗口与销毁，注明事件来自哪个进程的线程，与脚本进度交错打印；显示期将尽时列出两者的窗口树，给了文件时用 `PrintWindow` 存下 Word 窗口画的内容（屏幕上别的东西不会进去）。启动前已在运行的 Office 进程只计数、不碰。

    scripts/build-probe.sh tools/officeautomationprobe/embedevents.c tools/officeautomationprobe/embedevents.exe user32 gdi32

在 Wine 下“由哪个线程引起”会显示为窗口所属的线程：跨线程的 `SetWindowPos` 在 Wine 里由窗口所属线程执行；要知道调用者，用 `+win` 看 `NtUserSetWindowPos` 所在的线程，或在调用者进程里对 `win32u.dll` 的 `NtUserSetWindowPos` 下断点（`tools/bptrace`，`BPTRACE_ARG7=8f` 只报隐藏）。

2026-09-28 在 winref 上三次都没能得到原生的就地激活：Excel 以嵌入方式启动、加载完第三方加载项后，Word 的激活仍以 0x17B5（“……请确保 Excel 中的任何对话框都已关闭”）失败，Excel 从未建 EXCEL9 窗口；当时会话锁屏。另见 `docs/office365-under-wine.md` 的就地激活一节。

`word-spell.vbs <进度日志>` 看 Word 的校对能不能用：美式英语用的是哪个拼写词典，一句美式英语里拼错的三个词 Word 标出哪些，给 `mispeled` 的建议，以及 `CheckSpelling` 对拼对、拼错的词各答什么（布尔值以 `CInt` 写出，-1 为真，与界面语言无关）。只退出自己启动的 Word，不保存文档。2026-09-28 在 Wine 下：词典是 Office 自带的 `PROOF\MSSP7EN.LEX`，三个错词都标出，建议为 `misspelled`、`misplead`——Word 的拼写检查不依赖 Windows 的拼写检查 API（`ISpellCheckerFactory`）。

`word-pdf.vbs <输出.pdf> <进度日志>` 在新文档里写标题、一行英文、一行中文、一张 2×2 的表和一个形状，记下 Word 报告的页数，用 `ExportAsFixedFormat`（`wdExportFormatPDF` = 17）导出 PDF，不保存文档；只退出自己启动的 Word。导出时 Word 对每个用到的字体调 `fontsub.dll` 的 `CreateFontPackage`（字形列表、平台 3、编码 0xFFFF）。2026-09-28：原先 fontsub 把整个字体原样返回，这一页 PDF 嵌入了两份完整的等线，19.3 MB；fontsub 照 Windows 取子集后是 39 KB，`pdftoppm` 渲染与嵌入完整字体时逐像素相同（`tools/fontsubprobe`）。

`excel-sweep.vbs <输出目录> <进度日志> [图片目录]` 逐项跑依赖 Windows 组件的 Excel 功能：新旧公式（动态数组、XLOOKUP、LET、LAMBDA）、数字与条件格式、经典与新式图表、迷你图、表、数据透视表、排序筛选、数据验证、批注、超链接、图片目录里每种格式的图片、形状与 SmartArt、查找替换、分列、单变量求解、Excel 能写的每种格式、加密保存后用密码打开（错误密码被拒）、重新打开存下的工作簿、保护。每项记下结果或错误（`ok`/`FAIL`），一项失败不影响后面的；不打印、不发邮件、不碰云服务，输出只写进给定目录（不要给前缀里指向真实主目录的“文档”）。新式图表（瀑布图等）的数据来自选区，Windows 上的 Excel 对它们的 `SetSourceData` 同样报 445，所以只问系列数。2026-09-28 在 Wine 下 80 项全部通过；见 `docs/office365-under-wine.md` 的 Excel 功能普查一节。

`word-embedfonts.vbs <输出.docx> <进度日志>` 在新文档（缺省字体，外加一行中文）上打开 `EmbedTrueTypeFonts` 与 `SaveSubsetFonts`，存为 DOCX，关闭再打开，记下嵌入设置和正文开头。Word 保存时以字符列表调 `CreateFontPackage`，嵌入的 `word/fonts/font1.odttf` 是混淆过的子集：原先是 16 MB 的完整等线，现在未压缩 780 KB（字形号保持不变，所以度量表仍按原字形数），重新打开正常。

`office-paste.vbs <进度日志>` 经剪贴板在 Excel 与 Word 之间复制：Excel 复制 A1:B2，在 Word 里 `Selection.Paste`（成为表格），再两次 `PasteSpecial` 为 OLE 对象（`wdPasteOLEObject`），一次不给位置、一次给 `wdInLine`；Word 复制文字后 `Worksheet.Paste` 到 Excel 的 D1。它会占用剪贴板，只在没有人用的显示上跑（开发用的无头 `:2`），不要在有人使用的桌面上跑。2026-09-28 在 Wine 下全部成功：不给位置时对象是浮动的 Shape（类型 7，`Excel.Sheet.12`），给 `wdInLine` 时是 InlineShape（类型 1，`Excel.Sheet.12`）；路径是 `OleCreateFromDataEx` 从 Excel 的剪贴板数据取 Embed Source，经默认处理器以 `-Embedding` 启动 Excel。早先只数 InlineShapes，误以为没有粘上。

`word-cjkfonts.vbs <输出.pdf> <进度日志>` 每段用一种中文字体写同一句中文——Windows 自带而 Office 不带的宋体、新宋体、黑体、楷体、仿宋（中文名，外加 SimSun、SimHei 英文名），和 Office 自己装的微软雅黑、等线、华文宋体——导出 PDF；`pdffonts` 看实际嵌入了哪些字体，`pdftoppm` 渲染看有没有豆腐块。2026-09-29 在 Wine 下：宋体/SimSun 用的是 Microsoft 365 **云字体**下载到 `AppData\Local\Microsoft\FontCache\4\CloudFonts\SimSun` 的真宋体，新宋体、黑体、楷体、仿宋、SimHei 这次回退到微软雅黑，全部字形正常。所以 CrossOver 在 GDI 里“缺宋体就换成主机中文字体”的 HACK（bugs 13095/13610）对 Office 用不着。

`word-type.vbs <进度日志> <go 文件>` 让 Word 可见地新建一个文档并等待，另一个程序往里键入、go 文件出现后，把文档正文和其中每个非可打印 ASCII 字符的码点写进进度日志，然后不保存退出。配合 `tools/sendkeys`（会话内 SendInput，`KEYEVENTF_UNICODE`）使用：

    sendkeys -w OpusApp -d 200 "text:hello 中文输入测试，你好。" enter "text:第二行 ABC"

2026-09-28 在 Wine 下（`:2`）：正文为“Hello 中文输入测试，你好。”“第二行 ABC”两段（Word 把 hello 自动改成首字母大写），没有崩溃；9 月 4 日记录的“键入中文时 Word 以 0xe0000002 崩溃”不再出现。这只验证了 Unicode 字符输入，经 X 输入法（XIM）的组字路径没有测。

`word-math.vbs <输出.pdf> <进度日志> [公式字体或 ""] [显示秒数]` 在新文档里用线性格式写两个公式（a^2+b^2=c^2 与求根公式），`OMaths.Add` 后 `BuildUp` 成专业格式，记下 Word 数到的公式数、各自的函数数与文字，导出 PDF；给了字体就把文档的公式字体设成它，给了秒数就让 Word 可见地停留这么久以便截图。只退出自己启动的 Word，不保存。2026-09-28：Wine 下没有 Cambria Math，GDI 给了 Liberation Sans，公式在屏幕和 PDF 上都是空的；换成带 MATH 表的自由字体后出现。wine-src `a84f7a5` 让已装的自由数学字体（DejaVu Math TeX Gyre 等，TrueType 轮廓的优先）在没有 Cambria Math 时顶替它，默认公式字体下屏幕与 PDF 都正常。选 CFF 轮廓的数学字体（Latin Modern Math、系统同名的 STIX Two Math .otf）时，Word 导出 PDF 会把字形画成图像，画面上出现贯穿的竖线，原因未查清（`tools/glyphrasterprobe`）。

`powerpoint-sweep.vbs <输出目录> <进度日志> [图片目录]` 逐项跑依赖 Windows 组件的 PowerPoint 功能：各种版式的幻灯片与中英文文字、带样式的表格、
图表、SmartArt、艺术字、带渐变/阴影/发光/柔化边缘/映像/棱台的形状、图片目录里每种格式的图片、脚本自己写的 SVG 与 3D 模型（OBJ）、超链接、
备注、批注、切换（含平滑切换）、动画、节、查找替换、把幻灯片导出为 PNG/JPG/SVG/EMF、PowerPoint 能写的每种格式（pptx、ppt、pptm、potx、
ppsx、严格 OOXML、odp、xml、pdf、xps、PNG 目录）、加密保存后用密码打开（错误密码被拒）、重新打开存下的文件、标记为最终版本（先存盘，
否则会弹出“另存为”）、放映并翻页再退出，最后是动画 GIF 与视频（两者在后台生成）。每项记下结果或错误，一项失败不影响后面的；不打印、不
发邮件、不碰云服务，输出只写进给定目录。PowerPoint 是单实例：在别人的 Windows 机器上跑之前先确认它没在运行。

2026-09-28 在 Wine 下的进展：插入 3D 模型曾使 PowerPoint 崩溃（桌面没有 DACL，wine-src `bfd4e06c`）；带映像与棱台的形状曾使导出图片与
另存为 PNG 失败（DXGI 表面渲染目标各用各的 D2D 设备，wine-src `32ea29df`）；导出 WMF 曾不出文件（`GdipEmfToWmfBits` 是桩，wine-src
`94ff49cf`）。winref 上的原生 PowerPoint（build 20522）对同一演示文稿：SVG 导出同样报“转换器未安装”，WMF 13 MB，XPS 299 KB，
`SaveCopyAs` 动画 GIF 同样不出文件但之后照常响应，视频 5 秒生成。

2026-09-29 在 altars-up 上：WIC 的 GIF 元数据写入链补齐后（wine-src-up `01c911684cf`），`SaveCopyAs` 2 秒写出两帧循环
GIF，之后 Close、Quit 照常，48 项通过；视频在 SinkWriter 的编码与收尾补上后（`67c84e5b7dc` 等）4 秒生成 174 KB，
只剩 SVG 导出（与原生相同）。

`office-vba.vbs <进度日志>` 查 VBA 宏能不能跑：在 Word、Excel、PowerPoint 里各新建文档，经 VBA 工程对象模型加一个模块，
用 `Application.Run` 运行其中的函数（算术、`Format`、`CreateObject("Scripting.Dictionary")`、`On Error` 接住除零、
`Declare PtrSafe` 调 kernel32 的 `GetCurrentProcessId`），Word 里再跑一个改写正文的宏，Excel 里在单元格用自定义函数。
Office 运行宏前让 AMSI 扫描代码，所以它也验证了扫描器（clamd）可达。“信任对 VBA 工程对象模型的访问”（各应用
`Security\AccessVBOM`）只在运行期间打开，**等应用进程真正退出后**再按原状恢复并回读核对：应用退出时会把内存里的
信任中心设置写回注册表，`Quit` 返回时恢复会被它覆盖；而且 Excel、PowerPoint 要等脚本放掉全部引用才会退出。
写模块时 `Declare` 必须放在声明区、所有过程之前，否则 VBA 报编译错误并弹出模态对话框（自动化会一直等它）；
PowerPoint 的 `Run` 要带模块名，模块名随界面语言（中文是“模块1”）。2026-09-29 三个应用 33 项全部通过；此前 Excel
读过 `VBProject` 后 `Quit` 不退出，是 combase 在调用结束后才从分派线程释放存根（altars-up `53a55969585`）。

`excel-powerquery.vbs <进度日志>` 看 Excel 的 Power Query 能不能用：先列出 COM 加载项及是否已连接（自动化启动的 Excel 不加载 COM
加载项，所以这里全是未连接，要看加载项得正常启动 Excel），再用 M 公式建查询（`#table` 再 `Table.AddColumn`），经
`Microsoft.Mashup.OleDb.1` 加载到表、同步刷新，读回 A1:C3，应为 `a,b,c | 1,2,12 | 3,4,34`。不保存、不打印、不碰云服务。Power Query
在 .NET Framework 容器进程里求值，前缀里没有 .NET 4.x 时刷新会失败。2026-10-01 装了 .NET 4.8 后在 Wine 下通过。

`outlook-pst.vbs <进度日志> <条数>` 给 Outlook 的 PST 引擎加压，只用编造的数据：先用只有 PST、没有邮件帐户的配置文件启动
Outlook（`outlook.exe /PIM <配置名>`；把 `HKCU\Software\Microsoft\Office\16.0\Outlook` 的 `ForcePSTPath` 设到前缀里的目录，
PST 就不会落进宿主的“文档”），脚本附着上去，确认默认存储是 PST（不是帐户的 OST）后，在测试文件夹里写入指定条数的张贴条目
（新建的邮件条目无论在哪个文件夹建，存盘都进“草稿”），再全部读回核对。读正文会触发 Outlook 的对象模型防护提示（见文档），
要在提示里允许访问。
