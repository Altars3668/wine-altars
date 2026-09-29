# SAX 读取器的属性与上限

`saxpropprobe.exe` 量 MSXML 3.0 与 6.0 的 SAX 读取器（`SAXXMLReader30`、`SAXXMLReader60`）：

- 每个属性经 `ISAXXMLReader` 与 `IVBSAXXMLReader` 读出的类型与值，新建时、解析有无 XML 声明的字符串与流之后各一遍；
- `max-xml-size`、`max-element-depth` 接受哪些值（各种 VARIANT 类型、负数、上界）；
- 设了上限之后解析：在多少字节或字符处失败、返回什么、错误处理器收到什么（位置、消息），以及失败前报了哪些内容事件；
- Office 反复向 MSXML 对象 QueryInterface 的两个未公开 IID（`{e19c7100-…}`、`{c970c32d-…}`）原生是否实现。

`results/saxpropprobe.win.txt`（Windows 11 build 29671，中文界面）的要点：

- VB 接口取处理器属性得 `VT_DISPATCH`；`xmldecl-version/encoding/standalone` 在 6.0 里是上一个文档的 XML 声明（没有声明就是 NULL），
  3.0 永远是 NULL；`charset` 3.0 为 `VT_EMPTY`、6.0 为 NULL 串；`schema-declaration-handler` 3.0 是 `E_INVALIDARG`；`dom-node` 6.0 是
  `E_FAIL`；不认识的名字是 `E_INVALIDARG`。
- `max-element-depth` 默认 3.0 为 5000、6.0 为 256；两个上限都接受能转成范围内整数的值（`R8 2.5` 得 2、`"7"` 得 7），`max-xml-size`
  上界 4194303（6.0 收下 4194304 但记作 0）。
- `max-xml-size` 以 KB 计：流按字节，字符串 6.0 按字节、3.0 按字符。超限时 3.0 先报 `startDocument` 再以 `E_ABORT`（“系统错误:
  MaxXMLSize。”）停在 1:1，6.0 什么都不报、以 0xc00cee91 停在 1:0；嵌套超限两者都停在那个开始标签（3.0 在名字开头、6.0 在名字之后），
  分别是 `E_ABORT` 与 0xc00cee92。
- 两个 IID 原生也是 `E_NOINTERFACE`，Wine 日志里对应的 ERR/FIXME 只是噪声。

构建：`scripts/build-probe.sh tools/saxpropprobe/saxpropprobe.c tools/saxpropprobe/saxpropprobe.exe ole32 oleaut32 uuid`。
