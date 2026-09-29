# MSXML 的 XPath 扩展函数：`xpathextprobe.exe`

用 MSXML 6 的 `DOMDocument60`，`SelectionNamespaces` 声明 `xmlns:ms='urn:schemas-microsoft-com:xslt'`，对 `<r x='b'><e>b</e></r>`
逐个求 `/r[<表达式> = -1]`、`= 0`、`= 1`，打印哪个值让谓词通过（`none` 表示三者都不通过，即结果不是这三个数），或查询失败的
HRESULT。

起因：Office 的 App-V 运行时用
`/appv:Metadata/appv:OpaqueDirectories/appv:Entry[(ms:string-compare(@Long, "root\VFS\Common AppData", "", "i") = 0) or ...]`
挑选包的不透明目录；上游 Wine 新写的 XPath 引擎没有这个函数，且带前缀的函数调用根本编译不过，查询失败，任何 Office
进程都进不了包的虚拟环境。

`results/xpathextprobe.win.txt`（Windows 11 build 29671，zh-CN 系统、装有 en-US 界面语言）的要点：

- `ms:string-compare(x, y)` 按语言学顺序返回 -1/0/1：区分大小写且小写在前（'a' < 'A' < 'b'），连字符按 word sort 处理
  （'a-b' 在 'ab' 之后），区分重音，'10' 在 '9' 之前，数字参数先转成字符串。
- 第四个参数：`''` 同默认，`'i'` 忽略大小写，`'u'` 在只差大小写时让大写在前；`'iu'`、`'I'`、`'x'` 使查询失败（E_FAIL）。
- 参数少于 2 个或多于 4 个，查询失败。
- 第三个参数是语言：`'en-US'`、`'EN-us'`、`'en'`、`'zh-CN'`、`'de'` 可以，`'en_US'`、`'xx-XX'` 失败；**`'de-DE'`、`'sv-SE'`、
  `'fr-FR'` 在这台机器上也失败**，规则未明（可能与本机装的语言有关），Wine 这里接受任何有效区域名。
- `ms:utc`、`ms:local-name`、`ms:namespace-uri`、`ms:number` 存在（查询不失败，结果不是 -1/0/1）；`ms:type-is` 无模式时失败。
  这四个函数 Wine 尚未实现。

构建：`scripts/build-probe.sh tools/xpathextprobe/xpathextprobe.c tools/xpathextprobe/xpathextprobe.exe ole32 oleaut32`。
