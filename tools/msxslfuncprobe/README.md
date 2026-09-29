# MSXML 的扩展函数（`urn:schemas-microsoft-com:xslt`）：`msxslfuncprobe.exe`

第一部分：对一批语言名，并列打印 MLang 的解析（`Rfc1766ToLcidW`、注册表 RFC 1766 表）和 `ms:string-compare` 是否接受。
第二部分：每个表达式作为 XSLT `value-of` 的结果，以及同一表达式在 `selectNodes()`（`SelectionNamespaces` 声明 `ms`）
里能否编译、谓词真假。源文档 `<r xmlns:s='urn:source-s' x='b'><e>b</e></r>`，样式表另声明 `xmlns:a='urn:style-a'`。

`results/msxslfuncprobe.win.txt`（Windows 11 build 29671，zh-CN 用户区域）的要点：

- **语言参数**：接受且仅接受 `Rfc1766ToLcidW` 返回 S_OK 的名字（`de`、`en-us`、`zh-cn`、`pt-br`）；S_FALSE（`de-DE`、
  `fr-FR`、`ja-JP`）和 E_FAIL 都拒绝；空串表示用户默认区域。`ms:format-date/format-time` 遇到被拒的语言在**运行时**
  失败（E_FAIL，描述里带着那个名字），不是编译错误。
- `ms:utc(s)`：解析 XSD 的 dateTime/date/time/gYearMonth/gYear，带时区的换算成 UTC 后**去掉时区**输出；秒的小数截到
  3 位（`.1234567`→`.123`，`.5`→`.500`）；只有时间时日期部分输出成 10 个 `-`（`----------T10:00:00`）；`24:00:00`
  进位到次日零点；首尾空白忽略；非法日期（2 月 30 日）、非日期串、数字参数都给空串；无参数是编译错误。
- `ms:local-name(s)`：合法 QName 的本地名（`a:b`→`b`，`b`→`b`），其余（`a:b:c`、`:b`、`a:`、带空白、空串）为空串。
- `ms:namespace-uri(s)`：按**上下文节点**（源文档里的节点）的在作用域命名空间解析前缀：`s:b`→`urn:source-s`、
  `xml:lang`→XML 命名空间；样式表或 `SelectionNamespaces` 里声明的前缀不算；无前缀为空串。
- `ms:number(s)`：按 XSD double 语法：`1e3`→1000、`INF`→Infinity、`-INF`、`NaN`、`+1`、`.5`、`5.` 都认，`1,5` 和空串
  为 NaN，首尾空白忽略；数字参数先转成字符串。对照：XPath 的 `number('1e3')`、`number('INF')` 都是 NaN。
- `ms:format-date(dt, fmt [, lang])`、`ms:format-time(...)`：`GetDateFormat`/`GetTimeFormat` 的图片串（`yyyy-MM-dd`、
  `dddd, MMMM d, yyyy`、`hh:mm:ss tt`、`h:m:s t`），省略或空格式用该区域的短日期/时间格式（zh-CN 为 `2026/9/29`、
  `13:05:03`）；时间里的小数秒忽略；不能解析的日期给空串。
- `ms:type-is` 无模式时编译失败；`ms:type-local-name()`、`ms:type-namespace-uri()` 为空串，
  `ms:schema-info-available()` 为 false。
- 以上函数在 XSLT 和 XPath（MSXML 6）里都存在。

改动前的 Wine：XSLT 里一个都没有（libxslt 只注册了 `msxsl:node-set` 和脚本函数），XPath 里只有 `ms:string-compare`，
且它接受任何有效区域名。

构建：`scripts/build-probe.sh tools/msxslfuncprobe/msxslfuncprobe.c tools/msxslfuncprobe/msxslfuncprobe.exe ole32 oleaut32 mlang advapi32`。
