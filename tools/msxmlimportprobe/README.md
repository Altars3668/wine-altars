# importprobe：IXMLDOMDocument3::importNode 对各类节点的回答

Wine 的一致性测试曾预期导入文档节点返回 `E_FAIL`，Windows 回答的是 `E_INVALIDARG`。测试只问了文档节点；这个探针把
一个文档能给出的每类节点都问一遍——文档、文档类型、DTD 里的实体与记号、实体引用、元素、属性、文本、CDATA、注释、
处理指令、片段——深拷贝与浅拷贝，MSXML 3 与 6，打印 HRESULT、输出指针是否清空、克隆的 xml。不读写文件，不联网。

```
importprobe        （服务会话即可）
```

## 结论（Windows 11 29671，results/windows-29671.txt）

- 文档、文档类型、实体、记号：深浅都是 `E_INVALIDARG`，输出 NULL（altars-up `361f375c387` 起 Wine 相同）。
- 其余节点都能导入。
- MSXML 3.0 的文档在 Windows 上**不提供** `IXMLDOMDocument3`（`CoCreateInstance` 得 `E_NOINTERFACE`）；Wine 提供。未改。
- Windows 保留 DTD 里声明的内部实体的**实体引用节点**（`&e;`），Wine 把它展开成文本。未改，留作后续。
