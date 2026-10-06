# msxmlmore：msxml 文档还有哪些地方和 Windows 不一样

接着 `tools/msxmlstreamprobe`、`tools/msxmlimportprobe`，把剩下的未知点一次问完。不读写文件，不联网，流都是探针自己的对象
（堆上分配、不释放：msxml 可能在加载返回后还持有它）。文本里 ASCII 以外的字符一律写成 `\uXXXX`。

```
msxmlmore [interfaces] [reasons] [pending] [blocks] [entities]      （服务会话即可）
```

| 段 | 问什么 |
|---|---|
| interfaces | 各文档类（DOMDocument、2.6、3.0、自由线程 3.0、6.0、自由线程 6.0）对 17 个接口的 QueryInterface |
| reasons | 14 种坏文档与 6 种流读失败：parseError 的代码、reason 原文、行、列、filepos、srcText |
| pending | 流回答 `E_PENDING`（一次、两次、五次、数据中间）时 load 的返回、readyState、何时再读、流的引用 |
| blocks | 每次 `Read` 请求多少字节（流每次最多给 0/1/7/100/4095/5000 字节时） |
| entities | DTD 内部实体的引用在 DOM 里的节点结构（这次没跑成，见下） |

## 结论（Windows 11 29671，results/windows-29671.txt；Wine 为 results/wine-7c86d99d82a.txt）

接口：
- MSXML 3.0 与自由线程 3.0 **不提供** `IXMLDOMDocument3`，6.0 提供；Wine 都提供。
- 各版本都提供 `IPersistMoniker`、`IProvideClassInfo`、`IOleCommandTarget`、`IServiceProvider`；自由线程版与 6.0 还提供
  `IMarshal`。Wine 这五个都没有。2.6 在 Windows 11 上未注册。

解析错误：
- Windows 拒绝、Wine 接受：未定义的实体（`0xC00CE002`）、文本里的控制字符（`0xC00CE508`）、两个根元素（`0xC00CE555`）。
- `<a` 是“元素未关闭”`0xC00CE55E`（Wine 报名称起始字符无效）；空的 `loadXML("")` 也记 `XML_E_MISSINGROOT`（Wine 不记）。
- 行、列、filepos 指向出错处，srcText 是出错的那一行（Wine 多为 0 与 NULL）。
- reason 是带参数的中文原文，如“结束标记 'b' 与开始标记 'a' 不匹配。”；非 XML 的错误用系统消息（“未指定的错误”“拒绝访问。”）。
  3.0 与 6.0 措辞略有不同。
- 流读失败时 load 返回 `S_FALSE`，唯独 `E_ACCESSDENIED`（0x80070005）直接返回该错误。
- 默认 `validateOnParse` 为真时，DTD 里没声明的元素是 `0xC00CE00D`；Wine 不做 DTD 校验。

`E_PENDING`：第一次 `E_PENDING` 后立即再读一次；连续第二次仍是 `E_PENDING` 就挂起——load 返回 S_OK、结果为真，
readyState 停在 3，没有根元素，文档持有流直到被释放，之后不再读。数据中间出现一次 `E_PENDING` 同样重读一次后继续。
Wine 把 `E_PENDING` 当失败。

读块：先请求 4095 字节，之后是 MSXML 分词缓冲剩余的大小（不限量时 4095、6144、4117…；每次给 1 字节时 4095、8190、8189…），
6.0 每次给 1 字节时前八次是 4094、4093…。逐字节复刻要模拟它的分词器，功能上没有意义，不做。加载 VT_UNKNOWN 时 Windows
先对源对象 QI 两次 `IXMLDOMDocument`（`IPersistStreamInit::Load` 一次）。

实体：这次三个文档在 Windows 上都因 `validateOnParse` 为真、DTD 没声明元素 `r` 而失败，节点结构没测到；下一批关掉校验再测。
