# msxmlmore：msxml 文档还有哪些地方和 Windows 不一样

接着 `tools/msxmlstreamprobe`、`tools/msxmlimportprobe`，把剩下的未知点一次问完。不联网；流都是探针自己的对象
（堆上分配、不释放：msxml 可能在加载返回后还持有它）；methods 段在 %TEMP% 写一个小 XML 文件，用完删掉。
文本里 ASCII 以外的字符一律写成 `\uXXXX`。

```
msxmlmore [interfaces] [reasons] [pending] [blocks] [entities] [epilog] [validation] [methods] [spaces]
                                                                                    （服务会话即可）
```

| 段 | 问什么 |
|---|---|
| interfaces | 各文档类（DOMDocument、2.6、3.0、自由线程 3.0、6.0、自由线程 6.0）对 17 个接口的 QueryInterface |
| reasons | 14 种坏文档与 6 种流读失败：parseError 的代码、reason 原文、行、列、filepos、srcText |
| pending | 流回答 `E_PENDING`（一次、两次、五次、数据中间）时 load 的返回、readyState、何时再读、流的引用 |
| blocks | 每次 `Read` 请求多少字节（流每次最多给 0/1/7/100/4095/5000 字节时） |
| entities | DTD 内部实体的引用在 DOM 里的节点结构：先关掉 validateOnParse，再在声明了元素的 DTD 下校验着读 |
| epilog | 根元素之后能有什么，不能有的各报什么错、报在哪里；`<?xml` 出现在别处与大小写变体 |
| validation | 26 个文档：每种 DTD 校验错误在加载时（validateOnParse）的代码与位置，及不校验加载后 validate() 的结果 |
| methods | IMarshal、IProvideClassInfo、IOleCommandTarget、IServiceProvider、IPersistMoniker 各方法的回答 |
| spaces | 不校验时，各种内容模型（EMPTY、ANY、#PCDATA、元素、混合、未声明、无 DTD）下文档保留哪些空白 |

## 结论（Windows 11 29671，results/windows-29671.txt；Wine 为 results/wine-9a6d1602ec0.txt）

接口：
- MSXML 3.0 与自由线程 3.0 **不提供** `IXMLDOMDocument3`，6.0 提供（altars-up `37ad0e3e213` 照此实现，3.0 文档的 IDispatch
  随之改为 IXMLDOMDocument2 的）。
- 各版本都提供 `IPersistMoniker`、`IProvideClassInfo`、`IOleCommandTarget`、`IServiceProvider`；自由线程版与 6.0 还提供
  `IMarshal`。Wine 这五个都没有。2.6 在 Windows 11 上未注册。
- 第 168 批 methods 段只跑完 Msxml2.DOMDocument 就崩了（探针的错：`GetCurMoniker` 交回的正是传进去的 moniker，探针多
  Release 了一次）：`IProvideClassInfo::GetClassInfo` 是 coclass “DOMDocument” {F6D90F11-9C73-11D3-B32E-00C04F990BB4}，实现
  IXMLDOMDocument2（默认）与源接口 XMLDOMDocumentEvents，没有 `IProvideClassInfo2`；`IOleCommandTarget::QueryStatus` 对
  命令 1–70 都是 S_OK、标志 0，未知命令组 `OLECMDERR_E_UNKNOWNGROUP`，`Exec(OLECMDID_STOP)` S_OK；`IServiceProvider` 没有站点
  时一律 `E_NOINTERFACE`、不碰输出；`IPersistMoniker::GetClassID` 与 `IPersistStreamInit` 的相同，`IsDirty` S_FALSE，
  不带绑定上下文的 `Load` 是 `E_INVALIDARG`（不完全可用时 `E_NOTIMPL`），之后 `GetCurMoniker` 仍交回那个 moniker。
  探针已改为数 moniker 的引用、不替它释放，其余类待下一批。

