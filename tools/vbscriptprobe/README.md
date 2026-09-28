# VBScript 的原生对照

五个脚本在 `cscript //nologo <脚本> <输出文件>` 下逐项求值，把错误号（或结果类型与值）写成 UTF-16 文本，在 Windows 与 Wine 上各跑一次后逐行 diff。`results/*.win.txt` 是 Windows 11 build 29671（中文界面）的输出。

| 脚本 | 内容 |
|---|---|
| `errors.vbs` | `Err.Raise n` 对 1–65535 取描述：即 vbscript.dll 字符串表里的每一条（130 条；Windows 用错误号直接当资源 ID） |
| `expressions.vbs` | 116 个内建函数/运算对 Null、Empty、对象、越界与非法参数的结果或错误号 |
| `runtime.vbs` | 除零、对非对象调方法、Execute 里的语法错误与未定义过程、`GetFile` 等常见运行时错误的号、来源与描述 |
| `objects.vbs` | 对象作参数：哪些内建函数按对象本身接收（`IsObject/TypeName/VarType/Is*/Array`），其余取默认属性值（Dictionary 的默认属性要参数 → 450，RegExp 没有 → 438） |
| `files.vbs` | 未定义名的调用、`LoadPicture`（用 ADODB.Stream 造一张 1×1 位图，Wine 没有 ADODB 时这几行会是 432）、FileSystemObject 对各种不存在路径的错误号 |

2026-09-28 的结果：移植上游 Wine 自 11.0 起的 179 个 vbscript 提交（Eval/Execute/ExecuteGlobal/GetRef、DateDiff/DatePart、Filter、Escape 等原本都是桩）并补齐其余差异后，`errors.vbs` 只差 30000（中文为“ZH”，英文原文无从核实，未加），其余四个脚本与 Windows 逐行相同（`files.vbs` 的位图几行在 Wine 下另用预先造好的位图核对）。见 `docs/office365-under-wine.md` 的 VBScript 一节。
