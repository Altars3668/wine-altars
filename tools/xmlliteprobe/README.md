# xmllite 在文档结束处的原生对照

`xmleof.c` 把几十份在各处截断或多出东西的小文档（未闭合的元素、停在文本/注释/CDATA/处理指令/名字/属性里、空文档、只有声明、根元素之后又来一个元素或声明……）各用 UTF-16（无 BOM）和 UTF-8 喂给一个新的 `IXmlReader`，打印每次 `Read` 的结果、节点类型、名字、深度，带值的节点还打印 `GetValue`；到第一个不是 `S_OK` 的结果之后再多读两次，看错误是否粘住。流用 `SHCreateMemStream`，长度正好是文档长度。

    scripts/build-probe.sh tools/xmlliteprobe/xmleof.c tools/xmlliteprobe/xmleof.exe ole32 xmllite shlwapi
    WIN_HOST=… WIN_USER=… WIN_PORT=… scripts/winrun.sh tools/xmlliteprobe/xmleof.exe > tools/xmlliteprobe/xmleof.win.txt

`xmleof.win.txt` 是 winref（build 29671）上的输出。

## 测出的契约

- 元素还没闭合文档就完了：`MX_E_INPUTEND`（0xc00cee01），之后每次都是它，节点类型 None。文档停在文本里时先把这段文本作为完整的 Text 节点给出（`<a>text` 的值是 `text`），下一次才是 `MX_E_INPUTEND`。
- 停在注释、CDATA、处理指令里：`WC_E_COMMENT`、`WC_E_CDSECT`、`WC_E_PI`；停在名字里（`<a`、`</b`）：`NC_E_QNAMECHARACTER`；名字还没开始（`<a>t<`、`<a><b x="1"`）：`MX_E_INPUTEND`。
- 没有根元素（空文档、只有空白/声明/注释）或根元素之后又有元素、CDATA：`WC_E_ROOTELEMENT`；根元素之后的 `</b>`：`NC_E_QNAMECHARACTER`；之后的文本、根元素之前的文本：`WC_E_SYNTAX`。
- 开头以外任何地方的 `<?xml`，包括大小写不同的 `<?XML x?>`：`WC_E_TEXTXMLDECL`；`<?xml-stylesheet ...?>` 是普通处理指令。
- 没有 BOM、也不以 `<` 开头的文档按 UTF-8 读（单个空格是 Whitespace 节点，然后 `WC_E_ROOTELEMENT`）。

Wine 原先：未闭合的文档在末尾无限重复最后一个节点（元素每次深一层），注释/CDATA/处理指令的结束符跨在两次读入的块之间时整个看不见，只有声明的 UTF-8 文档触发断言，UTF-16 的同类文档把开头读两遍；wine-src `8b58e3c` 之后只剩“无 BOM 的 UTF-16 以空格开头”两例（Windows 把其中的 NUL 字节当非法字符报 `WC_E_SYNTAX`）不同。