解析错误（altars-up `29dd3a40a14`、`56757fe71dc`）：
- 未定义的实体（`0xC00CE002`）、文本里的控制字符（`0xC00CE508`）、两个根元素（`0xC00CE555`）Windows 都拒绝；`<a` 是
  “元素未关闭”`0xC00CE55E`；空的 `loadXML("")` 记 `XML_E_MISSINGROOT`。行、列、filepos 指向出错处，srcText 是出错的那一行。
- 根元素之后：结束标记 `0xC00CE552`（报在名字上）、DOCTYPE `0xC00CE57E`（报在声明的名字上）、`]]>` `0xC00CE55C`、
  末尾单独的 `<` `0xC00CE562`、`<?xml ` `0xC00CE557`（报在名字上）、控制字符 `0xC00CE508`；引用与 CDATA 节是
  `0xC00CE556`，报在 `&`、`&#`、`<![CDATA[` 之后；其余文本报在开头。
- reason 是带参数的中文原文，如“结束标记 'b' 与开始标记 'a' 不匹配。”；非 XML 的错误用系统消息（“未指定的错误”“拒绝访问。”）。
  3.0 与 6.0 措辞略有不同。Wine 仍是英文、不带参数，个别是 “error”——还没做。
- 流读失败时 load 返回 `S_FALSE`，唯独 `E_ACCESSDENIED`（0x80070005）直接返回该错误（`bc4d55d3ccb`）。

DTD 校验（altars-up `72207003cd2`、`e7a811081f3`、`452c34eb44f`）：Windows 边读边校验，报遇到的第一个错误，位置就是它的
SAX 定位器在相应事件上给的位置——
- 元素不该出现在那里、未声明、属性不该有或缺必需属性（依此先后）：报在该元素起始标记的末尾；
- 不该有的文本（EMPTY 元素里连空格也算）：报在文本开头，`0xC00CE018`；
- 缺子元素：报在结束标记的名字上，一个都没有是 `0xC00CE011`，不全是 `0xC00CE012`；
- 错误的子元素 `0xC00CE014`，重复的 ID `0xC00CE200`，ID 值不是名称 `0xC00CE504`，单名称类型的值里有多个名称 `0xC00CE574`；
- IDREF 找不到 ID：报在文档末尾，没有 filepos 和 srcText（`0xC00CE00E`）。
- 不校验加载后 `validate()` 报同样的错误，位置全为 0；没有 DTD 时 `0xC00CE224`。
- 唯一的差别：6.0 保留 EMPTY 元素里的空格，`validate()` 随后报它；Wine 把它丢掉了（spaces 段下一批测）。

`E_PENDING`（`d3a2b898381`）：第一次 `E_PENDING` 后立即再读一次；连续第二次仍是 `E_PENDING` 就挂起——load 返回 S_OK、
结果为真，readyState 停在 3，没有根元素，文档持有流直到被释放，之后不再读。数据中间出现一次 `E_PENDING` 同样重读一次后继续。

读块：先请求 4095 字节，之后是 MSXML 分词缓冲剩余的大小（不限量时 4095、6144、4117…；每次给 1 字节时 4095、8190、8189…），
6.0 每次给 1 字节时前八次是 4094、4093…。逐字节复刻要模拟它的分词器，功能上没有意义，不做。加载 VT_UNKNOWN 时 Windows
先对源对象 QI 两次 `IXMLDOMDocument`（`IPersistStreamInit::Load` 一次）。

实体引用节点：内部实体的引用（属性值里也一样）在 DOM 里是只读的 entityref 节点，子节点是替换内容，可以嵌套；空实体是没有
子节点的 entityref；字符引用与预定义实体只是文本，并入相邻文本；`appendChild`、改其文本的 `put_nodeValue` 都是 `E_FAIL`；
`createEntityReference` 给声明过的实体填入替换内容，未声明的没有子节点，`get_text` 是空串而不是 NULL。3.0 与 6.0、校验与否
结构相同，只有对根元素 `selectNodes("text()")` 3.0 是 7、6.0 是 8。Wine 把实体展开成文本——还没做。
